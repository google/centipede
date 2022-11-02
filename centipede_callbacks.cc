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

#include "./centipede_callbacks.h"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "./command.h"
#include "./coverage.h"
#include "./defs.h"
#include "./execution_request.h"
#include "./execution_result.h"
#include "./logging.h"
#include "./symbol_table.h"
#include "./util.h"

namespace centipede {

void CentipedeCallbacks::PopulateSymbolAndPcTables(
    SymbolTable &symbols, Coverage::PCTable &pc_table) {
  // Running in main thread, create our own temp dir.
  if (!std::filesystem::exists(temp_dir_)) {
    CreateLocalDirRemovedAtExit(temp_dir_);
  }
  std::string pc_table_path =
      std::filesystem::path(temp_dir_).append("pc_table");
  pc_table =
      Coverage::GetPcTableFromBinary(env_.coverage_binary, pc_table_path);
  if (pc_table.empty()) {
    if (env_.require_pc_table) {
      LOG(INFO) << "Could not get PCTable, exiting (override with "
                   "--require_pc_table=0)";
      exit(EXIT_FAILURE);
    }
    LOG(INFO) << "Could not get PCTable, debug symbols will not be used";
  } else {
    std::string tmp1 = std::filesystem::path(temp_dir_).append("sym-tmp1");
    std::string tmp2 = std::filesystem::path(temp_dir_).append("sym-tmp2");
    CHECK(!env_.coverage_binary.empty());
    std::vector<std::string> binary_flags =
        absl::StrSplit(env_.coverage_binary, ' ');
    std::string binary_name = binary_flags[0];
    symbols.GetSymbolsFromBinary(pc_table, binary_name, env_.symbolizer_path,
                                 tmp1, tmp2);
  }
}

std::string CentipedeCallbacks::ConstructRunnerFlags(
    std::string_view extra_flags, bool disable_coverage) {
  std::string path_level;
  if (!disable_coverage)
    path_level = absl::StrCat(":path_level=", env_.path_level, ":");
  return absl::StrCat(
      "CENTIPEDE_RUNNER_FLAGS=", ":timeout_in_seconds=", env_.timeout, ":",
      ":address_space_limit_mb=", env_.address_space_limit_mb, ":",
      ":rss_limit_mb=", env_.rss_limit_mb, ":",
      env_.use_pc_features && !disable_coverage ? ":use_pc_features:" : "",
      env_.use_counter_features && !disable_coverage ? ":use_counter_features:"
                                                     : "",
      path_level,
      env_.use_cmp_features && !disable_coverage ? ":use_cmp_features:" : "",
      env_.use_auto_dictionary && !disable_coverage ? ":use_auto_dictionary:"
                                                    : "",
      env_.use_dataflow_features && !disable_coverage
          ? ":use_dataflow_features:"
          : "",
      ":crossover_level=", env_.crossover_level, ":", extra_flags);
}

Command &CentipedeCallbacks::GetOrCreateCommandForBinary(
    std::string_view binary) {
  for (auto &cmd : commands_) {
    if (cmd.path() == binary) return cmd;
  }
  // We don't want to collect coverage for extra binaries. It won't be used.
  bool disable_coverage =
      std::find(env_.extra_binaries.begin(), env_.extra_binaries.end(),
                binary) != env_.extra_binaries.end();
  // Allow for the time it takes to fork a subprocess etc.
  const auto amortized_timeout = absl::Seconds(env_.timeout) + absl::Seconds(5);
  Command &cmd = commands_.emplace_back(Command(
      /*path=*/binary, /*args=*/{shmem_name1_, shmem_name2_},
      /*env=*/
      {ConstructRunnerFlags(
          absl::StrCat(":shmem:arg1=", shmem_name1_, ":arg2=", shmem_name2_,
                       ":failure_description_path=", failure_description_path_,
                       ":"),
          disable_coverage)},
      /*out=*/execute_log_path_,
      /*err=*/execute_log_path_,
      /*timeout=*/amortized_timeout));
  if (env_.fork_server) cmd.StartForkServer(temp_dir_, Hash(binary));

  return cmd;
}

int CentipedeCallbacks::ExecuteCentipedeSancovBinaryWithShmem(
    std::string_view binary, const std::vector<ByteArray> &inputs,
    BatchResult &batch_result) {
  batch_result.ClearAndResize(inputs.size());

  // Reset the blobseqs.
  inputs_blobseq_.Reset();
  outputs_blobseq_.Reset();

  // Feed the inputs to inputs_blobseq_.
  size_t num_inputs_written =
      execution_request::RequestExecution(inputs, inputs_blobseq_);

  if (num_inputs_written != inputs.size()) {
    LOG(INFO) << "Wrote " << num_inputs_written << "/" << inputs.size()
              << " inputs; shmem_size_mb might be too small: "
              << env_.shmem_size_mb;
  }

  // Run.
  Command &cmd = GetOrCreateCommandForBinary(binary);
  int retval = cmd.Execute();
  inputs_blobseq_.ReleaseSharedMemory();  // Inputs are already consumed.

  // Get results.
  batch_result.exit_code() = retval;
  CHECK(batch_result.Read(outputs_blobseq_));
  outputs_blobseq_.ReleaseSharedMemory();  // Outputs are already consumed.

  // We may have fewer feature blobs than inputs if
  // * some inputs were not written (i.e. num_inputs_written < inputs.size).
  //   * Logged above.
  // * some outputs were not written because the subprocess died.
  //   * Will be logged by the caller.
  // * some outputs were not written because the outputs_blobseq_ overflown.
  //   * Logged by the following code.
  if (retval == 0 && batch_result.num_outputs_read() != num_inputs_written) {
    LOG(INFO) << "Read " << batch_result.num_outputs_read() << "/"
              << num_inputs_written
              << " outputs; shmem_size_mb might be too small: "
              << env_.shmem_size_mb;
  }
  if (retval != EXIT_SUCCESS) {
    ReadFromLocalFile(execute_log_path_, batch_result.log());
    ReadFromLocalFile(failure_description_path_,
                      batch_result.failure_description());
    // Remove failure_description_ here so that it doesn't stay until another
    // failed execution.
    std::filesystem::remove(failure_description_path_);
  }
  return retval;
}

// See also: MutateInputsFromShmem().
bool CentipedeCallbacks::MutateViaExternalBinary(
    std::string_view binary, const std::vector<ByteArray> &inputs,
    std::vector<ByteArray> &mutants) {
  inputs_blobseq_.Reset();
  outputs_blobseq_.Reset();

  size_t num_inputs_written = execution_request::RequestMutation(
      mutants.size(), inputs, inputs_blobseq_);

  if (num_inputs_written != inputs.size())
    LOG(INFO) << VV(num_inputs_written) << VV(inputs.size());

  // Execute.
  Command &cmd = GetOrCreateCommandForBinary(binary);
  int retval = cmd.Execute();
  inputs_blobseq_.ReleaseSharedMemory();  // Inputs are already consumed.

  // Read all mutants.
  for (size_t i = 0; i < mutants.size(); ++i) {
    auto blob = outputs_blobseq_.Read();
    if (blob.size == 0) {
      mutants.resize(i);
      break;
    }
    auto &mutant = mutants[i];
    mutant.clear();
    mutant.insert(mutant.begin(), blob.data, blob.data + blob.size);
  }
  outputs_blobseq_.ReleaseSharedMemory();  // Outputs are already consumed.
  return retval == 0;
}

size_t CentipedeCallbacks::LoadDictionary(std::string_view dictionary_path) {
  if (dictionary_path.empty()) return 0;
  // First, try to parse the dictionary as an AFL/libFuzzer dictionary.
  // These dictionaries are in plain text format and thus a Centipede-native
  // dictionary will never be mistaken for an AFL/libFuzzer dictionary.
  std::string text;
  ReadFromLocalFile(dictionary_path, text);
  std::vector<ByteArray> entries;
  if (ParseAFLDictionary(text, entries) && !entries.empty()) {
    byte_array_mutator_.AddToDictionary(entries);
    LOG(INFO) << "Loaded " << entries.size()
              << " dictionary entries from AFL/libFuzzer dictionary "
              << dictionary_path;
    return entries.size();
  }
  // Didn't parse as plain text. Assume Centipede-native corpus format.
  ByteArray packed_corpus(text.begin(), text.end());
  std::vector<ByteArray> unpacked_corpus;
  UnpackBytesFromAppendFile(packed_corpus, &unpacked_corpus);
  CHECK(!unpacked_corpus.empty())
      << "Empty or corrupt dictionary file: " << dictionary_path;
  byte_array_mutator_.AddToDictionary(unpacked_corpus);
  LOG(INFO) << "Loaded " << unpacked_corpus.size()
            << " dictionary entries from " << dictionary_path;
  return unpacked_corpus.size();
}

}  // namespace centipede
