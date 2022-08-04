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
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_replace.h"
#include "absl/types/span.h"
#include "./blob_file.h"
#include "./centipede.h"
#include "./command.h"
#include "./coverage.h"
#include "./defs.h"
#include "./environment.h"
#include "./logging.h"
#include "./remote_file.h"
#include "./symbol_table.h"
#include "./util.h"

namespace centipede {

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

// Runs env.for_each_blob on every blob extracted from env.args.
// Returns EXIT_SUCCESS on success, EXIT_FAILURE otherwise.
int ForEachBlob(const Environment &env) {
  auto tmpdir = TemporaryLocalDirPath();
  CreateLocalDirRemovedAtExit(tmpdir);
  std::string tmpfile = std::filesystem::path(tmpdir).append("t");

  for (const auto &arg : env.args) {
    LOG(INFO) << "Running '" << env.for_each_blob << "' on " << arg;
    auto blob_reader = DefaultBlobFileReaderFactory();
    absl::Status open_status = blob_reader->Open(arg);
    if (!open_status.ok()) {
      LOG(INFO) << "failed to open " << arg << ": " << open_status;
      return EXIT_FAILURE;
    }
    absl::Span<uint8_t> blob;
    while (blob_reader->Read(blob) == absl::OkStatus()) {
      ByteArray bytes;
      bytes.insert(bytes.begin(), blob.data(), blob.end());
      // TODO(kcc): [impl] add a variant of WriteToLocalFile that accepts Span.
      WriteToLocalFile(tmpfile, bytes);
      std::string command_line = absl::StrReplaceAll(
          env.for_each_blob, {{"%P", tmpfile}, {"%H", Hash(bytes)}});
      Command cmd(command_line);
      // TODO(kcc): [as-needed] this creates one process per blob.
      // If this flag gets active use, we may want to define special cases,
      // e.g. if for_each_blob=="cp %P /some/where" we can do it in-process.
      cmd.Execute();
      if (EarlyExitRequested()) return ExitCode();
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

  auto one_time_callbacks = callbacks_factory.create(env);
  Coverage::PCTable pc_table;
  SymbolTable symbols;
  one_time_callbacks->PopulateSymbolAndPcTables(symbols, pc_table);
  callbacks_factory.destroy(one_time_callbacks);
  if (env.use_pcpair_features) {
    CHECK(!pc_table.empty())
        << "use_pcpair_features requires non-empty pc_table";
  }
  CoverageLogger coverage_logger(pc_table, symbols);

  std::vector<std::thread> threads(env.num_threads);
  auto thread_callback = [&](size_t my_shard_index) {
    CreateLocalDirRemovedAtExit(TemporaryLocalDirPath());  // creates temp dir.
    Environment my_env = env;
    my_env.my_shard_index = my_shard_index;
    my_env.seed = GetRandomSeed(env.seed);
    auto user_callbacks = callbacks_factory.create(my_env);
    Centipede centipede(my_env, *user_callbacks, pc_table, symbols,
                        coverage_logger);
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
