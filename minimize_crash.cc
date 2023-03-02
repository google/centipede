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

#include "./minimize_crash.h"

#include <filesystem>  // NOLINT

#include "./centipede_callbacks.h"
#include "./environment.h"
#include "./logging.h"
#include "./util.h"

namespace centipede {

// Work queue for the minimizer.
// TODO(kcc): extend it to suport concurrent executions (add locks, etc).
struct MinimizerWorkQueue {
  std::string crash_dir;
  std::vector<ByteArray> crashers;
};

// Performs a minimization loop in one thread.
static void MinimizeCrash(const Environment &env, CentipedeCallbacks *callbacks,
                          MinimizerWorkQueue &queue) {
  BatchResult batch_result;

  LOG(INFO) << "Starting the crash minimization loop";
  size_t num_batches = env.num_runs / env.batch_size;
  for (size_t i = 0; i < num_batches; ++i) {
    // Create several mutants that are smaller than the current smallest one.
    //
    // Currently, we do this by calling the vanilla mutator and
    // discarding all inputs that are too large.
    // TODO(kcc): modify the Mutate() interface such that max_len can be passed.
    //
    // We currently mutate all known crashers, not just the smallest one(s)
    // because we want to avoid being stuck in local minimum.
    // TODO(kcc): experiment with heuristics here: e.g. mutate N smallest, etc.

    std::vector<ByteArray> mutants;
    callbacks->Mutate(queue.crashers, env.batch_size, mutants);
    std::vector<ByteArray> smaller_mutants;
    for (const auto &m : mutants) {
      if (m.size() < queue.crashers.back().size()) smaller_mutants.push_back(m);
    }

    // Execute all mutants. If a new crasher is found, add it to `queue`.
    if (!callbacks->Execute(env.binary, smaller_mutants, batch_result)) {
      size_t crash_inputs_idx = batch_result.num_outputs_read();
      CHECK_LT(crash_inputs_idx, smaller_mutants.size());
      const auto &new_crasher = smaller_mutants[crash_inputs_idx];
      LOG(INFO) << "Crasher: size: " << new_crasher.size() << ": "
                << AsString(new_crasher, 40);
      queue.crashers.emplace_back(new_crasher);
      // Write the crasher to disk.
      auto hash = Hash(new_crasher);
      std::string file_path =
          std::filesystem::path(queue.crash_dir).append(hash);
      WriteToLocalFile(file_path, new_crasher);
    }
  }
}

// TODO(kcc): respect --num_threads.
int MinimizeCrash(ByteSpan crashy_input, const Environment &env,
                  CentipedeCallbacksFactory &callbacks_factory) {
  ScopedCentipedeCallbacks scoped_callback(callbacks_factory, env);
  auto callbacks = scoped_callback.callbacks();

  LOG(INFO) << "MinimizeCrash: trying the original crashy input";

  BatchResult batch_result;
  ByteArray original_crashy_input(crashy_input.begin(), crashy_input.end());
  if (callbacks->Execute(env.binary, {original_crashy_input}, batch_result)) {
    LOG(INFO) << "The original crashy input did not crash; exiting";
    return EXIT_FAILURE;
  }

  MinimizerWorkQueue queue;
  queue.crashers = {original_crashy_input};
  queue.crash_dir = env.MakeCrashReproducerDirPath();
  std::filesystem::create_directory(queue.crash_dir);

  MinimizeCrash(env, callbacks, queue);

  return queue.crashers.size() > 1 ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace centipede
