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

#include <cstddef>
#include <cstdint>

#include "./runner_interface.h"

// This is the header-less interface of libFuzzer, see
// https://llvm.org/docs/LibFuzzer.html.
extern "C" {
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
__attribute__((weak)) int LLVMFuzzerInitialize(int *argc, char ***argv);
__attribute__((weak)) size_t LLVMFuzzerCustomMutator(uint8_t *data, size_t size,
                                                     size_t max_size,
                                                     unsigned int seed);
__attribute__((weak)) size_t LLVMFuzzerCustomCrossOver(
    const uint8_t *data1, size_t size1, const uint8_t *data2, size_t size2,
    uint8_t *out, size_t max_out_size, unsigned int seed);
}  // extern "C"

// Returns CentipedeRunnerMain() with the default fuzzer callbacks.
int main(int argc, char **argv) {
  return CentipedeRunnerMain(argc, argv, LLVMFuzzerTestOneInput,
                             LLVMFuzzerInitialize, LLVMFuzzerCustomMutator,
                             LLVMFuzzerCustomCrossOver);
}