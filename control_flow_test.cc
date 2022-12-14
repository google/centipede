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
#include "./coverage.h"
#include "./logging.h"
#include "./test_util.h"

namespace centipede {
namespace {

// Mock CFTable representing the following cfg:
//    1
//  /   \
// 2     3
//  \   /
//    4
static const Coverage::CFTable g_cf_table = {1, 2, 3, 0, 0, 2, 4, 0,
                                             0, 3, 4, 0, 0, 4, 0, 0};
static const Coverage::PCTable g_pc_table = {
  {1, Coverage::PCInfo::kFuncEntry},
  {2, 0},
  {3, 0},
  {4, 0}
};

TEST(ControlFlowGraph, MakeCfgFromCfTable) {
  ControlFlowGraph cfg;
  cfg.ReadFromCfTable(g_cf_table, g_pc_table);
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

  CHECK_EQ(cfg.GetCyclomaticComplexity(1), 2);
}

TEST(FunctionComplexity, ComputeFuncComplexity) {
  static const Coverage::CFTable g_cf_table1 = {
      1, 2, 3, 0, 0,  // 1 goes to 2 and 3.
      2, 3, 4, 0, 0,  // 2 goes to 3 and 4.
      3, 1, 4, 0, 0,  // 3 goes to 1 and 4.
      4, 0, 0         // 4 goes nowhere.
  };
  static const Coverage::CFTable g_cf_table2 = {
      1, 0, 0,  // 1 goes nowhere.
  };
  static const Coverage::CFTable g_cf_table3 = {
      1, 2, 0, 0,  // 1 goes to 2.
      2, 3, 0, 0,  // 2 goes to 3.
      3, 1, 0, 0,  // 3 goes to 1.
  };
  static const Coverage::CFTable g_cf_table4 = {
      1, 2, 3, 0, 0,  // 1 goes to 2 and 3.
      2, 3, 4, 0, 0,  // 2 goes to 3 and 4.
      3, 0, 0,        // 3 goes nowhere.
      4, 0, 0         // 4 goes nowhere.
  };

  ControlFlowGraph cfg1;
  cfg1.ReadFromCfTable(g_cf_table1, g_pc_table);
  EXPECT_NE(cfg1.size(), 0);

  ControlFlowGraph cfg2;
  cfg2.ReadFromCfTable(g_cf_table2, g_pc_table);
  EXPECT_NE(cfg2.size(), 0);

  ControlFlowGraph cfg3;
  cfg3.ReadFromCfTable(g_cf_table3, g_pc_table);
  EXPECT_NE(cfg3.size(), 0);

  ControlFlowGraph cfg4;
  cfg4.ReadFromCfTable(g_cf_table4, g_pc_table);
  EXPECT_NE(cfg4.size(), 0);

  EXPECT_EQ(ComputeFunctionCyclomaticComplexity(1, cfg1), 4);
  EXPECT_EQ(ComputeFunctionCyclomaticComplexity(1, cfg2), 1);
  EXPECT_EQ(ComputeFunctionCyclomaticComplexity(1, cfg3), 2);
  EXPECT_EQ(ComputeFunctionCyclomaticComplexity(1, cfg4), 2);
}

}  // namespace

}  // namespace centipede
