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

#ifndef THIRD_PARTY_CENTIPEDE_CONTROL_FLOW_H_
#define THIRD_PARTY_CENTIPEDE_CONTROL_FLOW_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "./logging.h"

namespace centipede {

class SymbolTable;  // To avoid mutual inclusion with symbol_table.h.

// PCInfo is a pair {PC, bit mask with PC flags}.
// See https://clang.llvm.org/docs/SanitizerCoverage.html#pc-table
struct PCInfo {
  enum PCFlags : uintptr_t {
    kFuncEntry = 1 << 0,  // The PC is the function entry block.
  };

  uintptr_t pc;
  uintptr_t flags;

  bool has_flag(PCFlags f) const { return flags & f; }
};

// Array of PCInfo-s.
// PCTable is created by the compiler/linker in the instrumented binary.
// The order of elements is significant: each element corresponds
// to the coverage counter with the same index.
// Every PCInfo that is kFuncEntry is followed by PCInfo-s from the same
// function.
using PCTable = std::vector<PCInfo>;

// Reads the pc table from the binary file at `binary_path`.
// May create a file `tmp_path`, but will delete it afterwards.
// Currently works for
// * binaries linked with :centipede_runner
//     and built with -fsanitize-coverage=pc-table,
// * binaries built with -fsanitize-coverage=trace-pc
PCTable GetPcTableFromBinary(std::string_view binary_path,
                             std::string_view tmp_path);

// Helper for GetPcTableFromBinary,
// for binaries linked with :centipede_runner
// and built with -fsanitize-coverage=pc-table.
// Returns the PCTable that the binary itself reported.
// May create a file `tmp_path`, but will delete it afterwards.
PCTable GetPcTableFromBinaryWithPcTable(std::string_view binary_path,
                                        std::string_view tmp_path);

// Helper for GetPcTableFromBinary,
// for binaries built with -fsanitize-coverage=trace-pc.
// Returns the PCTable reconstructed from `binary_path` with `objdump -d`.
// May create a file `tmp_path`, but will delete it afterwards.
PCTable GetPcTableFromBinaryWithTracePC(std::string_view binary_path,
                                        std::string_view tmp_path);

// PCIndex: an index into the PCTable.
// We use 32-bit int for compactness since PCTable is never too large.
using PCIndex = uint32_t;
// A set of PCIndex-es, order is not important.
using PCIndexVec = std::vector<PCIndex>;

// Array of elements in __sancov_cfs section.
// CFTable is created by the compiler/linker in the instrumented binary.
// https://clang.llvm.org/docs/SanitizerCoverage.html#tracing-control-flow.
using CFTable = std::vector<intptr_t>;

// Reads the control-flow table from the binary file at `binary_path`.
// May create a file `tmp_path`, but will delete it afterwards.
// Currently works for
// * binaries linked with :fuzz_target_runner
//     and built with -fsanitize-coverage=control-flow.
CFTable GetCfTableFromBinary(std::string_view binary_path,
                             std::string_view tmp_path);

class ControlFlowGraph {
 public:
  // Reads form __sancov_cfs section. On error it crashes, if the section is not
  // there, the graph_ will be empty.
  ControlFlowGraph(const CFTable &cf_table, const PCTable &pc_table);

  // Returns the vector of successor PCs for the given basic block PC.
  const std::vector<uintptr_t> &GetSuccessors(uintptr_t basic_block) const;

  // Returns the number of cfg entries.
  size_t size() const { return graph_.size(); }

  // Checks if basic_block is in cfg.
  bool exists(const uintptr_t basic_block) const {
    return graph_.contains(basic_block);
  }

  // Returns cyclomatic complexity of function PC. CHECK-fails if it is not a
  // valid function PC.
  uint32_t GetCyclomaticComplexity(uintptr_t pc) const {
    auto it = function_complexities_.find(pc);
    CHECK(it != function_complexities_.end());
    return it->second;
  }

  // Returns true if the given basic block is function entry.
  bool BlockIsFunctionEntry(PCIndex pc_index) const {
    return pc_index < func_entries_.size() ? func_entries_[pc_index]: false;
  }

  // Returns the idx in pc_table associated with the PC, CHECK-fails if the PC
  // is not in the pc_table.
  PCIndex GetPcIndex(uintptr_t pc) const {
    auto it = pc_index_map_.find(pc);
    CHECK(it != pc_index_map_.end());
    return it->second;
  }

  // Returns true if the PC is in PCTable.
  bool IsInPcTable(uintptr_t pc) const { return pc_index_map_.contains(pc); }

 private:
  // Map from PC to the idx in pc_table.
  absl::flat_hash_map<uintptr_t, PCIndex> pc_index_map_;
  // A vector of size PCTable. func_entries[idx] is true iff means the PC at idx
  // is a function entry.
  std::vector<bool> func_entries_;
  // A map with PC as the keys and vector of PCs as value.
  absl::flat_hash_map<uintptr_t, std::vector<uintptr_t>> graph_;
  // A map from function PC to its calculated cyclomatic complexity. It is
  // to avoid unnecessary calls to ComputeFunctionCyclomaticComplexity.
  absl::flat_hash_map<uintptr_t, uint32_t> function_complexities_;
};

// Computes the Cyclomatic Complexity for the given function,
// https://en.wikipedia.org/wiki/Cyclomatic_complexity.
uint32_t ComputeFunctionCyclomaticComplexity(uintptr_t pc,
                                             const ControlFlowGraph &cfg);

}  // namespace centipede
#endif  // THIRD_PARTY_CENTIPEDE_CONTROL_FLOW_H_
