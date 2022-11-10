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

#ifndef THIRD_PARTY_CENTIPEDE_CALL_GRAPH_H_
#define THIRD_PARTY_CENTIPEDE_CALL_GRAPH_H_

#include <cstddef>
#include <cstdint>
#include <istream>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "./coverage.h"

namespace centipede {

class CallGraph {
 public:
  // Reads in the CfTable from __sancov_cfs section. On error it crashes, if the
  // section is not available, the hash maps will be empty.
  void ReadFromCfTable(const Coverage::CFTable& cf_table,
                       const Coverage::PCTable& pc_table);

  const std::vector<uintptr_t>& GetFunctionCallees(uintptr_t pc) const {
    return call_graph_.at(pc);
  }
  const std::vector<uintptr_t>& GetBasicBlockCallees(uintptr_t pc) const {
    return basic_block_callees_.at(pc);
  }
  const absl::flat_hash_set<uintptr_t>& GetFunctionEntries() const {
    return function_entries_;
  }

  bool IsFunctionEntry(uintptr_t pc) const {
    return function_entries_.contains(pc);
  }

 private:
  // call_graph_: the key is function entry PC and value is all the
  // callees of that function.
  absl::flat_hash_map<uintptr_t, std::vector<uintptr_t>> call_graph_;
  // bb_callees_: the key is a basic block PC and value is all callees in
  // that basic block.
  absl::flat_hash_map<uintptr_t, std::vector<uintptr_t>> basic_block_callees_;
  absl::flat_hash_set<uintptr_t> function_entries_;
};

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_CALL_GRAPH_H_
