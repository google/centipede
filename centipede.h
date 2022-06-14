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

#ifndef THIRD_PARTY_CENTIPEDE_CENTIPEDE_H_
#define THIRD_PARTY_CENTIPEDE_CENTIPEDE_H_

#include <time.h>

#include <cstddef>
#include <string_view>
#include <vector>

#include "./blob_file.h"
#include "./centipede_callbacks.h"
#include "./corpus.h"
#include "./coverage.h"
#include "./defs.h"
#include "./environment.h"
#include "./execution_result.h"
#include "./remote_file.h"
#include "./symbol_table.h"

namespace centipede {

// The main fuzzing class.
class Centipede {
 public:
  Centipede(const Environment &env, CentipedeCallbacks &user_callbacks,
            const Coverage::PCTable &pc_table, const SymbolTable &symbols);
  virtual ~Centipede() {}
  // Main loop.
  void FuzzingLoop();
  // Saves the sharded corpus into `dir`, one file per input.
  // Returns 0.
  static int SaveCorpusToLocalDir(const Environment &env, std::string_view dir);
  // Exports the corpus from `dir` (one file per input) into the sharded corpus.
  // Reads `dir` recursively.
  // Ignores inputs that already exist in the shard they need to be added to.
  // Sharding is stable and depends only on env.total_shards and the file name.
  // Returns 0.
  static int ExportCorpusFromLocalDir(const Environment &env,
                                      std::string_view dir);

 private:
  // Simpler timer for internal use.
  struct Timer {
    static time_t now() { return time(nullptr); }
    size_t seconds_since_beginning() const { return now() - beginning; }
    time_t beginning = now();
  };

  const Environment &env_;
  CentipedeCallbacks &user_callbacks_;
  Rng rng_;

  // Executes inputs from `input_vec`.
  // For every input, its pruned features are written to
  // `unconditional_features_file`, (if that's non-null).
  // For every input that caused new features to be observed:
  //   * the input is added to the corpus (corpus_ and fs_ are updated).
  //   * the input is written to `corpus_file` (if that's non-null).
  //   * its features are written to `features_file` (if that's non-null).
  // Returns true if new features were observed.
  // `batch_result` is used as a scratch storage to avoid extra mallocs.
  // Post-condition: `batch_result.results.size()` == `input_vec.size()`.
  // If RunBatch runs in a hot loop, define `batch_result` outside the loop.
  bool RunBatch(const std::vector<ByteArray> &input_vec,
                BatchResult &batch_result, BlobFileAppender *corpus_file,
                BlobFileAppender *features_file,
                BlobFileAppender *unconditional_features_file);
  // Loads a shard `shard_index` from `load_env.workdir`.
  // Note: `load_env_` may be different from `env_`.
  // If `rerun` is true, then also re-runs any inputs
  // for which the features are not found in `load_env.workdir`.
  void LoadShard(const Environment &load_env, size_t shard_index, bool rerun);
  // Prints one logging line with `log_type` in it
  // if `min_log_level` is not greater than `env_.log_level`.
  void Log(std::string_view log_type, size_t min_log_level);
  // For every feature in `fv`, translates the feature into code coverage
  // (PCIndex), then prints one logging line for every
  // FUNC/EDGE observed for the first time.
  // If symbolization failed, prints a simpler logging line.
  // Uses coverage_logger_ and VLOG(coverage_logger_verbose_level_).
  void LogFeaturesAsSymbols(const FeatureVec &f);
  // Generates a coverage report file in workdir.
  void GenerateCoverageReport();
  // Generates a corpus stats file in workdir.
  void GenerateCorpusStats();
  // Returns true if `input` passes env_.input_filter.
  bool InputPassesFilter(const ByteArray &input);
  // Executes `binary` with `input_vec` and `batch_result` as input/output.
  // If the binary crashes, calls ReportCrash().
  // Returns true iff there were no crashes.
  bool ExecuteAndReportCrash(std::string_view binary,
                             const std::vector<ByteArray> &input_vec,
                             BatchResult &batch_result);
  // Reports a crash and saves the reproducer to workdir/crashes, if possible.
  // `binary` is the binary causing the crash.
  // Prints the first `env_.max_num_crash_reports` logs.
  // `input_vec` is the batch of inputs that caused a crash.
  // `batch_result` contains the features computed for `input_vec`
  // (batch_result.results().size() == input_vec.size()). `batch_result` is used
  // as a hint when choosing which input to try first.
  void ReportCrash(std::string_view binary,
                   const std::vector<ByteArray> &input_vec,
                   const BatchResult &batch_result);
  // Merges shard `shard_index_to_merge` of the corpus in `merge_from_dir`
  // into the current corpus.
  // Writes added inputs to the current shard.
  void MergeFromOtherCorpus(std::string_view merge_from_dir,
                            size_t shard_index_to_merge);

  FeatureSet fs_;
  Timer timer_;  // counts time for coverage collection rate computation
  Corpus corpus_;
  size_t num_runs_ = 0;  // counts executed inputs

  // Coverage-related data, initialized at startup, once per process,
  // by InitializeCoverage.
  const Coverage::PCTable &pc_table_;
  const SymbolTable &symbols_;

  // Derived from env_.function_filter. Currently, duplicated by every thread.
  // In future, threads may have different filters.
  const FunctionFilter function_filter_;

  // Ensures every coverage location is reported at most once.
  CoverageLogger coverage_logger_;
  // Newly discovered coverage is logged
  // * with --v=2 (or higher) before "init-done".
  // * with --v=1 (or higher) after "init-done".
  int coverage_logger_verbose_level_ = 2;

  // Counts the number of crashes reported so far.
  int num_crash_reports_ = 0;

  // Path and command for the input_filter.
  std::string input_filter_path_;
  Command input_filter_cmd_;
};

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_CENTIPEDE_H_
