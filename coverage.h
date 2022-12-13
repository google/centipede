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

#ifndef THIRD_PARTY_CENTIPEDE_COVERAGE_H_
#define THIRD_PARTY_CENTIPEDE_COVERAGE_H_

#include <stddef.h>

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_set.h"
#include "absl/synchronization/mutex.h"
#include "./util.h"
#include "./logging.h"

namespace centipede {

class SymbolTable;  // To avoid mutual inclusion with symbol_table.h.

// Reads and visualizes the code coverage produced by SanitizerCoverage.
// https://clang.llvm.org/docs/SanitizerCoverage.html
//
// Thread-compatible.
class Coverage {
 public:
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
  static PCTable GetPcTableFromBinary(std::string_view binary_path,
                                      std::string_view tmp_path);

  // Helper for GetPcTableFromBinary,
  // for binaries linked with :centipede_runner
  // and built with -fsanitize-coverage=pc-table.
  // Returns the PCTable that the binary itself reported.
  // May create a file `tmp_path`, but will delete it afterwards.
  static PCTable GetPcTableFromBinaryWithPcTable(std::string_view binary_path,
                                                 std::string_view tmp_path);

  // Helper for GetPcTableFromBinary,
  // for binaries built with -fsanitize-coverage=trace-pc.
  // Returns the PCTable reconstructed from `binary_path` with `objdump -d`.
  // May create a file `tmp_path`, but will delete it afterwards.
  static PCTable GetPcTableFromBinaryWithTracePC(std::string_view binary_path,
                                                 std::string_view tmp_path);

  // PCIndex: an index into the PCTable.
  // We use 32-bit int for compactness since PCTable is never too large.
  using PCIndex = uint32_t;
  // A set of PCIndex-es, order is not important.
  using PCIndexVec = std::vector<PCIndex>;

  // PCTable is a property of the binary.
  // PCIndexVec is the coverage obtained from specific execution(s).
  Coverage(const PCTable &pc_table, const PCIndexVec &pci_vec);

  // Array of elements in __sancov_cfs section.
  // CFTable is created by the compiler/linker in the instrumented binary.
  // https://clang.llvm.org/docs/SanitizerCoverage.html#tracing-control-flow.
  using CFTable = std::vector<intptr_t>;

  // Reads the control-flow table from the binary file at `binary_path`.
  // May create a file `tmp_path`, but will delete it afterwards.
  // Currently works for
  // * binaries linked with :fuzz_target_runner
  //     and built with -fsanitize-coverage=control-flow.
  static CFTable GetCfTableFromBinary(std::string_view binary_path,
                                      std::string_view tmp_path);

  // Prints in human-readable form to `out` using `symbols`.
  void Print(const SymbolTable &symbols, std::ostream &out);

  // Returns true if the function is fully covered. pc_index is for a function
  // entry.
  bool FunctionIsFullyCovered(PCIndex pc_index) const {
    CHECK(func_entries_[pc_index]);
    return fully_covered_funcs_vec_[pc_index];
  }
  // Returns true if the given basic block is covered. pc_index is for any BB.
  bool BlockIsCovered(PCIndex pc_index) const {
    return covered_pcs_vec_[pc_index];
  }

 private:
  // A vector of size PCTable. func_entries[idx] is true iff means the PC at idx
  // is a function entry.
  std::vector<bool> func_entries_;
  // Vector of fully covered functions i.e. functions with all edges covered.
  // A Function is represented by its entry block's PCIndex.
  // TODO(kcc): fix private variables' name to match the code style.
  PCIndexVec fully_covered_funcs;
  // A vector of size PCTable. fully_covered_funcs_vec[idx] is true iff the PC
  // at idx is an entry block of a fully covered function.
  std::vector<bool> fully_covered_funcs_vec_;
  // A vector of size PCTable. covered_pcs_vec[idx] is true iff the PC at idx is
  // covered.
  std::vector<bool> covered_pcs_vec_;
  // Same as `fully_covered_funcs`, but for functions with no edges covered.
  PCIndexVec uncovered_funcs;
  // Partially covered function: function with some, but not all, edges covered.
  // Thus we can represent it as two vectors of PCIndex: covered and uncovered.
  struct PartiallyCoveredFunction {
    PCIndexVec covered;    // Non-empty, covered[0] is function entry.
    PCIndexVec uncovered;  // Non-empty.
  };
  std::vector<PartiallyCoveredFunction> partially_covered_funcs;
};

// Iterates `pc_table`, calls `callback` on every pair {beg, end}, such that
// pc_table[beg] is PCInfo::kFuncEntry, and pc_table[beg + 1 : end] are not.
template <typename Callback>
void IteratePcTableFunctions(const Coverage::PCTable &pc_table,
                             Callback callback) {
  for (size_t beg = 0, n = pc_table.size(); beg < n;) {
    if (pc_table[beg].has_flag(Coverage::PCInfo::kFuncEntry)) {
      size_t end = beg + 1;
      while (end < n && !pc_table[end].has_flag(Coverage::PCInfo::kFuncEntry)) {
        ++end;
      }
      callback(beg, end);
      beg = end;
    }
  }
}

// CoverageLogger helps to log coverage locations once for each location.
// CoverageLogger is thread-safe.
class CoverageLogger {
 public:
  // CTOR.
  // Lifetimes of `pc_table` and `symbols` should be longer than for `this`.
  CoverageLogger(const Coverage::PCTable &pc_table, const SymbolTable &symbols)
      : pc_table_(pc_table), symbols_(symbols) {}

  // Checks if `pc_index` or its symbolized description was observed before.
  // If yes, returns empty string.
  // If this is the first observation, returns a symbolized description.
  // If symbolization is not available, returns a non-symbolized description.
  std::string ObserveAndDescribeIfNew(Coverage::PCIndex pc_index);

 private:
  const Coverage::PCTable &pc_table_;
  const SymbolTable &symbols_;

  absl::Mutex mu_;
  absl::flat_hash_set<Coverage::PCIndex> observed_indices_ ABSL_GUARDED_BY(mu_);
  absl::flat_hash_set<std::string> observed_descriptions_ ABSL_GUARDED_BY(mu_);
};

// FunctionFilter maps a set of function names to a set of features.
class FunctionFilter {
 public:
  // Initialize the filter.
  // `functions_to_filter` is a comma-separated list of function names.
  // If a function name is found in `symbols`, the PCs from that function
  // will be filtered.
  FunctionFilter(std::string_view functions_to_filter,
                 const SymbolTable &symbols);

  // Returns true if
  // * some of the `features` is from feature_domains::k8bitCounters
  //   and belong to a filtered function.
  // * either `functions_to_filter` or `symbols` passed to CTOR was empty.
  bool filter(const FeatureVec &features) const;

  // Counts PCs that belong to filtered functions. Test-only.
  size_t count() const { return std::count(pcs_.begin(), pcs_.end(), 1); }

 private:
  // pcs_[idx]==1 means that the PC at idx belongs to the filtered function.
  // We don't use vector<bool> for performance.
  // We don't use a hash set, because CPU is more important here than RAM.
  std::vector<uint8_t> pcs_;
};

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_COVERAGE_H_
