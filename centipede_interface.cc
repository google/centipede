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

#include "./centipede_interface.h"

#include <signal.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "./centipede.h"
#include "./command.h"
#include "./defs.h"
#include "./environment.h"
#include "./logging.h"
#include "./remote_file.h"
#include "./shared_memory_blob_sequence.h"
#include "./symbol_table.h"
#include "./util.h"

namespace centipede {

// See also: MutateInputsFromShmem().
bool MutateViaExternalBinary(std::string_view binary,
                             const std::vector<ByteArray> &inputs,
                             std::vector<ByteArray> &mutants) {
  // Create in/out shmem blob sequences.
  std::string shmem_name1 = ProcessAndThreadUniqueID("/centipede-in-");
  std::string shmem_name2 = ProcessAndThreadUniqueID("/centipede-out-");
  const size_t kBlobSeqSize = 1 << 30;  // 1Gb, something large enough.
  SharedMemoryBlobSequence inputs_blobseq(shmem_name1.c_str(), kBlobSeqSize);
  SharedMemoryBlobSequence mutant_blobseq(shmem_name2.c_str(), kBlobSeqSize);
  // Write mutants.size() as the first input.
  size_t num_mutants = mutants.size();
  CHECK(inputs_blobseq.Write({1 /*unused tag*/, sizeof(num_mutants),
                              reinterpret_cast<uint8_t *>(&num_mutants)}));
  // Write all inputs.
  for (auto &input : inputs) {
    if (!inputs_blobseq.Write({1 /*unused tag*/, input.size(), input.data()})) {
      break;
    }
  }
  // Execute.
  Command cmd(binary, {shmem_name1, shmem_name2},
              {"CENTIPEDE_RUNNER_FLAGS=:mutate:"}, "/dev/null", "/dev/null");
  int retval = cmd.Execute();
  if (cmd.WasInterrupted()) RequestEarlyExit(EXIT_FAILURE);

  // Read all mutants.
  for (auto &mutant : mutants) {
    auto blob = mutant_blobseq.Read();
    if (blob.size == 0) break;
    mutant.clear();
    mutant.insert(mutant.begin(), blob.data, blob.data + blob.size);
  }
  return retval == 0;
}

namespace {

// Sets signal handler for SIGINT.
void SetSignalHandlers() {
  struct sigaction sigact = {};
  sigact.sa_handler = [](int) {
    ABSL_RAW_LOG(INFO, "SIGINT caught; cleaning up\n");
    RequestEarlyExit(EXIT_FAILURE);
  };
  sigaction(SIGINT, &sigact, nullptr);
}

void InitializeCoverage(const Environment &env, Coverage::PCTable &pc_table,
                        SymbolTable &symbols) {
  // Running in main thread, create our own temp dir.
  CreateLocalDirRemovedAtExit(TemporaryLocalDirPath());
  auto tmpdir = TemporaryLocalDirPath();
  std::string pc_table_path = std::filesystem::path(tmpdir).append("pc_table");
  pc_table = Coverage::GetPcTableFromBinary(env.coverage_binary, pc_table_path);
  if (pc_table.empty()) {
    LOG(INFO) << "Could not get PCTable, debug symbols will not be used";
  } else {
    std::string tmp1 = std::filesystem::path(tmpdir).append("sym-tmp1");
    std::string tmp2 = std::filesystem::path(tmpdir).append("sym-tmp2");
    symbols.GetSymbolsFromBinary(pc_table, env.coverage_binary,
                                 env.llvm_symbolizer_path, tmp1, tmp2);
    if (symbols.size() != pc_table.size()) {
      LOG(INFO) << "symbolization failed, debug symbols will not be used";
      pc_table.clear();
    }
  }
}

// Runs env.for_each_blob on every blob extracted from env.args.
// Returns EXIT_SUCCESS on success, EXIT_FAILURE otherwise.
int ForEachBlob(const Environment &env) {
  auto tmpdir = TemporaryLocalDirPath();
  CreateLocalDirRemovedAtExit(tmpdir);
  std::string tmpfile = std::filesystem::path(tmpdir).append("t");

  for (const auto &arg : env.args) {
    LOG(INFO) << "Running '" << env.for_each_blob << "' on " << arg;
    // TODO(kcc): [impl] replace this with FileBlob, once ready.
    RemoteFile *f = RemoteFileOpen(arg, "r");
    if (!f) {
      LOG(INFO) << "failed to open " << arg;
      return EXIT_FAILURE;
    }
    ByteArray bytes;
    RemoteFileRead(f, bytes);
    RemoteFileClose(f);

    std::vector<ByteArray> unpacked;
    UnpackBytesFromAppendFile(bytes, &unpacked);
    for (const auto &blob : unpacked) {
      WriteToLocalFile(tmpfile, blob);
      std::string command_line = absl::StrReplaceAll(
          env.for_each_blob, {{"%P", tmpfile}, {"%H", Hash(blob)}});
      Command cmd(command_line);
      // TODO(kcc): [as-needed] this creates one process per blob.
      // If this flag gets active use, we may want to define special cases,
      // e.g. if for_each_blob=="cp %P /some/where" we can do it in-process.
      cmd.Execute();
      if (cmd.WasInterrupted() || EarlyExitRequested()) return ExitCode();
    }
  }
  return EXIT_SUCCESS;
}

}  // namespace

int CentipedeMain(const Environment &env,
                  CentipedeCallbacksFactory &callbacks_factory) {
  SetSignalHandlers();

  if (!env.save_corpus_to_local_dir.empty())
    return Centipede::SaveCorpusToLocalDir(env, env.save_corpus_to_local_dir);

  if (!env.for_each_blob.empty()) return ForEachBlob(env);

  // Just export the corpus from a local dir and exit.
  if (!env.export_corpus_from_local_dir.empty())
    return Centipede::ExportCorpusFromLocalDir(
        env, env.export_corpus_from_local_dir);

  // Export the corpus from a local dir and then fuzz.
  if (!env.corpus_dir.empty()) {
    for (const auto &corpus_dir : env.corpus_dir) {
      Centipede::ExportCorpusFromLocalDir(env, corpus_dir);
    }
  }

  // Create the coverage dir once, before creating any threads.
  LOG(INFO) << "coverage dir " << env.MakeCoverageDirPath();
  RemoteMkdir(env.MakeCoverageDirPath());

  Coverage::PCTable pc_table;
  SymbolTable symbols;
  InitializeCoverage(env, pc_table, symbols);

  std::vector<std::thread> threads(env.num_threads);
  auto thread_callback = [&](size_t my_shard_index) {
    CreateLocalDirRemovedAtExit(TemporaryLocalDirPath());  // creates temp dir.
    Environment my_env = env;
    my_env.my_shard_index = my_shard_index;
    auto user_callbacks = callbacks_factory.create(my_env);
    Centipede centipede(my_env, *user_callbacks, pc_table, symbols);
    centipede.FuzzingLoop();
    callbacks_factory.destroy(user_callbacks);
  };

  // Create threads.
  for (size_t thread_idx = 0; thread_idx < env.num_threads; thread_idx++) {
    threads[thread_idx] =
        std::thread(thread_callback, env.my_shard_index + thread_idx);
  }
  // Join threads.
  for (size_t thread_idx = 0; thread_idx < env.num_threads; thread_idx++) {
    threads[thread_idx].join();
  }
  return ExitCode();
}

}  // namespace centipede
