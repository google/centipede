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

#include "./centipede_default_callbacks.h"

#include <cstddef>
#include <string_view>
#include <vector>

#include "./centipede_interface.h"
#include "./defs.h"
#include "./environment.h"
#include "./logging.h"

namespace centipede {

CentipedeDefaultCallbacks::CentipedeDefaultCallbacks(const Environment &env)
    : CentipedeCallbacks(env) {
  for (const auto &dictionary_path : env_.dictionary) {
    LoadDictionary(dictionary_path);
  }
  // Check if a custom mutator is available in the target.
  std::vector<ByteArray> mutants(1);
  if (MutateViaExternalBinary(env_.binary, {{0}}, mutants)) {
    custom_mutator_is_usable_ = true;
    LOG(INFO) << "Custom mutator detected in the target, will use it";
  } else {
    LOG(INFO) << "No custom mutator detected in the target";
  }
}

bool CentipedeDefaultCallbacks::Execute(std::string_view binary,
                                        const std::vector<ByteArray> &inputs,
                                        BatchResult &batch_result) {
  return ExecuteCentipedeSancovBinaryWithShmem(binary, inputs, batch_result) ==
         0;
}

void CentipedeDefaultCallbacks::Mutate(const std::vector<ByteArray> &inputs,
                                       size_t num_mutants,
                                       std::vector<ByteArray> &mutants) {
  mutants.resize(num_mutants);
  if (custom_mutator_is_usable_) {
    CHECK(MutateViaExternalBinary(env_.binary, inputs, mutants))
        << "Custom mutator in " << env_.binary << " failed, aborting";
    return;
  }
  // No custom mutator.
  byte_array_mutator_.MutateMany(inputs, num_mutants, env_.crossover_level,
                                 mutants);
}

}  // namespace centipede
