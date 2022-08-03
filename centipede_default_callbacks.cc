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
#include <string>
#include <string_view>
#include <vector>

#include "./centipede_interface.h"
#include "./coverage.h"
#include "./defs.h"
#include "./environment.h"
#include "./logging.h"
#include "./symbol_table.h"
#include "./util.h"

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
  if (custom_mutator_is_usable_ &&
      MutateViaExternalBinary(env_.binary, inputs, mutants)) {
    return;
  }
  // Either no custom mutator, or it failed for some reason.
  byte_array_mutator_.MutateMany(inputs, num_mutants, env_.crossover_level,
                                 mutants);
}

bool CentipedeDefaultCallbacks::PopulateSymbolAndPcTables(
    SymbolTable &symbols, Coverage::PCTable &pc_table) {
  // Running in main thread, create our own temp dir.
  auto tmpdir = TemporaryLocalDirPath();
  if (!std::filesystem::exists(tmpdir)) {
    CreateLocalDirRemovedAtExit(tmpdir);
  }
  std::string pc_table_path = std::filesystem::path(tmpdir).append("pc_table");
  pc_table =
      Coverage::GetPcTableFromBinary(env_.coverage_binary, pc_table_path);
  if (pc_table.empty()) {
    if (env_.require_pc_table) {
      LOG(ERROR) << "Could not get PCTable, exiting (override with "
                    "--require_pc_table=0)";
      return false;
    }
    LOG(WARNING) << "Could not get PCTable, debug symbols will not be used";
    return true;
  }
  std::string tmp1 = std::filesystem::path(tmpdir).append("sym-tmp1");
  std::string tmp2 = std::filesystem::path(tmpdir).append("sym-tmp2");
  symbols.GetSymbolsFromBinary(pc_table, env_.coverage_binary,
                               env_.symbolizer_path, tmp1, tmp2);
  if (symbols.size() != pc_table.size()) {
    LOG(WARNING) << "symbolization failed, debug symbols will not be used";
    pc_table.clear();
  }
  return true;
}

}  // namespace centipede
