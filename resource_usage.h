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

// TODO(ussuri): Upgrade to optionally measure the metrics of a given thread,
//  not the entire process (available via /proc/self/tasks/<tid>/<file>).

// Utility classes to capture and log system resource usage of the current
// process.

#ifndef THIRD_PARTY_CENTIPEDE_RESOURCE_USAGE_H_
#define THIRD_PARTY_CENTIPEDE_RESOURCE_USAGE_H_

#include <sys/resource.h>

#include <cstdint>
#include <iosfwd>
#include <ostream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"

namespace centipede::perf {

// Memory size in bytes.
using MemSize = int64_t;
// How many CPU hyperthreaded cores the process has been using on average.
// 1 corresponds to 1 hypercore. The max is the number of hyperthreaded cores
// on the system.
using CpuHyperCores = double;
// What percentage of the allotted system scheduling time the process has
// actually utilized for CPU processing, as opposed to idling (e.g. waiting for
// I/O etc.). The theoretical max is 1.0, which corresponds to 100% utilization,
// however the value can go slightly higher due to rounding errors in the system
// scheduler's accounting logic.
using CpuUtilization = long double;

//------------------------------------------------------------------------------
//                               ProcessTimer
//
// Measures the system, user, and wall times of the process. Can be a global
// variable because the implementation depends on nothing but syscalls.
// The parameterless SysTiming::Snapshot() uses the default global timer that
// starts with the process; clients also have an option to define and pass a
// custom timer to count from some other point in time.
//------------------------------------------------------------------------------

class ProcessTimer {
 public:
  ProcessTimer();
  void Get(double& user, double& sys, double& wall) const;

 private:
  absl::Time start_time_;
  struct rusage start_rusage_;
};

//------------------------------------------------------------------------------
//                                SysTiming
//
// An interfaces to measure, store, and log the system time usage of the current
// process.
//------------------------------------------------------------------------------

struct SysTiming {
  //----------------------------------------------------------------------------
  //             Static factory ctors and friend operators

  static SysTiming Zero();
  static SysTiming Min();
  static SysTiming Max();

  // Returns system time usage since this process started.
  static SysTiming Snapshot();
  // Same as above, but using a custom timer. The caller is responsible for
  // setting up and passing the same timer object to all Snapshot() calls to get
  // consistent results.
  static SysTiming Snapshot(const ProcessTimer& timer);

  // Comparisons. NOTE: `is_delta` is always ignored.
  friend bool operator==(const SysTiming& t1, const SysTiming& t2);
  friend bool operator!=(const SysTiming& t1, const SysTiming& t2);
  friend bool operator<(const SysTiming& t1, const SysTiming& t2);
  friend bool operator<=(const SysTiming& t1, const SysTiming& t2);
  friend bool operator>(const SysTiming& t1, const SysTiming& t2);
  friend bool operator>=(const SysTiming& t1, const SysTiming& t2);

  // Returns the low-water resource usage between the two args.
  static SysTiming LowWater(const SysTiming& t1, const SysTiming& t2);
  // Returns the high-water value between the two args.
  static SysTiming HighWater(const SysTiming& t1, const SysTiming& t2);

  // Returns the value with `is_delta` set to true. Useful for signed logging.
  friend SysTiming operator+(const SysTiming& t);
  // Returns the negated value with `is_delta` set to true.
  friend SysTiming operator-(const SysTiming& t);
  // Returns the signed delta between two stats, with `is_delta` set to true.
  friend SysTiming operator-(const SysTiming& t1, const SysTiming& t2);
  // Returns the sum of two stats, with `is_delta` set to true iff `t1` or `t2`
  // or both are deltas.
  friend SysTiming operator+(const SysTiming& t1, const SysTiming& t2);
  // Returns a SysTiming where every field is divided by `div`. `is_delta` is
  // carried over from `t`.
  friend SysTiming operator/(const SysTiming& t, int64_t div);

  // Streams `t.ShortStr()`.
  friend std::ostream& operator<<(std::ostream& os, const SysTiming& t);

  //----------------------------------------------------------------------------
  //                           Non-static methods

  // Returns the metrics as short string. If `is_delta` is true, positive values
  // will be prefixed with a '+'.
  std::string ShortStr() const;
  // Returns a formatted representation of the metrics. The format is fixed, so
  // if multiple objects get printed with newline separators, they will form a
  // table. If `is_delta` is true, positive values will be prefixed with a '+'.
  std::string FormattedStr() const;

  //----------------------------------------------------------------------------
  //                              Public data

  absl::Duration wall_time = absl::ZeroDuration();
  absl::Duration user_time = absl::ZeroDuration();
  absl::Duration sys_time = absl::ZeroDuration();
  CpuUtilization cpu_utilization = 0.;
  CpuHyperCores cpu_hyper_cores = 0.;
  // If true, positive values will be printed with a '+'.
  bool is_delta = false;
};

//------------------------------------------------------------------------------
//                               SysMemory
//
// An interface to measure, store, manipulate, and log the system resource usage
// of the current process.
//------------------------------------------------------------------------------

struct SysMemory {
  //----------------------------------------------------------------------------
  //              Static factory ctors and friend operators

  static SysMemory Zero();
  static SysMemory Min();
  static SysMemory Max();

  // Returns the current process's resource usage.
  static SysMemory Snapshot();

  // Comparisons. NOTE: `is_delta` is always ignored.
  friend bool operator==(const SysMemory& m1, const SysMemory& m2);
  friend bool operator!=(const SysMemory& m1, const SysMemory& m2);
  friend bool operator<(const SysMemory& m1, const SysMemory& m2);
  friend bool operator<=(const SysMemory& m1, const SysMemory& m2);
  friend bool operator>(const SysMemory& m1, const SysMemory& m2);
  friend bool operator>=(const SysMemory& m1, const SysMemory& m2);

  // Returns the low-water value between the two args.
  static SysMemory LowWater(const SysMemory& m1, const SysMemory& m2);
  // Returns the high-water value usage between the two args.
  static SysMemory HighWater(const SysMemory& m1, const SysMemory& m2);

  // Returns the value with `is_delta` set to true. Useful for signed logging.
  friend SysMemory operator+(const SysMemory& m);
  // Returns the negated value with `is_delta` set to true.
  friend SysMemory operator-(const SysMemory& m);
  // Returns the signed delta of two stats, with `is_delta` set to true.
  friend SysMemory operator-(const SysMemory& m1, const SysMemory& m2);
  // Returns the sum of two stats, with `is_delta` set to true iff `m1` or `m2`
  // or both are deltas.
  friend SysMemory operator+(const SysMemory& m1, const SysMemory& m2);
  // Returns a value with every metric divided by `div`. `is_delta` is
  // carried over from `m`.
  friend SysMemory operator/(const SysMemory& m, int64_t div);

  // Streams `m.ShortStr()`.
  friend std::ostream& operator<<(std::ostream& os, const SysMemory& m);

  //----------------------------------------------------------------------------
  //                           Non-static methods

  // Returns the metrics as short string. If `is_delta` is true, positive values
  // will be prefixed with a '+'.
  std::string ShortStr() const;
  // Returns a formatted representation of the metrics. The format is fixed, so
  // if multiple objects get printed with newline separators, they will form a
  // table. If `is_delta` is true, positive values will be prefixed with a '+'.
  std::string FormattedStr() const;

  //----------------------------------------------------------------------------
  //                                   Data

  // Memory sizes are all in bytes. For the meaning of these, cf. `man proc` or
  // https://man7.org/linux/man-pages/man5/proc.5.html, sections
  // /proc/[pid]/{stat,statm,status}.
  MemSize mem_vsize = 0;
  MemSize mem_vpeak = 0;
  MemSize mem_rss = 0;
  MemSize mem_data = 0;
  MemSize mem_shared = 0;
  // If true, positive values will be printed with a '+'.
  bool is_delta = false;
};

//------------------------------------------------------------------------------
//                       Pretty-printing of the stats
//------------------------------------------------------------------------------

// Formats `duration` as the most compact human-readable string. Differences
// from absl::FormatDuration():
// - Durations up to 1s are rounded up to whole numbers of ns/us/ms.
// - Durations longer than 1s are rounded up to 2 decimals and are never
//   converted to hours/minutes/seconds.
// - Positive durations can be prefixed with a '+' (useful to indicate that the
//   value is a positive delta).
std::string FormatInOptimalUnits(absl::Duration duration, bool always_signed);
// Formats `bytes` as the most compact human-readable string in SI units.
// `always_signed` prints '+' before positive numbers (useful to indicate
// positive deltas).
std::string FormatInOptimalUnits(MemSize bytes, bool always_signed);
// Formats CPU utilization as a percentage.
// `always_signed` prints '+' before positive numbers (useful to indicate
// positive deltas).
std::string FormatInOptimalUnits(CpuUtilization util, bool always_signed);
// Formats CPU hypercores with decimal precision.
// `always_signed` prints '+' before positive numbers (useful to indicate
// positive deltas).
std::string FormatInOptimalUnits(CpuHyperCores cores, bool always_signed);

}  // namespace centipede::perf

#endif  // THIRD_PARTY_CENTIPEDE_RESOURCE_USAGE_H_
