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

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "absl/flags/flag.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "./logging.h"
#include "./util.h"

ABSL_FLAG(std::string, binary, "", "The target binary");
ABSL_FLAG(std::string, coverage_binary, "",
          "The actual binary from which coverge is collected "
          "- if different from --binary");
ABSL_FLAG(std::string, extra_binaries, "",
          "A comma-separated list of extra target binaries. "
          "These binaries are fed the same inputs as the main binary, "
          "but the coverage feedback from them is not collected. "
          "Use this e.g. to run the target under sanitizers.");
ABSL_FLAG(std::string, workdir, "", "The working directory");
ABSL_FLAG(std::string, merge_from, "",
          "Another working directory to merge the corpus from. "
          "Inputs from 'merge_from' will be added to 'workdir' "
          "if the add new features.");
ABSL_FLAG(size_t, num_runs, 1000000000, "number of runs");
ABSL_FLAG(size_t, seed, 0,
          "rng seed. "
          "If 0, some other random number is used as seed");
ABSL_FLAG(size_t, total_shards, 1, "number of shards");
ABSL_FLAG(size_t, first_shard_index, 0,
          "index of the first shard, [0, total_shards-num_threads]. ");
ABSL_FLAG(size_t, num_threads, 1,
          "number of threads to execute in one process. "
          "i-th thread, where i is in [0, num_threads), will work on shard "
          "(first_shard_index + i) ");
ABSL_FLAG(size_t, j, 0,
          "If not 0, --j=N is a shorthand for "
          "--num_threads=N --total_shards=N --first_shard_index=0. "
          "Overrides values of these flags if they are also used.");
ABSL_FLAG(size_t, max_len, 4096, "Max length of mutants. Passed to mutator");
ABSL_FLAG(size_t, batch_size, 1000,
          "The number of inputs given to the target at one time."
          " Batches of more than 1 input are used to amortize the process"
          " start-up cost.");
ABSL_FLAG(size_t, load_other_shard_frequency, 10,
          "Load a random other shard after processing this many batches. "
          "Use 0 to disable loading other shards. "
          " For now, choose the value of this flag so that shard loads "
          " happen at most once in a few minutes. In future we may be able to "
          " find the suitable value automatically");
ABSL_FLAG(size_t, prune_frequency, 100,
          "Prune the corpus every time after this many inputs were added."
          " If zero, pruning is disabled."
          " Pruning removes redundant inputs from the corpus, e.g. inputs"
          " that have only 'frequent', i.e. uninteresting features.");
ABSL_FLAG(size_t, address_space_limit_mb, 8192,
          "If not zero, instructs the target to set setrlimit(RLIMIT_AS) to "
          "this number of megabytes. "
          "Some targets (e.g. if built with ASAN, which can't run with "
          "RLIMIT_AS) may choose to ignore this flag. See also rss_limit_mb");
ABSL_FLAG(
    size_t, rss_limit_mb, 4096,
    "If not zero, instructs the target to fail if RSS goes over this "
    "number of megabytes and report an OOM. See also address_space_limit_mb. "
    "These two flags have somewhat different meaning. "
    "address_space_limit_mb does not allow the process to grow the used "
    "address space beyond the limit. "
    "rss_limit_mb runs a background thread that monitors max RSS "
    "and also checks max RSS after executing every input, "
    "so it may detect OOM late. "
    "However rss_limit_mb allows Centipede to *report* an OOM condition "
    "in most cases, while address_space_limit_mb will cause a crash that may "
    "be hard to attribute to OOM. ");
ABSL_FLAG(size_t, timeout, 60,
          "Timeout in seconds (if not zero). "
          "If an input runs longer than this number of seconds the runner "
          "process will abort. "
          "Support may vary depending on the runner. ");
ABSL_FLAG(bool, fork_server, true,
          "If true (default) tries to execute the target(s) via the fork "
          "server, if supported by the target(s). "
          "If the target binary does not natively support Centipede's "
          "fork server, prepend the binary path with '%F' and the "
          "fork server helper will be LD_PRELOAD-ed. "
          "Prepend the binary path with '%f' to disable the fork server. "
          "--fork_server applies to binaries passed via these flags: "
          "--binary, --extra_binaries, --input_filter");
ABSL_FLAG(std::string, fork_server_helper_path, "",
          "Path to the fork server helper DSO. "
          "If empty, the helper is assumed to be in the same dir as Centipede");
ABSL_FLAG(bool, full_sync, false,
          "Perform a full corpus sync on startup. If true, feature sets and "
          "corpora are read from all shards before fuzzing. This way fuzzing "
          "starts with a full knowledge of the current state and will avoid "
          "adding duplicating inputs. This however is very expensive when the "
          "number of shards is very large.");
ABSL_FLAG(bool, use_corpus_weights, true,
          "If true, use weighted distribution when"
          " choosing the corpus element to mutate."
          " This flag is mostly for Centipede developers.");
ABSL_FLAG(int, crossover_level, 50,
          "Defines how much crossover is used during mutations. "
          "0 means no crossover, 100 means the most aggressive crossover. "
          "See https://en.wikipedia.org/wiki/Crossover_(genetic_algorithm).");
ABSL_FLAG(bool, use_pc_features, true,
          "When available from instrumentation, use features derived from PCs");
ABSL_FLAG(bool, use_cmp_features, true,
          "When available from instrumentation, use features derived from "
          "instrumentation of CMP instructions");
ABSL_FLAG(int, path_level, 0,  // Not ready for wide usage.
          "When available from instrumentation, use features derived from "
          "bounded execution paths. Be careful, may cause exponential feature "
          "explosion. 0 means no path features. "
          "Values between 1 and 100 define how agressively to use the paths. ");
ABSL_FLAG(bool, use_dataflow_features, true,
          "When available from instrumentation, use features derived from "
          "data flows");
ABSL_FLAG(bool, use_counter_features, false,
          "When available from instrumentation, use features derived from "
          "counting the number of occurrences of a given PC. "
          "When enabled, supersedes --use_pc_features.");
ABSL_FLAG(bool, use_pcpair_features, false,
          "If true, PC pairs are used as additional synthetic features. "
          "Experimental, use with care - it may explode the corpus.");
ABSL_FLAG(bool, generate_corpus_stats, false,
          "If true, a file workdir/corpus-stats-BINARY.json containing"
          "corpus stats will be generated periodically");
ABSL_FLAG(std::string, save_corpus_to_local_dir, "",
          "save the remote corpus from working to the given directory, one "
          "file per corpus.");
ABSL_FLAG(std::string, export_corpus_from_local_dir, "",
          "export a corpus from a local directory with one file per input "
          "into the sharded remote corpus in workdir. Not recursive");
ABSL_FLAG(std::string, corpus_dir, "",
          "Comma-separated list of paths to local corpus dirs, "
          "with one file per input."
          "At startup, the files are exported into the corpus in workdir. "
          "While fuzzing the new corpus elements are written to the first dir. "
          "This makes it more convenient to interop with libFuzzer corpora.");
ABSL_FLAG(std::string, llvm_symbolizer_path, "llvm-symbolizer",
          "Path to the llvm-symbolizer tool. "
          "Default is to assume it is in PATH");
ABSL_FLAG(
    size_t, distill_shards, 0,
    "The first `distill_shards` will write the distilled corpus to "
    "workdir/distilled-BINARY.SHARD. Implies full_sync for these shards. "
    "Note that every shard will produce its own variant of distilled corpus. "
    "Distillation will work properly only if all shards already have their "
    "feature files computed.");

ABSL_FLAG(bool, exit_on_crash, false,
          "If true, Centipede will exit on the first crash of the target");
ABSL_FLAG(size_t, num_crash_reports, 5, "report this many crashes per shard");
ABSL_FLAG(std::string, input_filter, "",
          "Path to a tool that filters bad inputs. "
          "The tool is invoked as 'input_filter INPUT_FILE' and returns 0 "
          "if the input is good and non-0 otherwise. Ignored if empty. "
          "The input_filter is invoked only for inputs that are considered "
          "for addition to the corpus.");

ABSL_FLAG(std::string, for_each_blob, "",
          "If non-empty, extracts individual blobs from the files "
          "given as arguments, copies each blob to a temporary file, "
          "and applies this command to that temporary file. "
          "%P is replaced with the temporary file's path and "
          "%H is replaced with the blob's hash. "
          "Example: "
          "  centipede --for_each_blob='ls -l  %P && echo %H' corpus.0");

ABSL_FLAG(std::string, dictionary, "",
          "A comma-separated list of paths to dictionary files. "
          "The dictionary file is either in AFL/libFuzzer plain text format or "
          "in the binary Centipede corpus file format. "
          "The flag is interpreted by CentipedeCallbacks so its meaning may "
          "be different in custom implementations of CentipedeCallbacks.");

ABSL_FLAG(std::string, function_filter, "",
          "A comma-separated list of functions that fuzzing needs to focus on. "
          "If this list is non-empty, the fuzzer will mutate only those inputs "
          "that trigger code in one of these functions. ");

namespace centipede {

Environment::Environment(int argc, char** argv)
    : binary(absl::GetFlag(FLAGS_binary)),
      coverage_binary(absl::GetFlag(FLAGS_coverage_binary).empty()
                          ? *absl::StrSplit(binary, ' ').begin()
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
      crossover_level(absl::GetFlag(FLAGS_crossover_level)),
      use_pc_features(absl::GetFlag(FLAGS_use_pc_features)),
      path_level(absl::GetFlag(FLAGS_path_level)),
      use_cmp_features(absl::GetFlag(FLAGS_use_cmp_features)),
      use_dataflow_features(absl::GetFlag(FLAGS_use_dataflow_features)),
      use_counter_features(absl::GetFlag(FLAGS_use_counter_features)),
      use_pcpair_features(absl::GetFlag(FLAGS_use_pcpair_features)),
      generate_corpus_stats(absl::GetFlag(FLAGS_generate_corpus_stats)),
      distill_shards(absl::GetFlag(FLAGS_distill_shards)),
      fork_server_helper_path(absl::GetFlag(FLAGS_fork_server_helper_path)),
      save_corpus_to_local_dir(absl::GetFlag(FLAGS_save_corpus_to_local_dir)),
      export_corpus_from_local_dir(
          absl::GetFlag(FLAGS_export_corpus_from_local_dir)),
      corpus_dir(absl::StrSplit(absl::GetFlag(FLAGS_corpus_dir), ',',
                                absl::SkipEmpty{})),
      llvm_symbolizer_path(absl::GetFlag(FLAGS_llvm_symbolizer_path)),
      input_filter(absl::GetFlag(FLAGS_input_filter)),
      dictionary(absl::StrSplit(absl::GetFlag(FLAGS_dictionary), ',',
                                absl::SkipEmpty{})),
      function_filter(absl::GetFlag(FLAGS_function_filter)),
      for_each_blob(absl::GetFlag(FLAGS_for_each_blob)),
      exit_on_crash(absl::GetFlag(FLAGS_exit_on_crash)),
      max_num_crash_reports(absl::GetFlag(FLAGS_num_crash_reports)),
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
  CHECK_LE(my_shard_index + num_threads, total_shards);
  if (argc > 0) {
    exec_name = argv[0];
    for (int argno = 1; argno < argc; ++argno) {
      args.push_back(argv[argno]);
    }
  }
}

std::string Environment::GetForkServerHelperPath() const {
  if (!fork_server) return "";  // no need for the fork server helper.

  // If present, use the user-provided path as-is.
  std::string path = fork_server_helper_path;

  if (path.empty()) {
    // Compute fork_server_helper_path based on Centipede's path.
    path = std::filesystem::absolute(
               std::filesystem::path(exec_name).parent_path()) /
           "runner_fork_server_helper.so";
  }

  if (!std::filesystem::exists(path)) {
    LOG(INFO) << "Fork server helper not found (" << VV(path)
              << "): %F for target binaries won't work";
  }

  return path;
}

std::string Environment::MakeCoverageDirPath() const {
  return std::filesystem::path(workdir).append(
      absl::StrCat(binary_name, "-", binary_hash));
}

std::string Environment::MakeCrashReproducerDirPath() const {
  return std::filesystem::path(workdir).append("crashes");
}

std::string Environment::MakeCorpusPath(size_t shard_index) const {
  return std::filesystem::path(workdir).append(
      absl::StrCat("corpus.", shard_index));
}
std::string Environment::MakeFeaturesPath(size_t shard_index) const {
  return std::filesystem::path(MakeCoverageDirPath())
      .append(absl::StrCat("features.", shard_index));
}
std::string Environment::MakeDistilledPath() const {
  return std::filesystem::path(workdir).append(
      absl::StrCat("distilled-", binary_name, ".", my_shard_index));
}

std::string Environment::MakeCoverageReportPath() const {
  return std::filesystem::path(workdir).append(absl::StrCat(
      "coverage-report-", binary_name, ".", my_shard_index, ".txt"));
}
std::string Environment::MakeCorpusStatsPath() const {
  return std::filesystem::path(workdir).append(
      absl::StrCat("corpus-stats-", binary_name, ".", my_shard_index, ".json"));
}

}  // namespace centipede
