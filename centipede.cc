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

// Centipede: an experimental distributed fuzzing engine.
// Very simple / naive so far.
// Main use case: large out-of-process fuzz targets with relatively slow
// execution (< 100 exec/s).
//
// Basic approach (subject to change):
// * All state is stored in a local or remote directory `workdir`.
// * State consists of a corpus (inputs) and feature sets (see feature_t).
// * Feature sets are associated with a binary, so that two binaries
//   have independent feature sets stored in different subdirs in `workdir`,
//   like binaryA-sha1-of-A and binaryB-sha1-of-B.
//   If the binary is recompiled at different revision or with different
//   compiler options, it is a different binary and feature sets will need to be
//   recomputed for the new binary in its separate dir.
// * The corpus is not tied to the binary. It is stored in `workdir`/.
// * The fuzzer runs in `total_shards` independent processes.
// * Each shard appends data to its own files in `workdir`: corpus and features;
//   no other process writes to those files.
// * Each shard may periodically read some other shard's corpus and features.
//   Since all files are append-only (no renames, no deletions) we may only
//   have partial reads, and the algorithm is expected to tolerate those.
// * Fuzzing can be run locally in multiple processes, with a local `workdir`
//   or on a cluster, which supports `workdir` on a remote file system.
// * The intent is to scale to an arbitrary number of shards,
//   currently tested with total_shards = 10000.
//
//  Differential fuzzing is not yet properly implemented.
//  Currently one can run target A in a given workdir, then target B, and so on,
//  and the corpus will grow over time benefiting from all targets.
#include "./centipede.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "./blob_file.h"
#include "./command.h"
#include "./coverage.h"
#include "./defs.h"
#include "./environment.h"
#include "./execution_result.h"
#include "./feature.h"
#include "./logging.h"
#include "./remote_file.h"
#include "./util.h"

namespace centipede {

// Reads corpus records (corpus and features, from different blob files),
// returns vector of CorpusRecord objects.
static std::vector<CorpusRecord> ReadCorpusRecords(const Environment &env,
                                                   size_t shard_index) {
  std::vector<CorpusRecord> result;
  std::unique_ptr<BlobFileReader> corpus_reader =
      DefaultBlobFileReaderFactory();
  std::unique_ptr<BlobFileReader> features_reader =
      DefaultBlobFileReaderFactory();
  // When opening files for reading, we ignore errors, because these files may
  // not exist.
  corpus_reader->Open(env.MakeCorpusPath(shard_index)).IgnoreError();
  features_reader->Open(env.MakeFeaturesPath(shard_index)).IgnoreError();

  absl::Span<uint8_t> blob;
  std::vector<ByteArray> corpus_blobs, feature_blobs;
  while (corpus_reader->Read(blob).ok()) {
    corpus_blobs.emplace_back().assign(blob.begin(), blob.end());
  }
  while (features_reader->Read(blob).ok()) {
    feature_blobs.emplace_back().assign(blob.begin(), blob.end());
  }

  ExtractCorpusRecords(corpus_blobs, feature_blobs, result);
  return result;
}

Centipede::Centipede(const Environment &env, CentipedeCallbacks &user_callbacks,
                     const Coverage::PCTable &pc_table,
                     const SymbolTable &symbols,
                     CoverageLogger &coverage_logger)
    : env_(env),
      user_callbacks_(user_callbacks),
      rng_(env_.seed),
      // TODO(kcc): [impl] find a better way to compute frequency_threshold.
      fs_(100 /*arbitrary frequency_threshold*/),
      pc_table_(pc_table),
      symbols_(symbols),
      function_filter_(env_.function_filter, symbols_),
      coverage_logger_(coverage_logger),
      input_filter_path_(std::filesystem::path(TemporaryLocalDirPath())
                             .append("filter-input")),
      input_filter_cmd_(env_.input_filter, {input_filter_path_}, {/*env*/},
                        "/dev/null", "/dev/null") {
  CHECK(env_.seed) << "env_.seed must not be zero";
  if (!env_.input_filter.empty() && env_.fork_server) {
    input_filter_cmd_.StartForkServer(TemporaryLocalDirPath(), "input_filter",
                                      env_.GetForkServerHelperPath());
  }
}

int Centipede::SaveCorpusToLocalDir(const Environment &env,
                                    std::string_view save_corpus_to_local_dir) {
  for (size_t shard = 0; shard < env.total_shards; shard++) {
    auto reader = DefaultBlobFileReaderFactory();
    reader->Open(env.MakeCorpusPath(shard)).IgnoreError();  // may not exist.
    absl::Span<uint8_t> blob;
    size_t num_read = 0;
    while (reader->Read(blob).ok()) {
      ++num_read;
      WriteToLocalHashedFileInDir(save_corpus_to_local_dir, blob);
    }
    LOG(INFO) << "read " << num_read << " from " << env.MakeCorpusPath(shard);
  }
  return 0;
}

int Centipede::ExportCorpusFromLocalDir(const Environment &env,
                                        std::string_view local_dir) {
  // Shard the file paths in `local_dir` based on hashes of filenames.
  // Such partition is stable: a given file always goes to a specific shard.
  std::vector<std::vector<std::string>> sharded_paths(env.total_shards);
  size_t total_paths = 0;
  for (const auto &entry :
       std::filesystem::recursive_directory_iterator(local_dir)) {
    if (entry.is_regular_file()) {
      size_t filename_hash = std::hash<std::string>{}(entry.path().filename());
      sharded_paths[filename_hash % env.total_shards].push_back(entry.path());
      ++total_paths;
    }
  }
  // Iterate over all shards.
  size_t inputs_added = 0;
  size_t inputs_ignored = 0;
  for (size_t shard = 0; shard < env.total_shards; shard++) {
    size_t num_shard_bytes = 0;
    // Read the shard (if it exists), collect input hashes from it.
    absl::flat_hash_set<std::string> existing_hashes;
    {
      auto reader = DefaultBlobFileReaderFactory();
      // May fail to open if file doesn't exist.
      reader->Open(env.MakeCorpusPath(shard)).IgnoreError();
      absl::Span<uint8_t> blob;
      while (reader->Read(blob).ok()) {
        existing_hashes.insert(Hash(blob));
      }
    }
    // Add inputs to the current shard, if the shard doesn't have them already.
    auto appender = DefaultBlobFileAppenderFactory();
    CHECK_OK(appender->Open(env.MakeCorpusPath(shard)));
    ByteArray shard_data;
    for (const auto &path : sharded_paths[shard]) {
      ByteArray input;
      ReadFromLocalFile(path, input);
      if (input.empty() || existing_hashes.contains(Hash(input))) {
        ++inputs_ignored;
        continue;
      }
      CHECK_OK(appender->Append(input));
      ++inputs_added;
    }
    LOG(INFO) << VV(shard) << VV(inputs_added) << VV(inputs_ignored)
              << VV(num_shard_bytes) << VV(shard_data.size());
  }
  CHECK_EQ(total_paths, inputs_added + inputs_ignored);
  return 0;
}

void Centipede::Log(std::string_view log_type, size_t min_log_level) {
  if (env_.log_level < min_log_level) {
    return;
  }
  const size_t seconds_since_beginning = timer_.seconds_since_beginning();
  const double exec_speed =
      seconds_since_beginning
          ? static_cast<double>(num_runs_) / seconds_since_beginning
          : 0;
  auto [max, avg] = corpus_.MaxAndAvgSize();
  LOG(INFO) << "[" << num_runs_ << "]"
            << " " << log_type << ":"
            << " ft: " << fs_.size() << " cov: " << fs_.ToCoveragePCs().size()
            << " cnt: " << fs_.CountFeatures(FeatureDomains::k8bitCounters)
            << " df: " << fs_.CountFeatures(FeatureDomains::kDataFlow)
            << " cmp: " << fs_.CountFeatures(FeatureDomains::kCMP)
            << " path: " << fs_.CountFeatures(FeatureDomains::kBoundedPath)
            << " pair: " << fs_.CountFeatures(FeatureDomains::kPCPair)
            << " corp: " << corpus_.NumActive() << "/" << corpus_.NumTotal()
            << " max/avg " << max << " " << avg << " "
            << corpus_.MemoryUsageString() << " exec/s: " << exec_speed
            << " mb: " << (MemoryUsage() >> 20);
}

void Centipede::LogFeaturesAsSymbols(const FeatureVec &fv) {
  auto feature_domain = FeatureDomains::k8bitCounters;
  for (auto feature : fv) {
    if (!feature_domain.Contains(feature)) continue;
    Coverage::PCIndex pc_index = Convert8bitCounterFeatureToPcIndex(feature);
    auto description = coverage_logger_.ObserveAndDescribeIfNew(pc_index);
    if (description.empty()) continue;
    VLOG(coverage_logger_verbose_level_) << description;
  }
}

bool Centipede::InputPassesFilter(const ByteArray &input) {
  if (env_.input_filter.empty()) return true;
  WriteToLocalFile(input_filter_path_, input);
  bool result = input_filter_cmd_.Execute() == EXIT_SUCCESS;
  std::filesystem::remove(input_filter_path_);
  return result;
}

bool Centipede::ExecuteAndReportCrash(std::string_view binary,
                                      const std::vector<ByteArray> &input_vec,
                                      BatchResult &batch_result) {
  bool success = user_callbacks_.Execute(binary, input_vec, batch_result);
  if (!success) ReportCrash(binary, input_vec, batch_result);
  return success;
}

// *** Highly experimental and risky. May not scale well for large targets. ***
//
// The idea: an unordered pair of two features {a, b} is by itself a feature.
// In the worst case, the number of such synthetic features is a square of
// the number of regular features, which may not scale.
// For now, we only treat pairs of PCs as features, which is still quadratic
// by the number of PCs. But in moderate-sized programs this may be tolerable.
//
// Rationale: if two different parts of the target are exercised simultaneously,
// this may create interesting behaviour that is hard to capture with regular
// control flow (or other) features.
size_t Centipede::AddPcPairFeatures(FeatureVec &fv) {
  // Using a scratch vector to avoid allocations.
  auto &pcs = add_pc_pair_scratch_;
  pcs.clear();

  size_t num_pcs = pc_table_.size();
  size_t num_added_pairs = 0;

  // Collect PCs from fv.
  for (auto feature : fv) {
    if (FeatureDomains::k8bitCounters.Contains(feature))
      pcs.push_back(Convert8bitCounterFeatureToPcIndex(feature));
  }

  // The quadratic loop: iterate all PC pairs (!!).
  for (size_t i = 0, n = pcs.size(); i < n; ++i) {
    size_t pc1 = pcs[i];
    for (size_t j = i + 1; j < n; ++j) {
      size_t pc2 = pcs[j];
      feature_t f = FeatureDomains::kPCPair.ConvertToMe(
          ConvertPcPairToNumber(pc1, pc2, num_pcs));
      // If we have seen this pair at least once, ignore it.
      if (fs_.Frequency(f)) continue;
      fv.push_back(f);
      ++num_added_pairs;
    }
  }
  return num_added_pairs;
}

bool Centipede::RunBatch(const std::vector<ByteArray> &input_vec,
                         BatchResult &batch_result,
                         BlobFileAppender *corpus_file,
                         BlobFileAppender *features_file,
                         BlobFileAppender *unconditional_features_file) {
  bool success = ExecuteAndReportCrash(env_.binary, input_vec, batch_result);
  CHECK_EQ(input_vec.size(), batch_result.results().size());

  for (const auto &extra_binary : env_.extra_binaries) {
    BatchResult extra_batch_result;
    success =
        ExecuteAndReportCrash(extra_binary, input_vec, extra_batch_result) &&
        success;
  }
  if (!success && env_.exit_on_crash) {
    LOG(INFO) << "exit_on_crash is enabled; exiting soon";
    RequestEarlyExit(1);
    return false;
  }
  CHECK_EQ(batch_result.results().size(), input_vec.size());
  num_runs_ += input_vec.size();
  bool batch_gained_new_coverage = false;
  for (size_t i = 0; i < input_vec.size(); i++) {
    FeatureVec &fv = batch_result.results()[i].mutable_features();
    bool function_filter_passed = function_filter_.filter(fv);
    bool input_gained_new_coverage =
        fs_.CountUnseenAndPruneFrequentFeatures(fv);
    if (env_.use_pcpair_features && AddPcPairFeatures(fv))
      input_gained_new_coverage = true;
    if (unconditional_features_file) {
      CHECK_OK(unconditional_features_file->Append(
          PackFeaturesAndHash(input_vec[i], fv)));
    }
    if (input_gained_new_coverage) {
      // TODO(kcc): [impl] add stats for filtered-out inputs.
      if (!InputPassesFilter(input_vec[i])) continue;
      fs_.IncrementFrequencies(fv);
      LogFeaturesAsSymbols(fv);
      batch_gained_new_coverage = true;
      CHECK_GT(fv.size(), 0UL);
      if (function_filter_passed) {
        corpus_.Add(input_vec[i], fv, fs_);
      }
      if (env_.prune_frequency != 0 &&
          (corpus_.NumTotal() % env_.prune_frequency) == 0) {
        corpus_.Prune(fs_);
      }
      if (corpus_file) {
        CHECK_OK(corpus_file->Append(input_vec[i]));
      }
      if (!env_.corpus_dir.empty()) {
        WriteToLocalHashedFileInDir(env_.corpus_dir[0], input_vec[i]);
      }
      if (features_file) {
        CHECK_OK(features_file->Append(PackFeaturesAndHash(input_vec[i], fv)));
      }
    }
  }
  return batch_gained_new_coverage;
}

// TODO(kcc): [impl] don't reread the same corpus twice.
void Centipede::LoadShard(const Environment &load_env, size_t shard_index,
                          bool rerun) {
  std::vector<CorpusRecord> records = ReadCorpusRecords(load_env, shard_index);
  size_t exported_with_features = 0;
  size_t exported_without_features = 0;
  size_t added_to_corpus = 0;
  std::vector<ByteArray> to_rerun;
  for (auto &cr : records) {
    if (cr.features.empty()) {
      exported_without_features++;
      if (rerun) {
        to_rerun.push_back(cr.data);
      }
      // TODO(kcc): [impl] distinguish inputs that have zero infrequent features
      // from inputs for which we don't know their features.
    } else {
      exported_with_features++;
      if (cr.features.empty()) continue;
      LogFeaturesAsSymbols(cr.features);
      if (fs_.CountUnseenAndPruneFrequentFeatures(cr.features)) {
        fs_.IncrementFrequencies(cr.features);
        corpus_.Add(cr.data, cr.features, fs_);
        added_to_corpus++;
      }
    }
  }
  // We don't prune the corpus while loading shards,
  // as it will interfere with distillation.
  // LOG(INFO) << "LoadShard:"
  //           << " shard: " << shard_index << " total: " << records.size()
  //           << " w/features: " << exported_with_features
  //           << " added: " << added_to_corpus;
  if (added_to_corpus) Log("load-shard", 1);

  if (to_rerun.empty()) return;
  auto features_file = DefaultBlobFileAppenderFactory();
  CHECK_OK(features_file->Open(env_.MakeFeaturesPath(shard_index)));

  LOG(INFO) << to_rerun.size() << " inputs to rerun";
  // Re-run all inputs for which we don't know their features.
  // Run in batches of at most env_.batch_size inputs each.
  while (!to_rerun.empty()) {
    size_t batch_size = std::min(to_rerun.size(), env_.batch_size);
    std::vector<ByteArray> batch(to_rerun.end() - batch_size, to_rerun.end());
    BatchResult batch_result;
    to_rerun.resize(to_rerun.size() - batch_size);
    if (RunBatch(batch, batch_result, nullptr, nullptr, features_file.get())) {
      Log("rerun-old", 1);
    }
  }
}

void Centipede::GenerateCoverageReport() {
  if (pc_table_.empty()) return;
  if (!env_.GeneratingCoverageReportInThisShard()) return;
  auto pci_vec = fs_.ToCoveragePCs();
  Coverage coverage(pc_table_, pci_vec);
  std::stringstream out;
  coverage.Print(symbols_, out);
  // Repackage the output as ByteArray for RemoteFileAppend's consumption.
  // TODO(kcc): [impl] may want to introduce RemoteFileAppend(f, std::string).
  std::string str = out.str();
  ByteArray bytes(str.begin(), str.end());
  auto report_path = env_.MakeCoverageReportPath();
  LOG(INFO) << "GenerateCoverageReport: " << report_path;
  auto f = RemoteFileOpen(report_path, "w");
  CHECK(f);
  RemoteFileAppend(f, bytes);
  RemoteFileClose(f);
}

void Centipede::GenerateCorpusStats() {
  if (!env_.GeneratingCorpusStatsInThisShard()) return;
  std::ostringstream os;
  corpus_.PrintStats(os, fs_);
  std::string str = os.str();
  ByteArray bytes(str.begin(), str.end());
  auto stats_path = env_.MakeCorpusStatsPath();
  LOG(INFO) << "GenerateCorpusStats: " << stats_path;
  auto *f = RemoteFileOpen(stats_path, "w");
  CHECK(f);
  RemoteFileAppend(f, bytes);
  RemoteFileClose(f);
}

void Centipede::MergeFromOtherCorpus(std::string_view merge_from_dir,
                                     size_t shard_index_to_merge) {
  LOG(INFO) << __func__ << ": " << merge_from_dir;
  Environment merge_from_env = env_;
  merge_from_env.workdir = merge_from_dir;
  size_t initial_corpus_size = corpus_.NumActive();
  LoadShard(merge_from_env, shard_index_to_merge, /*rerun=*/true);
  size_t new_corpus_size = corpus_.NumActive();
  CHECK_GE(new_corpus_size, initial_corpus_size);  // Corpus can't shrink here.
  if (new_corpus_size > initial_corpus_size) {
    auto appender = DefaultBlobFileAppenderFactory();
    CHECK_OK(appender->Open(env_.MakeCorpusPath(env_.my_shard_index)));
    for (size_t idx = initial_corpus_size; idx < new_corpus_size; ++idx) {
      CHECK_OK(appender->Append(corpus_.Get(idx)));
    }
    LOG(INFO) << "merge: " << (new_corpus_size - initial_corpus_size)
              << " new inputs added";
  }
}

void Centipede::FuzzingLoop() {
  LOG(INFO) << "shard: " << env_.my_shard_index << "/" << env_.total_shards
            << " " << TemporaryLocalDirPath() << " "
            << "seed: " << env_.seed << "\n\n\n";

  {
    // Execute a dummy input.
    BatchResult batch_result;
    user_callbacks_.Execute(env_.binary, {user_callbacks_.DummyValidInput()},
                            batch_result);
  }

  Log("begin-fuzz", 0);

  if (env_.full_sync || env_.DistillingInThisShard()) {
    // Load all shards in random order.
    std::vector<size_t> shards(env_.total_shards);
    std::iota(shards.begin(), shards.end(), 0);
    std::shuffle(shards.begin(), shards.end(), rng_);
    size_t num_shards_loaded = 0;
    for (auto shard : shards) {
      LoadShard(env_, shard, /*rerun=*/shard == env_.my_shard_index);
      if ((++num_shards_loaded % 100) == 0) {  // Log every 100 shards.
        LOG(INFO) << "num_shards_loaded: " << num_shards_loaded;
      }
    }
  } else {
    // Only load my shard.
    LoadShard(env_, env_.my_shard_index, /*rerun=*/true);
  }

  if (!env_.merge_from.empty()) {
    // Merge a shard with the same index from another corpus.
    MergeFromOtherCorpus(env_.merge_from, env_.my_shard_index);
  }

  auto corpus_file = DefaultBlobFileAppenderFactory();
  auto features_file = DefaultBlobFileAppenderFactory();
  CHECK_OK(corpus_file->Open(env_.MakeCorpusPath(env_.my_shard_index)));
  CHECK_OK(features_file->Open(env_.MakeFeaturesPath(env_.my_shard_index)));

  if (corpus_.NumTotal() == 0)
    corpus_.Add(user_callbacks_.DummyValidInput(), {}, fs_);

  Log("init-done:", 0);
  // Clear timer_ and num_runs_, so that the pre-init work doesn't affect them.
  timer_ = Timer();
  num_runs_ = 0;
  coverage_logger_verbose_level_ = 1;  // log coverage with --v=1.

  if (env_.DistillingInThisShard()) {
    auto distill_to_path = env_.MakeDistilledPath();
    auto appender = DefaultBlobFileAppenderFactory();
    CHECK_OK(appender->Open(distill_to_path));
    for (size_t i = 0; i < corpus_.NumActive(); i++) {
      CHECK_OK(appender->Append(corpus_.Get(i)));
      if (!env_.corpus_dir.empty()) {
        WriteToLocalHashedFileInDir(env_.corpus_dir[0], corpus_.Get(i));
      }
    }
    LOG(INFO) << "distill_to_path: " << distill_to_path
              << " distilled_size: " << corpus_.NumActive();
  }

  GenerateCoverageReport();

  // num_runs / batch_size, rounded up.
  size_t number_of_batches =
      (env_.num_runs + env_.batch_size - 1) / env_.batch_size;
  size_t new_runs = 0;
  std::vector<ByteArray> inputs, mutants;
  BatchResult batch_result;
  for (size_t batch_index = 0; batch_index < number_of_batches; batch_index++) {
    if (EarlyExitRequested()) break;
    CHECK_LT(new_runs, env_.num_runs);
    auto remaining_runs = env_.num_runs - new_runs;
    auto batch_size = std::min(env_.batch_size, remaining_runs);
    // Pick a small but non-trivial number of inputs so that crossover works.
    // TODO(kcc): may need to parametrize this constant.
    inputs.resize(20);
    for (auto &input : inputs) {
      input = env_.use_corpus_weights ? corpus_.WeightedRandom(rng_())
                                      : corpus_.UniformRandom(rng_());
    }
    user_callbacks_.Mutate(inputs, batch_size, mutants);
    bool gained_new_coverage = RunBatch(
        mutants, batch_result, corpus_file.get(), features_file.get(), nullptr);
    new_runs += mutants.size();

    bool batch_is_power_of_two = ((batch_index - 1) & batch_index) == 0;

    if (gained_new_coverage) {
      Log("new-feature", 1);
    } else if (batch_is_power_of_two) {
      Log("pulse", 1);  // log if batch_index is a power of two.
    }

    if (batch_is_power_of_two) {
      GenerateCorpusStats();
    }

    if (env_.load_other_shard_frequency != 0 &&
        (batch_index % env_.load_other_shard_frequency) == 0 &&
        env_.total_shards > 1) {
      size_t rand = rng_() % (env_.total_shards - 1);
      size_t other_shard_index =
          (env_.my_shard_index + 1 + rand) % env_.total_shards;
      CHECK_NE(other_shard_index, env_.my_shard_index);
      LoadShard(env_, other_shard_index, /*rerun*/ false);
    }
  }
  Log("end-fuzz", 0);  // Tests rely on this line being present at the end.
}

void Centipede::ReportCrash(std::string_view binary,
                            const std::vector<ByteArray> &input_vec,
                            const BatchResult &batch_result) {
  if (num_crash_reports_ >= env_.max_num_crash_reports) return;

  LOG(INFO) << "Batch execution failed; exit code: "
            << batch_result.exit_code();
  // Print the full log contents to stderr (LOG(INFO) will truncate it).
  std::cerr << "Log of batch follows: [[[==================\n"
            << batch_result.log() << "==================]]]\n";

  std::string log_prefix =
      absl::StrCat("ReportCrash[", num_crash_reports_, "]: ");

  LOG(INFO) << log_prefix << "the crash occurred when running " << binary
            << " on " << input_vec.size() << " inputs";
  num_crash_reports_++;
  if (num_crash_reports_ == env_.max_num_crash_reports) {
    LOG(INFO)
        << log_prefix
        << "Reached max number of crash reports (--max_num_crash_reports): "
           "further reports will be suppressed";
  }

  // Executes one input.
  // If it crashes, dumps the reproducer to disk and returns true.
  // Otherwise returns false.
  auto TryOneInput = [&](const ByteArray &input) -> bool {
    BatchResult batch_result;
    if (user_callbacks_.Execute(binary, {input}, batch_result)) return false;
    auto hash = Hash(input);
    auto crash_dir = env_.MakeCrashReproducerDirPath();
    RemoteMkdir(crash_dir);
    std::string file_path = std::filesystem::path(crash_dir).append(hash);
    LOG(INFO) << log_prefix << "crash detected, saving input to " << file_path;
    LOG(INFO) << "input bytes: " << AsString(input);
    LOG(INFO) << "exit code: " << batch_result.exit_code();
    LOG(INFO) << "failure description: " << batch_result.failure_description();
    auto file = RemoteFileOpen(file_path, "w");  // overwrites existing file.
    if (!file) {
      LOG(FATAL) << log_prefix << "failed to open " << file_path;
    }
    RemoteFileAppend(file, input);
    RemoteFileClose(file);
    return true;
  };

  // First, try the input on which we presumably crashed.
  CHECK_EQ(input_vec.size(), batch_result.results().size());
  if (batch_result.num_outputs_read() < input_vec.size()) {
    LOG(INFO) << log_prefix << "executing input "
              << batch_result.num_outputs_read() << " out of "
              << input_vec.size();
    if (TryOneInput(input_vec[batch_result.num_outputs_read()])) return;
  }
  // Next, try all inputs one-by-one.
  LOG(INFO) << log_prefix
            << "executing inputs one-by-one, trying to find the reproducer";
  for (auto &input : input_vec) {
    if (TryOneInput(input)) return;
  }
  LOG(INFO) << log_prefix
            << "crash was not observed when running inputs one-by-one";
  // TODO(kcc): [as-needed] there will be cases when several inputs cause a
  // crash, but no single input does. Handle this case.
}

}  // namespace centipede
