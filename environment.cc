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

#include "./environment.h"

#include <charconv>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "./logging.h"
#include "./util.h"

ABSL_FLAG(std::string, binary, "", "The target binary.");
ABSL_FLAG(std::string, coverage_binary, "",
          "The actual binary from which coverage is collected - if different "
          "from --binary.");
ABSL_FLAG(std::string, extra_binaries, "",
          "A comma-separated list of extra target binaries. These binaries are "
          "fed the same inputs as the main binary, but the coverage feedback "
          "from them is not collected. Use this e.g. to run the target under "
          "sanitizers.");
ABSL_FLAG(std::string, workdir, "", "The working directory.");
ABSL_FLAG(std::string, merge_from, "",
          "Another working directory to merge the corpus from. Inputs from "
          "--merge_from will be added to --workdir if the add new features.");
ABSL_FLAG(size_t, num_runs, std::numeric_limits<size_t>::max(),
          "Number of runs.");
ABSL_FLAG(size_t, seed, 0,
          "A seed for the random number generator. If 0, some other random "
          "number is used as seed.");
ABSL_FLAG(size_t, total_shards, 1, "Number of shards.");
ABSL_FLAG(size_t, first_shard_index, 0,
          "Index of the first shard, [0, --total_shards - --num_threads].");
ABSL_FLAG(size_t, num_threads, 1,
          "Number of threads to execute in one process. i-th thread, where i "
          "is in [0, --num_threads), will work on shard "
          "(--first_shard_index + i).");
ABSL_FLAG(size_t, j, 0,
          "If not 0, --j=N is a shorthand for "
          "--num_threads=N --total_shards=N --first_shard_index=0. "
          "Overrides values of these flags if they are also used.");
ABSL_FLAG(size_t, max_len, 4096, "Max length of mutants. Passed to mutator.");
ABSL_FLAG(size_t, batch_size, 1000,
          "The number of inputs given to the target at one time. Batches of "
          "more than 1 input are used to amortize the process start-up cost.");
ABSL_FLAG(size_t, mutate_batch_size, 20,
          "Mutate this many inputs to produce batch_size mutants");
ABSL_FLAG(size_t, load_other_shard_frequency, 10,
          "Load a random other shard after processing this many batches. Use 0 "
          "to disable loading other shards.  For now, choose the value of this "
          "flag so that shard loads  happen at most once in a few minutes. In "
          "future we may be able to find the suitable value automatically.");
ABSL_FLAG(size_t, prune_frequency, 100,
          "Prune the corpus every time after this many inputs were added. If "
          "zero, pruning is disabled. Pruning removes redundant inputs from "
          "the corpus, e.g. inputs that have only \"frequent\", i.e. "
          "uninteresting features. When the corpus gets larger than "
          "--max_corpus_size, some random elements may also be removed.");
ABSL_FLAG(size_t, address_space_limit_mb, 8192,
          "If not zero, instructs the target to set setrlimit(RLIMIT_AS) to "
          "this number of megabytes. Some targets (e.g. if built with ASAN, "
          "which can't run with RLIMIT_AS) may choose to ignore this flag. See "
          "also --rss_limit_mb.");
ABSL_FLAG(size_t, rss_limit_mb, 4096,
          "If not zero, instructs the target to fail if RSS goes over this "
          "number of megabytes and report an OOM. See also "
          "--address_space_limit_mb. These two flags have somewhat different "
          "meaning. --address_space_limit_mb does not allow the process to "
          "grow the used address space beyond the limit. --rss_limit_mb runs a "
          "background thread that monitors max RSS and also checks max RSS "
          "after executing every input, so it may detect OOM late. However "
          "--rss_limit_mb allows Centipede to *report* an OOM condition in "
          "most cases, while --address_space_limit_mb will cause a crash that "
          "may be hard to attribute to OOM.");
ABSL_FLAG(size_t, timeout, 60,
          "Timeout in seconds (if not 0). If an input runs longer than this "
          "number of seconds the runner process will abort. Support may vary "
          "depending on the runner.");
ABSL_FLAG(bool, fork_server, true,
          "If true (default) tries to execute the target(s) via the fork "
          "server, if supported by the target(s). Prepend the binary path with "
          "'%f' to disable the fork server. --fork_server applies to binaries "
          "passed via these flags: --binary, --extra_binaries, "
          "--input_filter.");
ABSL_FLAG(bool, full_sync, false,
          "Perform a full corpus sync on startup. If true, feature sets and "
          "corpora are read from all shards before fuzzing. This way fuzzing "
          "starts with a full knowledge of the current state and will avoid "
          "adding duplicating inputs. This however is very expensive when the "
          "number of shards is very large.");
ABSL_FLAG(bool, use_corpus_weights, true,
          "If true, use weighted distribution when choosing the corpus element "
          "to mutate. This flag is mostly for Centipede developers.");
ABSL_FLAG(bool, use_coverage_frontier, false,
          "If true, use coverage frontier when choosing the corpus element to "
          "mutate. This flag is mostly for Centipede developers.");
ABSL_FLAG(size_t, max_corpus_size, 100000,
          "Indicates the number of inputs in the in-memory corpus after which"
          "more aggressive pruning will be applied.");
ABSL_FLAG(int, crossover_level, 50,
          "Defines how much crossover is used during mutations. 0 means no "
          "crossover, 100 means the most aggressive crossover. See "
          "https://en.wikipedia.org/wiki/Crossover_(genetic_algorithm).");
ABSL_FLAG(bool, use_pc_features, true,
          "When available from instrumentation, use features derived from "
          "PCs.");
ABSL_FLAG(bool, use_cmp_features, true,
          "When available from instrumentation, use features derived from "
          "instrumentation of CMP instructions.");
ABSL_FLAG(bool, use_auto_dictionary, false,
          "If true, use automatically-generated dictionary derived from "
          "intercepting comparison instructions, memcmp, and similar.");
ABSL_FLAG(size_t, path_level, 0,  // Not ready for wide usage.
          "When available from instrumentation, use features derived from "
          "bounded execution paths. Be careful, may cause exponential feature "
          "explosion. 0 means no path features. Values between 1 and 100 "
          "define how aggressively to use the paths.");
ABSL_FLAG(bool, use_dataflow_features, true,
          "When available from instrumentation, use features derived from "
          "data flows.");
ABSL_FLAG(bool, use_counter_features, false,
          "When available from instrumentation, use features derived from "
          "counting the number of occurrences of a given PC. When enabled, "
          "supersedes --use_pc_features.");
ABSL_FLAG(bool, use_pcpair_features, false,
          "If true, PC pairs are used as additional synthetic features. "
          "Experimental, use with care - it may explode the corpus.");
ABSL_FLAG(size_t, feature_frequency_threshold, 100,
          "Internal flag. When a given feature is present in the corpus this "
          "many times Centipede will stop recording it for future corpus "
          "elements. Larger values will use more RAM but may improve corpus "
          "weights. Valid values are 1 - 255.");
ABSL_FLAG(bool, require_pc_table, true,
          "If true, Centipede will exit if the --pc_table is not found.");
ABSL_FLAG(int, telemetry_every_n_batches, 0,
          "Dump telemetry files, i.e. coverage report "
          "(workdir/coverage-report-BINARY.*.txt) and corpus stats "
          "(workdir/corpus-stats-*.json), every N batches. The default (0) has "
          "special meaning: dump every time the number of processed batches "
          "reaches the next power-of-2. -1 turns dumping off. Note that the "
          "initial (before fuzzing) and final (after fuzzing) versions of the "
          "files are always dumped.");
ABSL_FLAG(std::string, save_corpus_to_local_dir, "",
          "Save the remote corpus from working to the given directory, one "
          "file per corpus.");
ABSL_FLAG(std::string, export_corpus_from_local_dir, "",
          "Export a corpus from a local directory with one file per input into "
          "the sharded remote corpus in workdir. Not recursive.");
ABSL_FLAG(std::string, corpus_dir, "",
          "Comma-separated list of paths to local corpus dirs, with one file "
          "per input.At startup, the files are exported into the corpus in "
          "--workdir. While fuzzing the new corpus elements are written to the "
          "first dir. This makes it more convenient to interop with libFuzzer "
          "corpora.");
ABSL_FLAG(std::string, symbolizer_path, "llvm-symbolizer",
          "Path to the symbolizer tool. By default, we use llvm-symbolizer "
          "and assume it is in PATH.");
ABSL_FLAG(size_t, distill_shards, 0,
          "The first --distill_shards will write the distilled corpus to "
          "workdir/distilled-BINARY.SHARD. Implies --full_sync for these "
          "shards. Note that every shard will produce its own variant of "
          "distilled corpus. Distillation will work properly only if all "
          "shards already have their feature files computed.");
ABSL_FLAG(bool, exit_on_crash, false,
          "If true, Centipede will exit on the first crash of the target.");
ABSL_FLAG(size_t, num_crash_reports, 5, "report this many crashes per shard.");
ABSL_FLAG(std::string, input_filter, "",
          "Path to a tool that filters bad inputs. The tool is invoked as "
          "`input_filter INPUT_FILE` and should return 0 if the input is good "
          "and non-0 otherwise. Ignored if empty. The --input_filter is "
          "invoked only for inputs that are considered for addition to the "
          "corpus.");
ABSL_FLAG(std::string, for_each_blob, "",
          "If non-empty, extracts individual blobs from the files given as "
          "arguments, copies each blob to a temporary file, and applies this "
          "command to that temporary file. %P is replaced with the temporary "
          "file's path and %H is replaced with the blob's hash. Example:\n"
          "$ centipede --for_each_blob='ls -l  %P && echo %H' corpus.0");
ABSL_FLAG(std::string, experiment, "",
          "A colon-separated list of values, each of which is a flag followed "
          "by = and a comma-separated list of values. Example: "
          "'foo=1,2,3:bar=10,20'. When non-empty, this flag is used to run an "
          "A/B[/C/D...] experiment: different threads will set different "
          "values of 'foo' and 'bar' and will run independent fuzzing "
          "sessions. If more than one flag is given, all flag combinations are "
          "tested. In example above: '--foo=1 --bar=10' ... "
          "'--foo=3 --bar=20'. The number of threads should be multiple of the "
          "number of flag combinations.");
ABSL_FLAG(bool, analyze, false,
          "If set, Centipede will read the corpora from the work dirs provided"
          " as argv and analyze differences between those corpora."
          " Used by the Centipede developers to improve the engine. "
          " TODO(kcc) implement. ");
ABSL_FLAG(std::string, dictionary, "",
          "A comma-separated list of paths to dictionary files. The dictionary "
          "file is either in AFL/libFuzzer plain text format or in the binary "
          "Centipede corpus file format. The flag is interpreted by "
          "CentipedeCallbacks so its meaning may be different in custom "
          "implementations of CentipedeCallbacks.");
ABSL_FLAG(std::string, function_filter, "",
          "A comma-separated list of functions that fuzzing needs to focus on. "
          "If this list is non-empty, the fuzzer will mutate only those inputs "
          "that trigger code in one of these functions.");
ABSL_FLAG(size_t, shmem_size_mb, 1024,
          "Size of the shared memory regions used to communicate between the "
          "ending and the runner.");

namespace centipede {

Environment::Environment(int argc, char **argv)
    : binary(absl::GetFlag(FLAGS_binary)),
      coverage_binary(
          absl::GetFlag(FLAGS_coverage_binary).empty()
              ? (binary.empty() ? "" : *absl::StrSplit(binary, ' ').begin())
              : absl::GetFlag(FLAGS_coverage_binary)),
      extra_binaries(absl::StrSplit(absl::GetFlag(FLAGS_extra_binaries), ',',
                                    absl::SkipEmpty{})),
      workdir(absl::GetFlag(FLAGS_workdir)),
      merge_from(absl::GetFlag(FLAGS_merge_from)),
      num_runs(absl::GetFlag(FLAGS_num_runs)),
      total_shards(absl::GetFlag(FLAGS_total_shards)),
      my_shard_index(absl::GetFlag(FLAGS_first_shard_index)),
      num_threads(absl::GetFlag(FLAGS_num_threads)),
      max_len(absl::GetFlag(FLAGS_max_len)),
      batch_size(absl::GetFlag(FLAGS_batch_size)),
      mutate_batch_size(absl::GetFlag(FLAGS_mutate_batch_size)),
      load_other_shard_frequency(
          absl::GetFlag(FLAGS_load_other_shard_frequency)),
      seed(absl::GetFlag(FLAGS_seed)),
      prune_frequency(absl::GetFlag(FLAGS_prune_frequency)),
      address_space_limit_mb(absl::GetFlag(FLAGS_address_space_limit_mb)),
      rss_limit_mb(absl::GetFlag(FLAGS_rss_limit_mb)),
      timeout(absl::GetFlag(FLAGS_timeout)),
      fork_server(absl::GetFlag(FLAGS_fork_server)),
      full_sync(absl::GetFlag(FLAGS_full_sync)),
      use_corpus_weights(absl::GetFlag(FLAGS_use_corpus_weights)),
      use_coverage_frontier(absl::GetFlag(FLAGS_use_coverage_frontier)),
      max_corpus_size(absl::GetFlag(FLAGS_max_corpus_size)),
      crossover_level(absl::GetFlag(FLAGS_crossover_level)),
      use_pc_features(absl::GetFlag(FLAGS_use_pc_features)),
      path_level(absl::GetFlag(FLAGS_path_level)),
      use_cmp_features(absl::GetFlag(FLAGS_use_cmp_features)),
      use_auto_dictionary(absl::GetFlag(FLAGS_use_auto_dictionary)),
      use_dataflow_features(absl::GetFlag(FLAGS_use_dataflow_features)),
      use_counter_features(absl::GetFlag(FLAGS_use_counter_features)),
      use_pcpair_features(absl::GetFlag(FLAGS_use_pcpair_features)),
      feature_frequency_threshold(
          absl::GetFlag(FLAGS_feature_frequency_threshold)),
      require_pc_table(absl::GetFlag(FLAGS_require_pc_table)),
      telemetry_every_n_batches(
          absl::GetFlag(FLAGS_telemetry_every_n_batches)),
      distill_shards(absl::GetFlag(FLAGS_distill_shards)),
      save_corpus_to_local_dir(absl::GetFlag(FLAGS_save_corpus_to_local_dir)),
      export_corpus_from_local_dir(
          absl::GetFlag(FLAGS_export_corpus_from_local_dir)),
      corpus_dir(absl::StrSplit(absl::GetFlag(FLAGS_corpus_dir), ',',
                                absl::SkipEmpty{})),
      symbolizer_path(absl::GetFlag(FLAGS_symbolizer_path)),
      input_filter(absl::GetFlag(FLAGS_input_filter)),
      dictionary(absl::StrSplit(absl::GetFlag(FLAGS_dictionary), ',',
                                absl::SkipEmpty{})),
      function_filter(absl::GetFlag(FLAGS_function_filter)),
      for_each_blob(absl::GetFlag(FLAGS_for_each_blob)),
      experiment(absl::GetFlag(FLAGS_experiment)),
      analyze(absl::GetFlag(FLAGS_analyze)),
      exit_on_crash(absl::GetFlag(FLAGS_exit_on_crash)),
      max_num_crash_reports(absl::GetFlag(FLAGS_num_crash_reports)),
      shmem_size_mb(absl::GetFlag(FLAGS_shmem_size_mb)),
      cmd(binary),
      binary_name(std::filesystem::path(coverage_binary).filename().string()),
      binary_hash(HashOfFileContents(coverage_binary)) {
  if (size_t j = absl::GetFlag(FLAGS_j)) {
    total_shards = j;
    num_threads = j;
    my_shard_index = 0;
  }
  CHECK_GE(total_shards, 1);
  CHECK_GE(batch_size, 1);
  CHECK_GE(num_threads, 1);
  CHECK_LE(num_threads, total_shards);
  CHECK_LE(my_shard_index + num_threads, total_shards)
      << VV(my_shard_index) << VV(num_threads);
  if (argc > 0) {
    exec_name = argv[0];
    for (int argno = 1; argno < argc; ++argno) {
      args.emplace_back(argv[argno]);
    }
  }
}

namespace {

// Max number of decimal digits in a shard index given `total_shards`. Used to
// pad indices with 0's in output file names so the names are sorted by index.
inline constexpr int kDigitsInShardIndex = 6;

// If `annotation` is empty, returns an empty string. Otherwise, verifies that
// it does not start with a dot and returns it with a dot prepended.
std::string NormalizeAnnotation(std::string_view annotation) {
  std::string ret;
  if (!annotation.empty()) {
    CHECK_NE(annotation.front(), '.');
    ret = absl::StrCat(".", annotation);
  }
  return ret;
}

}  // namespace

std::string Environment::MakeCoverageDirPath() const {
  return std::filesystem::path(workdir).append(
      absl::StrCat(binary_name, "-", binary_hash));
}

std::string Environment::MakeCrashReproducerDirPath() const {
  return std::filesystem::path(workdir).append("crashes");
}

std::string Environment::MakeCorpusPath(size_t shard_index) const {
  return std::filesystem::path(workdir).append(
      absl::StrFormat("corpus.%0*d", kDigitsInShardIndex, shard_index));
}

std::string Environment::MakeFeaturesPath(size_t shard_index) const {
  return std::filesystem::path(MakeCoverageDirPath())
      .append(
          absl::StrFormat("features.%0*d", kDigitsInShardIndex, shard_index));
}

std::string Environment::MakeDistilledPath() const {
  return std::filesystem::path(workdir).append(absl::StrFormat(
      "distilled-%s.%0*d", binary_name, kDigitsInShardIndex, my_shard_index));
}

std::string Environment::MakeCoverageReportPath(
    std::string_view annotation) const {
  return std::filesystem::path(workdir).append(absl::StrFormat(
      "coverage-report-%s.%0*d%s.txt", binary_name, kDigitsInShardIndex,
      my_shard_index, NormalizeAnnotation(annotation)));
}

std::string Environment::MakeCorpusStatsPath(
    std::string_view annotation) const {
  return std::filesystem::path(workdir).append(absl::StrFormat(
      "corpus-stats-%s.%0*d%s.json", binary_name, kDigitsInShardIndex,
      my_shard_index, NormalizeAnnotation(annotation)));
}

// Returns true if `value` is one of "1", "true".
// Returns true if `value` is one of "0", "false".
// CHECK-fails otherwise.
static bool GetBoolFlag(std::string_view value) {
  if (value == "0" || value == "false") return false;
  CHECK(value == "1" || value == "true") << value;
  return true;
}

// Returns `value` as a size_t, CHECK-fails on parse error.
static size_t GetIntFlag(std::string_view value) {
  size_t result{};
  CHECK(std::from_chars(value.begin(), value.end(), result).ec == std::errc())
      << value;
  return result;
}

void Environment::SetFlag(std::string_view name, std::string_view value) {
  // TODO(kcc): support more flags, as needed.

  // Handle bool flags.
  absl::flat_hash_map<std::string, bool *> bool_flags{
      {"use_cmp_features", &use_cmp_features},
      {"use_coverage_frontier", &use_coverage_frontier}};
  auto bool_iter = bool_flags.find(name);
  if (bool_iter != bool_flags.end()) {
    *bool_iter->second = GetBoolFlag(value);
    return;
  }

  // Handle int flags.
  absl::flat_hash_map<std::string, size_t *> int_flags{
      {"path_level", &path_level},
      {"max_corpus_size", &max_corpus_size},
      {"mutate_batch_size", &mutate_batch_size}};
  auto int_iter = int_flags.find(name);
  if (int_iter != int_flags.end()) {
    *int_iter->second = GetIntFlag(value);
    return;
  }
  CHECK(false) << "Unknown flag for experiment: " << name << "=" << value;
}

void Environment::UpdateForExperiment() {
  if (experiment.empty()) return;

  // Parse the --experiments flag.
  struct Experiment {
    std::string flag_name;
    std::vector<std::string> flag_values;
  };
  std::vector<Experiment> experiments;
  for (auto flag : absl::StrSplit(this->experiment, ':', absl::SkipEmpty())) {
    std::vector<std::string> flag_and_value = absl::StrSplit(flag, '=');
    CHECK_EQ(flag_and_value.size(), 2) << flag;
    experiments.emplace_back(
        Experiment{flag_and_value[0], absl::StrSplit(flag_and_value[1], ',')});
  }

  // Count the number of flag combinations.
  size_t num_combinations = 1;
  for (const auto &exp : experiments) {
    CHECK_NE(exp.flag_values.size(), 0) << exp.flag_name;
    num_combinations *= exp.flag_values.size();
  }
  CHECK_GT(num_combinations, 0);
  CHECK_EQ(num_threads % num_combinations, 0)
      << VV(num_threads) << VV(num_combinations);

  // Update the flags for the current shard and compute experiment_name.
  // TODO(kcc): add and populate a field "experiment_values", like foo=1:bar=20.
  CHECK_LT(my_shard_index, num_threads);
  size_t my_combination_num = my_shard_index % num_combinations;
  experiment_name.clear();
  // Reverse the flags.
  // This way, the flag combinations will go in natural order.
  // E.g. for --experiment='foo=1,2,3:bar=10,20' the order of combinations is
  //   foo=1 bar=10
  //   foo=1 bar=20
  //   foo=2 bar=10 ...
  // Alternative would be to iterate in reverse order with rbegin()/rend().
  std::reverse(experiments.begin(), experiments.end());
  for (const auto &exp : experiments) {
    size_t idx = my_combination_num % exp.flag_values.size();
    SetFlag(exp.flag_name, exp.flag_values[idx]);
    my_combination_num /= exp.flag_values.size();
    experiment_name = std::to_string(idx) + experiment_name;
  }
  experiment_name = "E" + experiment_name;
  load_other_shard_frequency = 0;  // The experiments should be independent.
}

}  // namespace centipede
