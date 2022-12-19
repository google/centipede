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

#include <cstddef>
#include <cstdint>

#include "googlemock/include/gmock/gmock.h"
#include "googletest/include/gtest/gtest.h"
#include "./defs.h"
#include "./environment.h"
#include "./execution_result.h"
#include "./logging.h"
#include "./symbol_table.h"
#include "./test_util.h"
#include "./util.h"

namespace centipede {
namespace {

// Mock CFTable representing the following cfg:
//    1
//  /   \
// 2     3
//  \   /
//    4
static const CFTable g_cf_table = {1, 2, 3, 0, 0, 2, 4, 0,
                                   0, 3, 4, 0, 0, 4, 0, 0};
static const PCTable g_pc_table = {
    {1, PCInfo::kFuncEntry}, {2, 0}, {3, 0}, {4, 0}};

TEST(ControlFlow, MakeCfgFromCfTable) {
  ControlFlowGraph cfg(g_cf_table, g_pc_table);
  EXPECT_NE(cfg.size(), 0);

  for (auto &pc : {1, 2, 3, 4}) {
    SCOPED_TRACE(testing::Message() << VV(pc));
    EXPECT_TRUE(cfg.exists(pc));

    // Check that cfg traversal is possible.
    auto succs = cfg.GetSuccessors(pc);
    for (auto &succ : succs) {
      EXPECT_TRUE(cfg.exists(succ));
    }

    EXPECT_THAT(cfg.GetSuccessors(1).size(), 2);
    EXPECT_THAT(cfg.GetSuccessors(2).size(), 1);
    EXPECT_THAT(cfg.GetSuccessors(3).size(), 1);
    EXPECT_TRUE(cfg.GetSuccessors(4).empty());
  }

  CHECK_EQ(cfg.GetPcIndex(1), 0);
  CHECK_EQ(cfg.GetPcIndex(2), 1);
  CHECK_EQ(cfg.GetPcIndex(3), 2);
  CHECK_EQ(cfg.GetPcIndex(4), 3);

  EXPECT_TRUE(cfg.BlockIsFunctionEntry(0));
  EXPECT_FALSE(cfg.BlockIsFunctionEntry(1));
  EXPECT_FALSE(cfg.BlockIsFunctionEntry(2));
  EXPECT_FALSE(cfg.BlockIsFunctionEntry(3));

  auto func_blocks = cfg.GetFunctionBlocks(1);
  EXPECT_EQ(func_blocks.size(), 4);
  EXPECT_TRUE(func_blocks.contains(1));
  EXPECT_TRUE(func_blocks.contains(2));
  EXPECT_TRUE(func_blocks.contains(3));
  EXPECT_TRUE(func_blocks.contains(4));

  EXPECT_EQ(cfg.GetFunctionEntryBlock(1), 1);
  EXPECT_EQ(cfg.GetFunctionEntryBlock(2), 1);
  EXPECT_EQ(cfg.GetFunctionEntryBlock(3), 1);
  EXPECT_EQ(cfg.GetFunctionEntryBlock(4), 1);

  auto dominates = cfg.GetDominates(1);
  EXPECT_NE(std::find(dominates.begin(), dominates.end(), 1), dominates.end());
  EXPECT_NE(std::find(dominates.begin(), dominates.end(), 2), dominates.end());
  EXPECT_NE(std::find(dominates.begin(), dominates.end(), 3), dominates.end());
  EXPECT_NE(std::find(dominates.begin(), dominates.end(), 4), dominates.end());

  dominates = cfg.GetDominates(2);
  EXPECT_NE(std::find(dominates.begin(), dominates.end(), 2), dominates.end());

  dominates = cfg.GetDominates(3);
  EXPECT_NE(std::find(dominates.begin(), dominates.end(), 3), dominates.end());

    dominates = cfg.GetDominates(4);
  EXPECT_NE(std::find(dominates.begin(), dominates.end(), 4), dominates.end());

  EXPECT_THAT(cfg.GetDominates(1).size(), 4);
  EXPECT_THAT(cfg.GetDominates(2).size(), 1);
  EXPECT_THAT(cfg.GetDominates(3).size(), 1);
  EXPECT_THAT(cfg.GetDominates(4).size(), 1);

  CHECK_EQ(cfg.GetCyclomaticComplexity(1), 2);
}

TEST(ControlFlow, ComputeFuncComplexity) {
  static const CFTable g_cf_table1 = {
      1, 2, 3, 0, 0,  // 1 goes to 2 and 3.
      2, 3, 4, 0, 0,  // 2 goes to 3 and 4.
      3, 1, 4, 0, 0,  // 3 goes to 1 and 4.
      4, 0, 0         // 4 goes nowhere.
  };
  static const CFTable g_cf_table2 = {
      1, 0, 0,  // 1 goes nowhere.
  };
  static const CFTable g_cf_table3 = {
      1, 2, 0, 0,  // 1 goes to 2.
      2, 3, 0, 0,  // 2 goes to 3.
      3, 1, 0, 0,  // 3 goes to 1.
  };
  static const CFTable g_cf_table4 = {
      1, 2, 3, 0, 0,  // 1 goes to 2 and 3.
      2, 3, 4, 0, 0,  // 2 goes to 3 and 4.
      3, 0, 0,        // 3 goes nowhere.
      4, 0, 0         // 4 goes nowhere.
  };

  ControlFlowGraph cfg1(g_cf_table1, g_pc_table);
  EXPECT_NE(cfg1.size(), 0);

  ControlFlowGraph cfg2(g_cf_table2, g_pc_table);
  EXPECT_NE(cfg2.size(), 0);

  ControlFlowGraph cfg3(g_cf_table3, g_pc_table);
  EXPECT_NE(cfg3.size(), 0);

  ControlFlowGraph cfg4(g_cf_table4, g_pc_table);
  EXPECT_NE(cfg4.size(), 0);

  EXPECT_EQ(ComputeFunctionCyclomaticComplexity(1, cfg1), 4);
  EXPECT_EQ(ComputeFunctionCyclomaticComplexity(1, cfg2), 1);
  EXPECT_EQ(ComputeFunctionCyclomaticComplexity(1, cfg3), 2);
  EXPECT_EQ(ComputeFunctionCyclomaticComplexity(1, cfg4), 2);
}

// Returns a path for i-th temporary file.
static std::string GetTempFilePath(size_t i) {
  return std::filesystem::path(GetTestTempDir())
      .append(absl::StrCat("coverage_test", i, "-", getpid()));
}

// Returns path to test_fuzz_target.
static std::string GetTargetPath() {
  return GetDataDependencyFilepath("testing/test_fuzz_target");
}

// Returns path to llvm-symbolizer.
static std::string GetLLVMSymbolizerPath() {
  CHECK_EQ(system("which llvm-symbolizer"), EXIT_SUCCESS)
      << "llvm-symbolizer has to be installed and findable via PATH";
  return "llvm-symbolizer";
}

// Tests GetCfTableFromBinary() on test_fuzz_target.
TEST(CFTable, GetCfTable) {
  auto target_path = GetTargetPath();
  std::string tmp_path1 = GetTempFilePath(1);
  std::string tmp_path2 = GetTempFilePath(2);

  // Load the cf table.
  auto cf_table = GetCfTableFromBinary(target_path, tmp_path1);
  LOG(INFO) << VV(target_path) << VV(tmp_path1) << VV(cf_table.size());
  if (cf_table.empty()) {
    LOG(INFO) << "__sancov_cfs is empty.";
    // TODO(navidem): This should be removed once OSS's clang supports
    // control-flow.
    GTEST_SKIP();
  }

  ASSERT_FALSE(
      std::filesystem::exists(tmp_path1.c_str()));  // tmp_path1 was deleted.
  LOG(INFO) << VV(cf_table.size());

  // Load the pc table.
  auto pc_table = GetPcTableFromBinary(target_path, tmp_path1);
  ASSERT_FALSE(
      std::filesystem::exists(tmp_path1.c_str()));  // tmp_path1 was deleted.
  EXPECT_THAT(pc_table.empty(), false);

  // Symbilize pc_table.
  SymbolTable symbols;
  symbols.GetSymbolsFromBinary(pc_table, target_path, GetLLVMSymbolizerPath(),
                               tmp_path1, tmp_path2);
  ASSERT_EQ(symbols.size(), pc_table.size());

  absl::flat_hash_map<uintptr_t, size_t> pc_table_index;
  for (size_t i = 0; i < pc_table.size(); i++) {
    pc_table_index[pc_table[i].pc] = i;
  }

  for (size_t j = 0; j < cf_table.size();) {
    auto current_pc = cf_table[j];
    ++j;
    size_t succ_num = 0;
    size_t callee_num = 0;
    size_t icallee_num = 0;

    // Iterate over successors.
    while (cf_table[j]) {
      ++succ_num;
      ++j;
    }
    ++j;  // Step over the delimeter.

    // Iterate over callees.
    while (cf_table[j]) {
      if (cf_table[j] > 0) ++callee_num;
      if (cf_table[j] < 0) ++icallee_num;
      ++j;
    }
    ++j;  // Step over the delimeter.

    // Determine if current_pc is a function entry.
    if (pc_table_index.contains(current_pc)) {
      size_t index = pc_table_index[current_pc];
      if (pc_table[index].has_flag(PCInfo::kFuncEntry)) {
        const std::string &current_function = symbols.func(index);
        // Check for properties.
        SCOPED_TRACE(testing::Message()
                     << "Checking for " << VV(current_function)
                     << VV(current_pc) << VV(cf_table[j]) << VV(j));
        if (current_function == "SingleEdgeFunc") {
          EXPECT_EQ(succ_num, 0);
          EXPECT_EQ(icallee_num, 0);
          EXPECT_EQ(callee_num, 0);
        } else if (current_function == "MultiEdgeFunc") {
          EXPECT_EQ(succ_num, 2);
          EXPECT_EQ(icallee_num, 0);
          EXPECT_EQ(callee_num, 0);
        } else if (current_function == "IndirectCallFunc") {
          EXPECT_EQ(succ_num, 0);
          EXPECT_EQ(icallee_num, 1);
          EXPECT_EQ(callee_num, 0);
        }
      }
    }
  }
}

}  // namespace

}  // namespace centipede
