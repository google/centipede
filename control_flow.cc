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

#include "./control_flow.h"

#include <queue>

#include "./coverage.h"
#include "./logging.h"

namespace centipede {

void ControlFlowGraph::ReadFromCfTable(const Coverage::CFTable &cf_table,
                                       const Coverage::PCTable &pc_table) {
  for (size_t j = 0; j < cf_table.size();) {
    std::vector<uintptr_t> successors;
    auto curr_pc = cf_table[j];
    ++j;

    // Iterate over successors.
    while (cf_table[j]) {
      successors.push_back(cf_table[j]);
      ++j;
    }
    ++j;  // Step over the delimeter.

    // Record the list of successors
    graph_[curr_pc] = std::move(successors);

    // Iterate over callees.
    while (cf_table[j]) {
      ++j;
    }
    ++j;  // Step over the delimeter.
    CHECK_LE(j, cf_table.size());
  }
  // Calculate cyclomatic complexity for all functions.
  for (Coverage::PCIndex i = 0; i < pc_table.size(); ++i) {
    if (pc_table[i].has_flag(Coverage::PCInfo::kFuncEntry)) {
      uintptr_t func_pc = pc_table[i].pc;
      auto func_comp = ComputeFunctionCyclomaticComplexity(func_pc, *this);
      function_complexities_[func_pc] = func_comp;
    }
  }
}

const std::vector<uintptr_t> &ControlFlowGraph::GetSuccessors(
    uintptr_t basic_block) const {
  auto it = graph_.find(basic_block);
  CHECK(it != graph_.end());
  return it->second;
}

uint32_t ComputeFunctionCyclomaticComplexity(uintptr_t pc,
                                             const ControlFlowGraph &cfg) {
  size_t edge_num = 0, node_num = 0;

  absl::flat_hash_set<uintptr_t> visited_pcs;
  std::queue<uintptr_t> worklist;

  worklist.push(pc);

  while (!worklist.empty()) {
    auto currnet_pc = worklist.front();
    worklist.pop();
    if (!visited_pcs.insert(currnet_pc).second) continue;
    ++node_num;
    for (auto &successor : cfg.GetSuccessors(currnet_pc)) {
      ++edge_num;
      worklist.push(successor);
    }
  }

  return edge_num - node_num + 2;
}

}  // namespace centipede
