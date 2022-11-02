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

#ifndef THIRD_PARTY_CENTIPEDE_ENVIRONMENT_H_
#define THIRD_PARTY_CENTIPEDE_ENVIRONMENT_H_

#include <cstddef>
#include <string>
#include <vector>

namespace centipede {

// Fuzzing environment that is initialized at startup and doesn't change.
// Data fields are copied from the FLAGS defined in centipede_interface.cc,
// or derived from them. See FLAGS descriptions for comments.
// Users or tests can override any of the non-const fields after the object
// is constructed, but before it is passed to CentipedeMain.
struct Environment {
  explicit Environment(const std::vector<std::string>& argv = {});

  Environment(int argc, char **argv)
      : Environment(std::vector<std::string>{argv, argv + argc}) {}

  std::string binary;
  std::string coverage_binary;
  std::vector<std::string> extra_binaries;
  std::string workdir;
  std::string merge_from;
  size_t num_runs;
  size_t total_shards;
  size_t my_shard_index;
  size_t num_threads;
  size_t max_len;
  size_t batch_size;
  size_t mutate_batch_size;
  size_t load_other_shard_frequency;
  size_t seed;
  size_t prune_frequency;
  size_t address_space_limit_mb;
  size_t rss_limit_mb;
  size_t timeout;
  bool fork_server;
  bool full_sync;
  bool use_corpus_weights;
  bool use_coverage_frontier;
  size_t max_corpus_size;
  int crossover_level;
  bool use_pc_features;
  size_t path_level;
  bool use_cmp_features;
  bool use_auto_dictionary;
  bool use_dataflow_features;
  bool use_counter_features;
  size_t use_pcpair_features;
  size_t feature_frequency_threshold;
  bool require_pc_table;
  int telemetry_frequency;
  size_t distill_shards;
  size_t log_features_shards;
  std::string save_corpus_to_local_dir;
  std::string export_corpus_from_local_dir;
  std::vector<std::string> corpus_dir;
  std::string symbolizer_path;
  std::string input_filter;
  std::vector<std::string> dictionary;
  std::string function_filter;
  std::string for_each_blob;
  std::string experiment;
  bool analyze;
  bool exit_on_crash;
  size_t max_num_crash_reports;
  size_t shmem_size_mb;

  std::string experiment_name;   // Set by UpdateForExperiment.
  std::string experiment_flags;  // Set by UpdateForExperiment.

  // Set to zero to reduce logging in tests.
  size_t log_level = 1;

  std::string exec_name;          // copied from argv[0]
  std::vector<std::string> args;  // copied from argv[1:].

  // Created once in CTOR, don't override.
  // The command to execute the binary (may contain arguments).
  const std::string cmd;
  const std::string binary_name;  // Name of coverage_binary, w/o directories.
  const std::string binary_hash;  // Hash of the coverage_binary file.
  bool has_input_wildcards = false;  // Set to true iff `binary` contains "@@"

  // Returns the path to the coverage dir.
  std::string MakeCoverageDirPath() const;
  // Returns the path to the crash reproducer dir.
  std::string MakeCrashReproducerDirPath() const;
  // Returns the path for a corpus file by its shard_index.
  std::string MakeCorpusPath(size_t shard_index) const;
  // Returns the path for a features file by its shard_index.
  std::string MakeFeaturesPath(size_t shard_index) const;
  // Returns the path for the distilled corpus file for my_shard_index.
  std::string MakeDistilledPath() const;
  // Returns true if we want to distill the corpus in this shard before fuzzing.
  bool DistillingInThisShard() const { return my_shard_index < distill_shards; }
  // Returns true if we want to log features as symbols in this shard.
  bool LogFeaturesInThisShard() const {
    return my_shard_index < log_features_shards;
  }
  // Returns the path for the coverage report file for my_shard_index.
  // The coverage report is generated before fuzzing begins and after it ends.
  // Non-default `annotation` becomes a part of the returned filename.
  // `annotation` must not start with a '.'.
  std::string MakeCoverageReportPath(std::string_view annotation = "") const;
  // Returns the path for the corpus stats report file for my_shard_index.
  // The corpus stats report is regenerated periodically during fuzzing.
  std::string MakeCorpusStatsPath(std::string_view annotation = "") const;
  // Returns true if we want to generate the telemetry files (coverage report,
  // corpus stats, etc.) in this shard.
  bool DumpTelemetryInThisShard() const { return my_shard_index == 0; }
  // Returns true if we want to generate the telemetry files (coverage report,
  // the corpus stats, etc.) after processing `batch_index`-th batch.
  bool DumpTelemetryForThisBatch(size_t batch_index) const {
    // Always dump for batch 0 (i.e. at the beginning of execution).
    if (batch_index == 0) {
      return true;
    }
    // Special mode for negative --telemetry_frequency: dump when batch_index
    // is a power-of-two and is >= than 2^abs(--telemetry_frequency).
    if (((telemetry_frequency < 0) &&
         (batch_index >= (1 << -telemetry_frequency)) &&
         ((batch_index - 1) & batch_index) == 0)) {
      return true;
    }
    // Normal mode: dump when requested number of batches get processed.
    if (((telemetry_frequency > 0) &&
         (batch_index % telemetry_frequency == 0))) {
      return true;
    }
    return false;
  }

  // Sets flag 'name' to `value`. CHECK-fails on invalid name/value combination.
  void SetFlag(std::string_view name, std::string_view value);

  // Updates `this` according to the `--experiment` flag.
  // The `--experiment` flag, if not empty, has this form:
  //   foo=1,2,3:bar=10,20
  // where foo and bar are some of the flag names supported for experimentation,
  // see `SetFlag()`.
  // `--experiment` defines the flag values to be set differently in different
  // shards. E.g. in this case,
  //   shard 0 will have {foo=1,bar=10},
  //   shard 1 will have {foo=1,bar=20},
  //   ...
  //   shard 3 will have {foo=2,bar=10},
  //   ...
  //   shard 5 will have {foo=2,bar=30},
  // and so on.
  //
  // CHECK-fails if the `--experiment` flag is not well-formed,
  // or if num_threads is not a multiple of the number of flag combinations
  // (which is 6 in this example).
  //
  // Sets load_other_shard_frequency=0 (experiments should be independent).
  //
  // Sets this->experiment_name to a string like "E01",
  // which means "value #0 is used for foo and value #1 is used for bar".
  void UpdateForExperiment();
};

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_ENVIRONMENT_H_
