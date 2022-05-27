// Copyright 2022 Google LLC.
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

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "./command.h"
#include "./defs.h"
#include "./execution_result.h"
#include "./feature.h"
#include "./logging.h"
#include "./shared_memory_blob_sequence.h"
#include "./util.h"

namespace centipede {

std::string CentipedeCallbacks::ConstructRunnerFlags(
    std::string_view extra_flags) {
  return absl::StrCat(
      "CENTIPEDE_RUNNER_FLAGS=", ":timeout_in_seconds=", env_.timeout, ":",
      ":address_space_limit_mb=", env_.address_space_limit_mb, ":",
      ":rss_limit_mb=", env_.rss_limit_mb, ":",
      env_.use_pc_features ? ":use_pc_features:" : "",
      env_.use_path_features ? ":use_path_features:" : "",
      env_.use_cmp_features ? ":use_cmp_features:" : "",
      env_.use_dataflow_features ? ":use_dataflow_features:" : "", extra_flags);
}

Command &CentipedeCallbacks::GetOrCreateCommandForBinary(
    std::string_view binary) {
  for (auto &cmd : commands_) {
    if (cmd.path() == binary) return cmd;
  }
  return commands_.emplace_back(Command(
      /*path=*/binary, /*args=*/{shmem_name1_, shmem_name2_},
      /*env=*/{ConstructRunnerFlags(":shmem:")}, /*out=*/execute_log_path_,
      /*err=*/ execute_log_path_));
}

int CentipedeCallbacks::ExecuteCentipedeSancovBinaryWithShmem(
    std::string_view binary, const std::vector<ByteArray> &inputs,
    BatchResult &batch_result) {
  batch_result.ClearAndResize(inputs.size());
  for (auto c : binary) CHECK(!isspace(c));  // Don't allow spaces in 'binary'

  // Set the shared memory sizes to 1Gb. There seem to be no penalty
  // for creating large shared memory regions if they are not actually
  // fully utilized.
  const size_t kBlobSeqSize = 1 << 30;  // 1Gb.
  SharedMemoryBlobSequence inputs_blobseq(shmem_name1_.c_str(), kBlobSeqSize);
  SharedMemoryBlobSequence feature_blobseq(shmem_name2_.c_str(), kBlobSeqSize);

  size_t num_inputs_written = 0;
  for (auto &input : inputs) {
    if (!inputs_blobseq.Write({1 /*unused tag*/, input.size(), input.data()})) {
      LOG(INFO)
          << "too many input bytes in the batch, inputs_blobseq overflown";
      break;
    }
    num_inputs_written++;
  }
  Command &cmd = GetOrCreateCommandForBinary(binary);
  int retval = cmd.Execute();
  if (cmd.WasInterrupted()) RequestEarlyExit(EXIT_FAILURE);
  batch_result.exit_code() = retval;
  CHECK(batch_result.Read(feature_blobseq));

  // We may have fewer feature blobs than inputs if
  // * some inputs were not written (i.e. num_inputs_written < inputs.size).
  //   * Logged above.
  // * some outputs were not written because the subprocess died.
  //   * Will be logged by the caller.
  // * some outputs were not written because the feature_blobseq overflown.
  //   * Logged by the following code.
  if (retval == 0 && batch_result.num_outputs_read() != num_inputs_written) {
    LOG(INFO) << "too few outputs while the subprocess succeeded. "
                 "feature_blobseq may have overflown";
  }
  if (retval != EXIT_SUCCESS)
    ReadFromLocalFile(execute_log_path_, batch_result.log());
  return retval;
}

size_t CentipedeCallbacks::LoadDictionary(std::string_view dictionary_path) {
  if (dictionary_path.empty()) return 0;
  ByteArray packed_corpus;
  ReadFromLocalFile(dictionary_path, packed_corpus);
  std::vector<ByteArray> unpacked_corpus;
  UnpackBytesFromAppendFile(packed_corpus, &unpacked_corpus);
  byte_array_mutator_.AddToDictionary(unpacked_corpus);
  LOG(INFO) << "loaded " << unpacked_corpus.size()
            << " dictionary entries from " << dictionary_path;
  return unpacked_corpus.size();
}

}  // namespace centipede
