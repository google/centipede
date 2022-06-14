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

#include "./coverage.h"

#include <stdio.h>
#include <unistd.h>

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>  // NOLINT
#include <vector>

#include "devtools/build/runtime/get_runfiles_dir.h"
#include "testing/base/public/gmock.h"
#include "testing/base/public/gunit.h"
#include "absl/strings/str_cat.h"
#include "./centipede_interface.h"
#include "./defs.h"
#include "./environment.h"
#include "./execution_result.h"
#include "./feature.h"
#include "./logging.h"
#include "./symbol_table.h"
#include "./util.h"

namespace centipede {
namespace {

// llvm-symbolizer output for a binary with 3 functions:
// A, BB, CCC.
// A and BB have one control flow edge each.
// CCC has 3 edges.
const char *symbolizer_output =
    "A\n"
    "a.cc:1:0\n"
    "\n"
    "BB\n"
    "bb.cc:1:0\n"
    "\n"
    "CCC\n"
    "ccc.cc:1:0\n"
    "\n"
    "CCC\n"
    "ccc.cc:2:0\n"
    "\n"
    "CCC\n"
    "ccc.cc:3:0\n"
    "\n"
    "CCC\n"
    "ccc.cc:3:0\n"  // same as the previous entry
    "\n";

// PCTable that corresponds to symbolizer_output above.
// PCs are not used and are zeros.
static const Coverage::PCTable pc_table = {
    {0, Coverage::PCInfo::kFuncEntry},
    {0, Coverage::PCInfo::kFuncEntry},
    {0, Coverage::PCInfo::kFuncEntry},
    {0, 0},
    {0, 0},
    {0, 0},
};

// Tests Coverage and SymbolTable together.
TEST(Coverage, SymbolTable) {
  // Initialize and test SymbolTable.
  SymbolTable symbols;
  std::istringstream iss(symbolizer_output);
  symbols.ReadFromLLVMSymbolizer(iss);
  EXPECT_EQ(symbols.size(), 6U);
  EXPECT_EQ(symbols.func(1), "BB");
  EXPECT_EQ(symbols.location(2), "ccc.cc:1:0");
  EXPECT_EQ(symbols.full_description(0), "A a.cc:1:0");
  EXPECT_EQ(symbols.full_description(4), "CCC ccc.cc:3:0");

  {
    // Tests coverage output for PCIndexVec = {0, 2},
    // i.e. the covered edges are 'A' and the entry of 'CCC'.
    Coverage cov(pc_table, {0, 2});
    cov.Print(symbols, std::cout);
    std::ostringstream os;
    cov.Print(symbols, os);
    std::string str = os.str();
    EXPECT_THAT(str, testing::HasSubstr("FULL: A a.cc:1:0"));
    EXPECT_THAT(str, testing::HasSubstr("NONE: BB bb.cc:1:0"));
    EXPECT_THAT(str, testing::HasSubstr("PARTIAL: CCC ccc.cc:1:0"));
    EXPECT_THAT(str, testing::HasSubstr("+ CCC ccc.cc:1:0"));
    EXPECT_THAT(str, testing::HasSubstr("- CCC ccc.cc:2:0"));
    EXPECT_THAT(str, testing::HasSubstr("- CCC ccc.cc:3:0"));
  }
  {
    // Same as above, but for PCIndexVec = {1, 2, 3},
    Coverage cov(pc_table, {1, 2, 3});
    std::ostringstream os;
    cov.Print(symbols, os);
    std::string str = os.str();
    EXPECT_THAT(str, testing::HasSubstr("FULL: BB bb.cc:1:0"));
    EXPECT_THAT(str, testing::HasSubstr("NONE: A a.cc:1:0"));
    EXPECT_THAT(str, testing::HasSubstr("PARTIAL: CCC ccc.cc:1:0"));
    EXPECT_THAT(str, testing::HasSubstr("+ CCC ccc.cc:1:0"));
    EXPECT_THAT(str, testing::HasSubstr("+ CCC ccc.cc:2:0"));
    EXPECT_THAT(str, testing::HasSubstr("- CCC ccc.cc:3:0"));
  }
}

TEST(Coverage, CoverageLogger) {
  SymbolTable symbols;
  std::istringstream iss(symbolizer_output);
  symbols.ReadFromLLVMSymbolizer(iss);
  CoverageLogger logger(pc_table, symbols);
  // First time logging pc_index=0.
  EXPECT_EQ(logger.ObserveAndDescribeIfNew(0), "FUNC: A a.cc:1:0");
  // Second time logger pc_index=0.
  EXPECT_EQ(logger.ObserveAndDescribeIfNew(0), "");
  // First time logging pc_index=4.
  EXPECT_EQ(logger.ObserveAndDescribeIfNew(4), "EDGE: CCC ccc.cc:3:0");
  // First time logging pc_index=5, but it produces the same description as
  // pc_index=4, and so the result is empty.
  EXPECT_EQ(logger.ObserveAndDescribeIfNew(5), "");

  // Logging with pc_index out of bounds. Second time gives empty result.
  EXPECT_EQ(logger.ObserveAndDescribeIfNew(42), "FUNC/EDGE index: 42");
  EXPECT_EQ(logger.ObserveAndDescribeIfNew(42), "");

  CoverageLogger concurrently_used_logger(pc_table, symbols);
  auto cb = [&]() {
    for (int i = 0; i < 1000; i++) {
      Coverage::PCIndex pc_index = i % pc_table.size();
      logger.ObserveAndDescribeIfNew(pc_index);
    }
  };
  std::thread t1(cb), t2(cb);
  t1.join();
  t2.join();
}

// Returns a path for i-th temporary file.
static std::string GetTempFilePath(size_t i) {
  return std::filesystem::path(GetTestTempDir())
      .append(absl::StrCat("coverage_test", i, "-", getpid()));
}

// Returns path to test_fuzz_target_sancov.
static std::string GetTargetPath() {
  return devtools_build::GetDataDependencyFilepath(
      "google3/third_party/centipede/testing/test_fuzz_target_sancov");
}

// Returns path to test_fuzz_target_trace_pc.
static std::string GetTracePCTargetPath() {
  return devtools_build::GetDataDependencyFilepath(
      "google3/third_party/centipede/testing/test_fuzz_target_trace_pc");
}

// Returns path to path_stress_fuzz_target_sancov.
static std::string GetPathStressTargetPath() {
  return devtools_build::GetDataDependencyFilepath(
      "google3/third_party/centipede/testing/path_stress_fuzz_target_sancov");
}

// Returns path to threaded_fuzz_target_sancov.
static std::string GetThreadedTargetPath() {
  return devtools_build::GetDataDependencyFilepath(
      "google3/third_party/centipede/testing/threaded_fuzz_target_sancov");
}

// Returns path to llvm-symbolizer.
static std::string GetLLVMSymbolizerPath() {
  return devtools_build::GetDataDependencyFilepath(
      "google3/third_party/crosstool/google3_users/llvm-symbolizer");
}

static void SymbolizeBinary(std::string_view target_path, bool use_trace_pc) {
  std::string tmp_path1 = GetTempFilePath(1);
  std::string tmp_path2 = GetTempFilePath(2);

  // Load the pc table.
  auto pc_table =
      use_trace_pc
          ? Coverage::GetPcTableFromBinaryWithTracePC(target_path, tmp_path1)
          : Coverage::GetPcTableFromBinaryWithPcTable(target_path, tmp_path1);
  EXPECT_EQ(fopen(tmp_path1.c_str(), "r"), nullptr);  // tmp_path1 was deleted.
  LOG(INFO) << "pc_table.size(): " << pc_table.size();
  // Check that it's not empty.
  EXPECT_NE(pc_table.size(), 0);
  // Check that the first PCInfo corresponds to a kFuncEntry.
  EXPECT_TRUE(pc_table[0].has_flag(Coverage::PCInfo::kFuncEntry));

  // Test the symbols.
  SymbolTable symbols;
  symbols.GetSymbolsFromBinary(pc_table, target_path, GetLLVMSymbolizerPath(),
                               tmp_path1, tmp_path2);
  EXPECT_EQ(fopen(tmp_path1.c_str(), "r"), nullptr);  // tmp_path1 was deleted.
  EXPECT_EQ(fopen(tmp_path2.c_str(), "r"), nullptr);  // tmp_path2 was deleted.
  EXPECT_EQ(symbols.size(), pc_table.size());

  bool has_llvm_fuzzer_test_one_input = false;
  size_t single_edge_func_num_edges = 0;
  size_t multi_edge_func_num_edges = 0;
  // Iterate all symbols, verify that we:
  //  * Don't have main (coverage instrumentation is disabled for main).
  //  * Have LLVMFuzzerTestOneInput with the correct location.
  //  * Have one edge for SingleEdgeFunc.
  //  * Have several edges for MultiEdgeFunc.
  for (size_t i = 0; i < symbols.size(); i++) {
    bool is_func_entry = pc_table[i].has_flag(Coverage::PCInfo::kFuncEntry);
    if (is_func_entry) {
      LOG(INFO) << symbols.full_description(i);
    }
    single_edge_func_num_edges += symbols.func(i) == "SingleEdgeFunc";
    multi_edge_func_num_edges += symbols.func(i) == "MultiEdgeFunc";
    EXPECT_NE(symbols.func(i), "main");
    if (is_func_entry && symbols.func(i) == "LLVMFuzzerTestOneInput") {
      // This is a function entry block for LLVMFuzzerTestOneInput.
      has_llvm_fuzzer_test_one_input = true;
      EXPECT_THAT(symbols.location(i),
                  testing::StartsWith(
                      "third_party/centipede/testing/test_fuzz_target.cc:53"));
    }
  }
  EXPECT_TRUE(has_llvm_fuzzer_test_one_input);
  EXPECT_EQ(single_edge_func_num_edges, 1);
  EXPECT_GT(multi_edge_func_num_edges, 1);
}

// Tests GetPcTableFromBinary() and SymbolTable on test_fuzz_target_sancov.
TEST(Coverage, GetPcTableFromBinary_And_SymbolTable_PCTable) {
  EXPECT_NO_FATAL_FAILURE(
      SymbolizeBinary(GetTargetPath(), /*use_trace_pc=*/false));
}

// Tests GetPcTableFromBinary() and SymbolTable on test_fuzz_target_trace_pc.
TEST(Coverage, GetPcTableFromBinary_And_SymbolTable_TracePC) {
  EXPECT_NO_FATAL_FAILURE(
      SymbolizeBinary(GetTracePCTargetPath(), /*use_trace_pc=*/true));
}

// A simple CentipedeCallbacks derivative for this test.
class TestCallbacks : public CentipedeCallbacks {
 public:
  TestCallbacks(const Environment &env) : CentipedeCallbacks(env) {}
  bool Execute(std::string_view binary, const std::vector<ByteArray> &inputs,
               BatchResult &batch_result) override {
    int result =
        ExecuteCentipedeSancovBinaryWithShmem(binary, inputs, batch_result);
    CHECK_EQ(EXIT_SUCCESS, result);
    return true;
  }
  void Mutate(std::vector<ByteArray> &inputs) override {}
};

// Runs all `inputs`, returns FeatureVec for every input.
// `env` defines what target is executed and with what flags.
static std::vector<FeatureVec> RunInputsAndCollectCoverage(
    const Environment &env,
    const std::vector<std::string> &inputs) {
  TestCallbacks CBs(env);
  std::filesystem::create_directories(TemporaryLocalDirPath());

  // Repackage string inputs into ByteArray inputs.
  std::vector<ByteArray> byte_array_inputs;
  for (auto &string_input : inputs) {
    byte_array_inputs.push_back(
        ByteArray(string_input.begin(), string_input.end()));
  }
  BatchResult batch_result;
  // Run.
  CBs.Execute(env.binary, byte_array_inputs, batch_result);

  // Cleanup.
  std::filesystem::remove_all(TemporaryLocalDirPath());
  // Repackage execution results into a vector of FeatureVec.
  std::vector<FeatureVec> res;
  for (const auto &er : batch_result.results()) {
    res.push_back(er.features());
  }
  return res;
}

// Tests coverage collection on test_fuzz_target_sancov
// using two inputs that trigger different code paths.
TEST(Coverage, CoverageFeatures) {
  // Prepare the inputs.
  Environment env;
  env.binary = GetTargetPath();
  auto features = RunInputsAndCollectCoverage(env, {"func1", "func2-A"});
  EXPECT_EQ(features.size(), 2);
  EXPECT_NE(features[0], features[1]);
  // Get pc_table and symbols.
  auto pc_table =
      Coverage::GetPcTableFromBinary(GetTargetPath(), GetTempFilePath(0));
  SymbolTable symbols;
  symbols.GetSymbolsFromBinary(pc_table, GetTargetPath(),
                               GetLLVMSymbolizerPath(), GetTempFilePath(0),
                               GetTempFilePath(1));
  // pc_table and symbols should have the same size.
  EXPECT_EQ(pc_table.size(), symbols.size());
  // Check what's covered.
  // Both inputs should cover LLVMFuzzerTestOneInput.
  // Input[0] should cover SingleEdgeFunc and not MultiEdgeFunc.
  // Input[1] - the other way around.
  for (size_t input_idx = 0; input_idx < 2; input_idx++) {
    size_t llvm_fuzzer_test_one_input_num_edges = 0;
    size_t single_edge_func_num_edges = 0;
    size_t multi_edge_func_num_edges = 0;
    for (auto feature : features[input_idx]) {
      if (!FeatureDomains::k8bitCounters.Contains(feature)) continue;
      auto pc_index = Convert8bitCounterFeatureToPcIndex(feature);
      single_edge_func_num_edges += symbols.func(pc_index) == "SingleEdgeFunc";
      multi_edge_func_num_edges += symbols.func(pc_index) == "MultiEdgeFunc";
      llvm_fuzzer_test_one_input_num_edges +=
          symbols.func(pc_index) == "LLVMFuzzerTestOneInput";
    }
    EXPECT_GT(llvm_fuzzer_test_one_input_num_edges, 1);
    if (input_idx == 0) {
      // This input calls SingleEdgeFunc, but not MultiEdgeFunc.
      EXPECT_EQ(single_edge_func_num_edges, 1);
      EXPECT_EQ(multi_edge_func_num_edges, 0);
    } else {
      // This input calls MultiEdgeFunc, but not SingleEdgeFunc.
      EXPECT_EQ(single_edge_func_num_edges, 0);
      EXPECT_GT(multi_edge_func_num_edges, 1);
    }
  }
}

static FeatureVec ExtractDomainFeatures(const FeatureVec &features,
                                        const FeatureDomains::Domain &domain) {
  FeatureVec result;
  for (auto feature : features) {
    if (domain.Contains(feature)) {
      result.push_back(feature);
    }
  }
  return result;
}

// Tests data flow instrumentation and feature collection.
TEST(Coverage, DataFlowFeatures) {
  Environment env;
  env.binary = GetTargetPath();
  auto features_g = RunInputsAndCollectCoverage(env, {"glob1", "glob2"});
  auto features_c = RunInputsAndCollectCoverage(env, {"cons1", "cons2"});
  for (auto &features : {features_g, features_c}) {
    EXPECT_EQ(features.size(), 2);
    // Dataflow features should be different.
    EXPECT_NE(ExtractDomainFeatures(features[0], FeatureDomains::kDataFlow),
              ExtractDomainFeatures(features[1], FeatureDomains::kDataFlow));
    // But control flow features should be the same.
    EXPECT_EQ(
        ExtractDomainFeatures(features[0], FeatureDomains::k8bitCounters),
        ExtractDomainFeatures(features[1], FeatureDomains::k8bitCounters));
  }
}

// Tests feature collection for counters (--use_counter_features).
TEST(Coverage, CounterFeatures) {
  Environment env;
  env.binary = GetTargetPath();

  // Inputs that generate the same PC coverage but different counters.
  std::vector<std::string> inputs = {"cnt\x01", "cnt\x02", "cnt\x04",
                                     "cnt\x08", "cnt\x10"};
  const size_t n = inputs.size();

  // Run with use_counter_features = true.
  env.use_counter_features = true;
  auto features = RunInputsAndCollectCoverage(env, inputs);
  EXPECT_EQ(features.size(), n);
  // Counter features should be different.
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      EXPECT_NE(
          ExtractDomainFeatures(features[i], FeatureDomains::k8bitCounters),
          ExtractDomainFeatures(features[j], FeatureDomains::k8bitCounters));
    }
  }

  // Run with use_counter_features = false.
  env.use_counter_features = false;
  features = RunInputsAndCollectCoverage(env, inputs);
  EXPECT_EQ(features.size(), n);
  // Counter features should be the same now.
  for (size_t i = 0; i < n; ++i) {
    for (size_t j = i + 1; j < n; ++j) {
      EXPECT_EQ(
          ExtractDomainFeatures(features[i], FeatureDomains::k8bitCounters),
          ExtractDomainFeatures(features[j], FeatureDomains::k8bitCounters));
    }
  }
}

// Tests CMP tracing and feature collection.
TEST(Coverage, CMPFeatures) {
  Environment env;
  env.binary = GetTargetPath();
  auto features =
      RunInputsAndCollectCoverage(env, {"cmpAAAAAAAA", "cmpAAAABBBB"});
  EXPECT_EQ(features.size(), 2);
  // CMP features should be different.
  EXPECT_NE(ExtractDomainFeatures(features[0], FeatureDomains::kCMP),
            ExtractDomainFeatures(features[1], FeatureDomains::kCMP));
  // But control flow features should be the same.
  EXPECT_EQ(ExtractDomainFeatures(features[0], FeatureDomains::k8bitCounters),
            ExtractDomainFeatures(features[1], FeatureDomains::k8bitCounters));
}

// Tests memcmp interceptor.
TEST(Coverage, CMPFeaturesFromMemcmp) {
  Environment env;
  env.binary = GetTargetPath();
  auto features =
      RunInputsAndCollectCoverage(env, {"mcmpAAAAAAAA", "mcmpAAAABBBB"});
  EXPECT_EQ(features.size(), 2);
  // CMP features should be different.
  EXPECT_NE(ExtractDomainFeatures(features[0], FeatureDomains::kCMP),
            ExtractDomainFeatures(features[1], FeatureDomains::kCMP));
  // But control flow features should be the same.
  EXPECT_EQ(ExtractDomainFeatures(features[0], FeatureDomains::k8bitCounters),
            ExtractDomainFeatures(features[1], FeatureDomains::k8bitCounters));
}

TEST(Coverage, PathFeatures) {
  Environment env;
  env.binary = GetPathStressTargetPath();
  env.use_path_features = true;
  // Inputs "129" and "219" generate different call sequences but exactly the
  // same edge coverage. This test verifies that we can capture this.
  auto features = RunInputsAndCollectCoverage(env, {"129", "219"});
  EXPECT_EQ(features.size(), 2);
  // Path features should be different.
  EXPECT_NE(ExtractDomainFeatures(features[0], FeatureDomains::kBoundedPath),
            ExtractDomainFeatures(features[1], FeatureDomains::kBoundedPath));
  // But control flow features should be the same.
  EXPECT_EQ(ExtractDomainFeatures(features[0], FeatureDomains::k8bitCounters),
            ExtractDomainFeatures(features[1], FeatureDomains::k8bitCounters));
}

TEST(Coverage, FunctionFilter) {
  // initialize coverage data.
  Coverage::PCTable pc_table =
      Coverage::GetPcTableFromBinary(GetTargetPath(), GetTempFilePath(0));
  SymbolTable symbols;
  symbols.GetSymbolsFromBinary(pc_table, GetTargetPath(),
                               GetLLVMSymbolizerPath(), GetTempFilePath(0),
                               GetTempFilePath(1));
  // empty filter.
  FunctionFilter empty_filter("", symbols);
  EXPECT_EQ(empty_filter.count(), 0);

  // Single-function filter. The function has one PC.
  FunctionFilter sing_edge_func_filter("SingleEdgeFunc", symbols);
  EXPECT_EQ(sing_edge_func_filter.count(), 1);

  // Another single-function filter. This function has several PCs.
  FunctionFilter multi_edge_func_filter("MultiEdgeFunc", symbols);
  EXPECT_GT(multi_edge_func_filter.count(), 1);

  // Two-function-filter.
  FunctionFilter both_func_filter("MultiEdgeFunc,SingleEdgeFunc", symbols);
  EXPECT_GT(both_func_filter.count(), multi_edge_func_filter.count());

  // Collect features from the test target by running 3 different inputs.
  Environment env;
  env.binary = GetTargetPath();
  std::vector<FeatureVec> features =
      RunInputsAndCollectCoverage(env, {"func1", "func2-A", "other"});
  EXPECT_EQ(features.size(), 3);
  auto &single = features[0];
  auto &multi = features[1];
  auto &other = features[2];

  // Check the features against the different filters.
  EXPECT_TRUE(empty_filter.filter(single));
  EXPECT_TRUE(empty_filter.filter(multi));
  EXPECT_TRUE(empty_filter.filter(other));

  EXPECT_TRUE(sing_edge_func_filter.filter(single));
  EXPECT_FALSE(sing_edge_func_filter.filter(multi));
  EXPECT_FALSE(sing_edge_func_filter.filter(other));

  EXPECT_FALSE(multi_edge_func_filter.filter(single));
  EXPECT_TRUE(multi_edge_func_filter.filter(multi));
  EXPECT_FALSE(multi_edge_func_filter.filter(other));

  EXPECT_TRUE(both_func_filter.filter(single));
  EXPECT_TRUE(both_func_filter.filter(multi));
  EXPECT_FALSE(both_func_filter.filter(other));
}

TEST(Coverage, ThreadedTest) {
  Environment env;
  env.use_path_features = true;
  env.binary = GetThreadedTargetPath();

  std::vector<FeatureVec> features =
      RunInputsAndCollectCoverage(env, {"f", "fu", "fuz", "fuzz"});
  EXPECT_EQ(features.size(), 4);
  // For several pairs of inputs, check that their features in
  // k8bitCounters and kBoundedPath are different.
  for (size_t idx0 = 0; idx0 < 3; ++idx0) {
    for (size_t idx1 = idx0 + 1; idx1 < 4; ++idx1) {
      EXPECT_NE(
          ExtractDomainFeatures(features[idx0], FeatureDomains::k8bitCounters),
          ExtractDomainFeatures(features[idx1], FeatureDomains::k8bitCounters));
      EXPECT_NE(
          ExtractDomainFeatures(features[idx0], FeatureDomains::kBoundedPath),
          ExtractDomainFeatures(features[idx1], FeatureDomains::kBoundedPath));
    }
  }
}

}  // namespace
}  // namespace centipede
