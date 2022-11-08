// Copyright 2022 The Centipede Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "./jit_profiler.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>  // NOLINT
#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "absl/time/time.h"

namespace centipede::perf {

//------------------------------------------------------------------------------
//                           JitProfiler::Snapshot
//------------------------------------------------------------------------------

std::string JitProfiler::Snapshot::WhereStr() const {
  return absl::StrFormat("%s:%d", location.file, location.line);
}

std::string JitProfiler::Snapshot::ShortWhereStr() const {
  return absl::StrFormat(  //
      "%s:%d", std::filesystem::path(location.file).filename(), location.line);
}

std::string JitProfiler::Snapshot::WhenStr() const {
  return absl::FormatTime("%E4Y-%m-%dT%H:%M:%E2S", time, absl::LocalTimeZone());
}

std::string JitProfiler::Snapshot::ShortWhenStr() const {
  return absl::FormatTime("%H:%M:%E2S", time, absl::LocalTimeZone());
}

std::string JitProfiler::Snapshot::FormattedMetricsStr() const {
  std::string s;
  absl::StrAppendFormat(                      //
      &s, "  [P.%d:S.%d] TIMING   | %s |\n",  //
      profiler_id, id, timing.FormattedStr());
  if (delta_timing != SysTiming::Zero()) {
    absl::StrAppendFormat(                      //
        &s, "  [P.%d:S.%d] TIMING Δ | %s |\n",  //
        profiler_id, id, delta_timing.FormattedStr());
  }
  absl::StrAppendFormat(                      //
      &s, "  [P.%d:S.%d] MEMORY   | %s |\n",  //
      profiler_id, id, memory.FormattedStr());
  if (delta_memory != SysMemory::Zero()) {
    absl::StrAppendFormat(                      //
        &s, "  [P.%d:S.%d] MEMORY Δ | %s |\n",  //
        profiler_id, id, delta_memory.FormattedStr());
  }
  return s;
}

std::string JitProfiler::Snapshot::ShortMetricsStr() const {
  std::string s;
  absl::StrAppendFormat(  //
      &s, "TIMING { %s } ", timing.ShortStr());
  if (delta_timing != SysTiming::Zero()) {
    absl::StrAppendFormat(  //
        &s, "TIMING Δ { %s } ", delta_timing.ShortStr());
  }
  absl::StrAppendFormat(  //
      &s, "MEMORY { %s } ", memory.ShortStr());
  if (delta_memory != SysMemory::Zero()) {
    absl::StrAppendFormat(  //
        &s, "MEMORY Δ { %s } ", delta_memory.ShortStr());
  }
  return s;
}

const JitProfiler::Snapshot& JitProfiler::Snapshot::Log() const {
  if (id >= 0) {
    LOG(INFO).AtLocation(location.file, location.line)
        << "PROFILER [P." << profiler_id << (profiler_desc.empty() ? "" : " ")
        << profiler_desc << "] SNAPSHOT [S." << id << (title.empty() ? "" : " ")
        << title << "]:\n"
        << FormattedMetricsStr();
  }
  return *this;
}

std::ostream& operator<<(std::ostream& os, const JitProfiler::Snapshot& ss) {
  return os << ss.title << ": " << ss.ShortWhereStr() << " @ "
            << ss.ShortWhenStr() << ": " << ss.ShortMetricsStr();
}

//------------------------------------------------------------------------------
//                            TimelapseThread
//
// Starts a new thread that periodically takes a profiling snapshot using the
// passed parent profiler. The thread is auto-started at construction and
// terminated at destruction.
//------------------------------------------------------------------------------

class JitProfiler::TimelapseThread {
 public:
  TimelapseThread(              //
      JitProfiler* parent,      //
      SourceLocation loc,       //
      absl::Duration interval,  //
      bool also_log,            //
      std::string title)
      : parent_{parent},
        loc_{loc},
        interval_{interval},
        also_log_{also_log},
        title_{std::move(title)},
        stop_loop_{},
        loop_thread_{[this]() { RunLoop(); }} {}

  ~TimelapseThread() { StopLoop(); }

 private:
  void RunLoop() {
    while (!stop_loop_.HasBeenNotified()) {
      const auto& s = parent_->TakeSnapshot(loc_, title_);
      if (also_log_) s.Log();
      absl::SleepFor(interval_);
    }
  }

  void StopLoop() {
    stop_loop_.Notify();
    loop_thread_.join();
  }

  JitProfiler* parent_;
  const SourceLocation loc_;
  const absl::Duration interval_;
  const bool also_log_;
  const std::string title_;

  // NOTE: The order is important.
  absl::Notification stop_loop_;
  std::thread loop_thread_;
};

namespace {

//------------------------------------------------------------------------------
//                           ProfileReportGenerator
//
// A helper for JitProfiler::GenerateReport(): generates individual
// chronological charts of the tracked metrics and streams them to an ostream.
//------------------------------------------------------------------------------

class ProfileReportGenerator {
 public:
  ProfileReportGenerator(                                  //
      const std::deque<JitProfiler::Snapshot>& snapshots,  //
      JitProfiler::ReportSink* report_sink)
      : snapshots_{snapshots}, report_sink_{report_sink} {
    for (const auto& snapshot : snapshots_) {
      timing_low_ = SysTiming::LowWater(  //
          timing_low_, snapshot.timing);
      timing_high_ = SysTiming::HighWater(  //
          timing_high_, snapshot.timing);
      delta_timing_low_ = SysTiming::LowWater(  //
          delta_timing_low_, snapshot.delta_timing);
      delta_timing_high_ = SysTiming::HighWater(  //
          delta_timing_high_, snapshot.delta_timing);

      memory_low_ = SysMemory::LowWater(  //
          memory_low_, snapshot.memory);
      memory_high_ = SysMemory::HighWater(  //
          memory_high_, snapshot.memory);
      delta_memory_low_ = SysMemory::LowWater(  //
          delta_memory_low_, snapshot.delta_memory);
      delta_memory_high_ = SysMemory::HighWater(  //
          delta_memory_high_, snapshot.delta_memory);

      max_where_len_ =  //
          std::max<int>(max_where_len_, snapshot.ShortWhereStr().length());
      max_when_len_ =  //
          std::max<int>(max_when_len_, snapshot.ShortWhenStr().length());
      max_title_len_ =  //
          std::max<int>(max_title_len_, snapshot.title.length());
    }
  }

  // GenChartImpl() wrappers for the 2 available "snap" metrics.
  template <typename MetricT>
  void GenChart(                       //
      const std::string& chart_title,  //
      const MetricT SysTiming::*metric_field) {
    GenChartImpl(                                                   //
        chart_title, &JitProfiler::Snapshot::timing, metric_field,  //
        timing_low_, timing_high_, /*is_delta=*/false);
  }
  template <typename MetricT>
  void GenChart(                       //
      const std::string& chart_title,  //
      const MetricT SysMemory::*metric_field) const {
    GenChartImpl(                                                   //
        chart_title, &JitProfiler::Snapshot::memory, metric_field,  //
        memory_low_, memory_high_, /*is_delta=*/false);
  }

  // GenChartImpl() wrappers for the 2 available delta metrics.
  template <typename MetricT>
  void GenDeltaChart(                  //
      const std::string& chart_title,  //
      const MetricT SysTiming::*metric_field) {
    GenChartImpl(                                                         //
        chart_title, &JitProfiler::Snapshot::delta_timing, metric_field,  //
        delta_timing_low_, delta_timing_high_, /*is_delta=*/true);
  }
  template <typename MetricT>
  void GenDeltaChart(                  //
      const std::string& chart_title,  //
      const MetricT SysMemory::*metric_field) const {
    GenChartImpl(                                                         //
        chart_title, &JitProfiler::Snapshot::delta_memory, metric_field,  //
        delta_memory_low_, delta_memory_high_, /*is_delta=*/true);
  }

 private:
  // The actual chart generator. For better understanding of the code: an
  // example of `metric_field` is `&JitProfiler::Snapshot::delta_timing` which
  // has type `SysTiming`; an example of a matching `submetric_field` for that
  // is `&SysTiming::wall_time`.
  template <typename MetricT, typename SubmetricT>
  void GenChartImpl(                                       //
      const std::string& chart_title,                      //
      const MetricT JitProfiler::Snapshot::*metric_field,  //
      const SubmetricT MetricT::*submetric_field,          //
      MetricT metric_low_water,                            //
      MetricT metric_high_water,                           //
      bool is_delta) const {
    *report_sink_ << chart_title;

    constexpr SubmetricT kZero{};  // works for both ints and absl::Duration
    const SubmetricT low_water = metric_low_water.*submetric_field;
    const SubmetricT high_water = metric_high_water.*submetric_field;
    // SubmetricT can be int64 or Duration: calculate a notch_size that is a
    // double or an unrounded Duration, respectively, so the below calculations
    // are exact.
    const auto notch_size =
        (high_water - low_water) / static_cast<double>(kBarNotches);
    // The position of the notch indicating 0 (used for delta metrics only).
    // clang-format off
    const int notch_zero =
        notch_size == kZero ? kBarNotches :
        low_water >= kZero ? 0 :
        std::floor(std::abs(low_water / notch_size));
    // clang-format on
    CHECK_GE(kBarNotches, notch_zero);
    // Print a zero mark only if a delta metric goes negative.
    std::string zero_mark = low_water < kZero ? "|" : "";

    for (const auto& snapshot : snapshots_) {
      const SubmetricT current = snapshot.*metric_field.*submetric_field;

      // Generate a bar of #'s as a graphical representation of the current
      // value of the metric relative to its full range [low_water, high_water]:
      // low_water is no #'s and all -'s, high_water is kBarNotches of #'s.
      const std::string metric_str = FormatInOptimalUnits(current, is_delta);
      std::string metric_bar;
      // clang-format off
      const int notches =
          notch_size == kZero
              ? kBarNotches : std::floor((current - low_water) / notch_size);
      // clang-format on
      CHECK_GE(kBarNotches, notches);

      if (!is_delta) {
        // Non-delta metrics can't go negative, so the bar always looks like
        // this:
        // ###############--------------------------
        const std::string filled(notches, '#');
        const std::string unfilled(kBarNotches - notches, '-');
        metric_bar = absl::StrCat(filled, unfilled);
      } else {
        // Delta metrics can go negative, so this become more complicated. In
        // general, print a zero mark '|' at the proper fixed position of every
        // bar for this metric's history, and grow the #'s away from the zero
        // mark, to the left for negative and to the right for positive deltas:
        // +Delta: --------|#######---------
        // -Delta: ########|----------------
        std::string pad_minus, minus, plus, pad_plus;
        // Notches range from 0 (for low_water) to kBarNotches (for high_water).
        if (notches < notch_zero) {
          pad_minus = std::string(notches, '-');
          minus = std::string(notch_zero - notches, '#');
          pad_plus = std::string(kBarNotches - notch_zero, '-');
        } else if (notches > notch_zero) {
          pad_minus = std::string(notch_zero, '-');
          plus = std::string(notches - notch_zero, '#');
          pad_plus = std::string(kBarNotches - notches, '-');
        } else {
          pad_minus = std::string(notch_zero, '-');
          pad_plus = std::string(kBarNotches - notch_zero, '-');
        }
        metric_bar = absl::StrCat(pad_minus, minus, zero_mark, plus, pad_plus);
      }

      // Finally print a full line for the current snapshot/metric, like on of:
      // source.cc:123 @ 21:08:27.61 [P.1:S.1 Snap  ]  493.78M [############---]
      // source.cc:123 @ 21:08:27.61 [P.1:S.2 +Delta] +138.15M [-----|#####----]
      // source.cc:123 @ 21:08:27.61 [P.1:S.3 -Delta]  -82.69M [--###|---------]
      *report_sink_ << absl::StrFormat(                 //
          "  %*s @ %*s [P.%d:S.%-2d %*s] %10s [%s]\n",  // '*' is custom width
          -max_where_len_, snapshot.ShortWhereStr(),    // ...passed here.
          -max_when_len_, snapshot.ShortWhenStr(),      // '-' left-justifies
          snapshot.profiler_id, snapshot.id,            //
          -max_title_len_, snapshot.title,              //
          metric_str, metric_bar);
    }
  }

  static constexpr int kBarNotches = 50;

  const std::deque<JitProfiler::Snapshot>& snapshots_;
  JitProfiler::ReportSink* report_sink_;

  SysMemory memory_low_ = SysMemory::Max();
  SysMemory memory_high_ = SysMemory::Min();
  SysMemory delta_memory_low_ = SysMemory::Max();
  SysMemory delta_memory_high_ = SysMemory::Min();
  SysTiming timing_low_ = SysTiming::Max();
  SysTiming timing_high_ = SysTiming::Min();
  SysTiming delta_timing_low_ = SysTiming::Max();
  SysTiming delta_timing_high_ = SysTiming::Min();

  // NOTE: The values are negated, so have to be signed.
  int max_where_len_ = 0;
  int max_when_len_ = 0;
  int max_title_len_ = 0;
};

}  // namespace

//------------------------------------------------------------------------------
//                              JitProfiler
//------------------------------------------------------------------------------

std::atomic<int> JitProfiler::next_id_;

JitProfiler::JitProfiler(          //
    MetricsMask metrics,           //
    RaiiActionsMask raii_actions,  //
    SourceLocation location,       //
    std::string description)
    : metrics_{metrics},
      raii_actions_{raii_actions},
      ctor_loc_{location},
      description_{std::move(description)},
      id_{next_id_.fetch_add(1, std::memory_order_relaxed)} {
  if (metrics_ == kMetricsOff) return;

  if (raii_actions_ & kCtorSnapshot) {
    TakeSnapshot(ctor_loc_, "INITIAL").Log();
  }
}

JitProfiler::JitProfiler(               //
    MetricsMask metrics,                //
    absl::Duration timelapse_interval,  //
    bool also_log_timelapses,           //
    SourceLocation location,            //
    std::string description)
    : metrics_{metrics},
      raii_actions_{kDtorSnapshot | kDtorReport},
      ctor_loc_{location},
      description_{std::move(description)},
      id_{next_id_.fetch_add(1, std::memory_order_relaxed)} {
  if (metrics_ == kMetricsOff) return;

  StartTimelapse(  //
      ctor_loc_, timelapse_interval, also_log_timelapses, "Timelapse");
}

JitProfiler::~JitProfiler() {
  if (metrics_ == kMetricsOff) return;

  // In case the caller hasn't done this.
  if (timelapse_thread_) {
    StopTimelapse();
  }
  if (raii_actions_ & kDtorSnapshot) {
    // NOTE: Can't pass the real location from callers, so use next best thing.
    TakeSnapshot(ctor_loc_, "FINAL").Log();
  }
  // If requested, also print a final report.
  if (raii_actions_ & kDtorReport) {
    const std::string title =
        absl::StrFormat("PROFILER [P.%d %s] FINAL REPORT:", id_, description_);
    PrintReport(ctor_loc_, title);
  }
}

const JitProfiler::Snapshot& JitProfiler::TakeSnapshot(  //
    SourceLocation loc, std::string title) {
  if (metrics_ == kMetricsOff) {
    static const Snapshot kEmpty{};
    return kEmpty;
  }

  absl::WriterMutexLock lock{&mutex_};

  SysTiming snap_timing = SysTiming::Zero();
  SysTiming delta_timing = SysTiming::Zero();
  SysMemory snap_memory = SysMemory::Zero();
  SysMemory delta_memory = SysMemory::Zero();

  if (metrics_ & kTiming) {
    const auto current = SysTiming::Snapshot(timer_);
    if (metrics_ & kSnapTiming) {
      snap_timing = current;
    }
    if (metrics_ & kDeltaTiming && !snapshots_.empty()) {
      const auto& previous = snapshots_.back().timing;
      delta_timing = current - previous;
    }
  }

  if (metrics_ & kMemory) {
    const auto current = SysMemory::Snapshot();
    if (metrics_ & kSnapMemory) {
      snap_memory = current;
    }
    if (metrics_ & kDeltaMemory && !snapshots_.empty()) {
      const auto& previous = snapshots_.back().memory;
      delta_memory = current - previous;
    }
  }

  Snapshot snapshot{
      .id = static_cast<int64_t>(snapshots_.size()),
      .title = std::move(title),
      .location = loc,
      .time = absl::Now(),
      .profiler_id = id_,
      .profiler_desc = description_,
      .timing = snap_timing,
      .delta_timing = delta_timing,
      .memory = snap_memory,
      .delta_memory = delta_memory,
  };

  return snapshots_.emplace_back(std::move(snapshot));
}

void JitProfiler::StartTimelapse(  //
    SourceLocation loc,            //
    absl::Duration interval,       //
    bool also_log,                 //
    std::string title) {
  absl::WriterMutexLock lock{&mutex_};
  CHECK(!timelapse_thread_) << "StopTimelapse() wasn't called";
  timelapse_thread_ = std::make_unique<TimelapseThread>(
      this, loc, interval, also_log, std::move(title));
}

void JitProfiler::StopTimelapse() {
  absl::WriterMutexLock lock{&mutex_};
  CHECK(timelapse_thread_) << "StartTimelapse() wasn't called";
  timelapse_thread_.reset();  // ~TimelapseThread() runs
}

void JitProfiler::PrintReport(  //
    SourceLocation loc, const std::string& title) {
  if (metrics_ == kMetricsOff) return;

  // Logs streamed-in text to LOG(INFO), while dropping the usual log prefix
  // (date/time/thread/source). LOG()'s limit on the size of a single message
  // applies to one streamed text fragment only (if needed, this can be reduced
  // even further to a single line of text in a fragment): this is the main
  // purpose of this class, as profiling reports can get very long. especially
  // with automatic timelapse snapshotting.
  class ReportLogger final : public ReportSink {
   public:
    ~ReportLogger() override {
      if (!buffer_.empty()) {
        LOG(INFO).NoPrefix() << buffer_;
      }
    }

    void operator<<(const std::string& fragment) override {
      const auto last_newline = fragment.rfind('\n');
      if (last_newline == std::string::npos) {
        // Accumulate no-'\n' fragments: LOG() always wraps around.
        buffer_ += fragment;
      } else {
        // Now we can log, but save the last bit of text
        LOG(INFO).NoPrefix() << buffer_ << fragment.substr(0, last_newline);
        buffer_ = fragment.substr(last_newline + 1);
      }
    }

   private:
    std::string buffer_;
  };

  LOG(INFO).AtLocation(loc.file, loc.line) << title << "\n";
  ReportLogger report_logger;
  GenerateReport(&report_logger);
}

void JitProfiler::GenerateReport(ReportSink* report_sink) const {
  absl::ReaderMutexLock lock{&mutex_};
  // Prevent interleaved reports from multiple concurrent JitProfilers.
  ABSL_CONST_INIT static absl::Mutex report_generation_mutex_{absl::kConstInit};
  absl::WriterMutexLock logging_lock{&report_generation_mutex_};

  ProfileReportGenerator gen{snapshots_, report_sink};

  if (metrics_ & kSnapTiming) {
    *report_sink << absl::StrFormat(  //
        "\n=== TIMING [P.%d %s] ===\n", id_, description_);
    gen.GenChart("\nWALL TIME:\n", &SysTiming::wall_time);
    gen.GenChart("\nUSER TIME:\n", &SysTiming::user_time);
    gen.GenChart("\nSYSTEM TIME:\n", &SysTiming::sys_time);
    gen.GenChart("\nCPU UTILIZATION:\n", &SysTiming::cpu_utilization);
    gen.GenChart("\nAVERAGE CORES:\n", &SysTiming::cpu_hyper_cores);
  }
  if (metrics_ & kDeltaTiming) {
    *report_sink << absl::StrFormat(  //
        "\n=== Δ TIMING [P.%d %s] ===\n", id_, description_);
    gen.GenDeltaChart("\nΔ WALL TIME:\n", &SysTiming::wall_time);
    gen.GenDeltaChart("\nΔ USER TIME:\n", &SysTiming::user_time);
    gen.GenDeltaChart("\nΔ SYSTEM TIME:\n", &SysTiming::sys_time);
    gen.GenDeltaChart("\nΔ CPU UTILIZATION:\n", &SysTiming::cpu_utilization);
    gen.GenDeltaChart("\nΔ AVERAGE CORES:\n", &SysTiming::cpu_hyper_cores);
  }
  if (metrics_ & kSnapMemory) {
    *report_sink << absl::StrFormat(  //
        "\n=== MEMORY USAGE [P.%d %s] ===\n", id_, description_);
    gen.GenChart("\nRESIDENT SET SIZE:\n", &SysMemory::mem_rss);
    gen.GenChart("\nVIRTUAL SIZE:\n", &SysMemory::mem_vsize);
    gen.GenChart("\nVIRTUAL PEAK:\n", &SysMemory::mem_vpeak);
    gen.GenChart("\nDATA SEGMENT:\n", &SysMemory::mem_data);
    gen.GenChart("\nSHARED MEMORY:\n", &SysMemory::mem_shared);
  }
  if (metrics_ & kDeltaMemory) {
    *report_sink << absl::StrFormat(  //
        "\n=== Δ MEMORY USAGE [P.%d %s] ===\n", id_, description_);
    gen.GenDeltaChart("\nΔ RESIDENT SET SIZE:\n", &SysMemory::mem_rss);
    gen.GenDeltaChart("\nΔ VIRTUAL SIZE:\n", &SysMemory::mem_vsize);
    gen.GenDeltaChart("\nΔ VIRTUAL PEAK:\n", &SysMemory::mem_vpeak);
    gen.GenDeltaChart("\nΔ DATA SEGMENT:\n", &SysMemory::mem_data);
    gen.GenDeltaChart("\nΔ SHARED MEMORY:\n", &SysMemory::mem_shared);
  }
}

}  // namespace centipede::perf
