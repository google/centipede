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

#include <string.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/synchronization/mutex.h"
#include "./command.h"
#include "./defs.h"
#include "./logging.h"
#include "./symbol_table.h"
#include "./util.h"

namespace centipede {

Coverage::Coverage(const PCTable &pc_table, const PCIndexVec &pci_vec) {
  CHECK_LT(pc_table.size(), std::numeric_limits<PCIndex>::max());
  absl::flat_hash_set<PCIndex> covered_pcs(pci_vec.begin(), pci_vec.end());
  // Iterate though all the pc_table entries.
  // The first one is some function's kFuncEntry.
  // Then find the next kFuncEntry or the table end.
  // Everything in between corresponds to the current function.
  // For fully (un)covered functions, add their entry PCIndex
  // to fully_covered_funcs or uncovered_funcs correspondingly.
  // For all others add them to partially_covered_funcs.
  for (size_t this_func = 0; this_func < pc_table.size();) {
    CHECK(pc_table[this_func].has_flag(PCInfo::kFuncEntry));
    // Find next entry.
    size_t next_func = this_func + 1;
    while (next_func < pc_table.size() &&
           !pc_table[next_func].has_flag(PCInfo::kFuncEntry)) {
      next_func++;
    }
    // Collect covered and uncovered indices.
    PartiallyCoveredFunction pcf;
    for (size_t i = this_func; i < next_func; i++) {
      if (covered_pcs.contains(i)) {
        pcf.covered.push_back(i);
      } else {
        pcf.uncovered.push_back(i);
      }
    }
    // Put this function into one of
    // {fully_covered_funcs, uncovered_funcs, partially_covered_funcs}
    size_t num_func_pcs = next_func - this_func;
    if (num_func_pcs == pcf.covered.size()) {
      fully_covered_funcs.push_back(this_func);
    } else if (pcf.covered.empty()) {
      uncovered_funcs.push_back(this_func);
    } else {
      CHECK(!pcf.covered.empty());
      CHECK(!pcf.uncovered.empty());
      CHECK_EQ(pcf.covered.size() + pcf.uncovered.size(), num_func_pcs);
      partially_covered_funcs.push_back(pcf);
    }
    // Move to the next function.
    this_func = next_func;
  }
}

void Coverage::Print(const SymbolTable &symbols, std::ostream &out) {
  // Print symbolized function names for all covered functions.
  for (auto pc_index : fully_covered_funcs) {
    out << "FULL: " << symbols.full_description(pc_index) << "\n";
  }
  // Same for uncovered functions.
  for (auto pc_index : uncovered_funcs) {
    out << "NONE: " << symbols.full_description(pc_index) << "\n";
  }
  // For every partially covered function, first print its name,
  // then print its covered edges, then uncovered edges.
  for (auto &pcf : partially_covered_funcs) {
    out << "PARTIAL: " << symbols.full_description(pcf.covered[0]) << "\n";
    for (auto pc_index : pcf.covered) {
      out << "  + " << symbols.full_description(pc_index) << "\n";
    }
    for (auto pc_index : pcf.uncovered) {
      out << "  - " << symbols.full_description(pc_index) << "\n";
    }
  }
}

Coverage::PCTable Coverage::GetPcTableFromBinary(std::string_view binary_path,
                                                 std::string_view tmp_path) {
  Coverage::PCTable res =
      GetPcTableFromBinaryWithPcTable(binary_path, tmp_path);
  if (res.empty()) {
    // Fall back to trace-pc.
    res = GetPcTableFromBinaryWithTracePC(binary_path, tmp_path);
  }
  return res;
}

Coverage::PCTable Coverage::GetPcTableFromBinaryWithPcTable(
    std::string_view binary_path, std::string_view tmp_path) {
  Command cmd(binary_path, {},
              {absl::StrCat("CENTIPEDE_RUNNER_FLAGS=:dump_pc_table:arg1=",
                            tmp_path, ":")},
              "/dev/null", "/dev/null");
  int system_exit_code = cmd.Execute();
  if (system_exit_code) {
    LOG(INFO) << "system() for " << binary_path
              << " with --dump_pc_table failed: " << VV(system_exit_code);
    return {};
  }
  ByteArray pc_infos_as_bytes;
  ReadFromLocalFile(tmp_path, pc_infos_as_bytes);
  std::filesystem::remove(tmp_path);
  CHECK_EQ(pc_infos_as_bytes.size() % sizeof(PCInfo), 0);
  size_t pc_table_size = pc_infos_as_bytes.size() / sizeof(PCInfo);
  const auto* pc_infos = reinterpret_cast<PCInfo*>(pc_infos_as_bytes.data());
  PCTable pc_table{pc_infos, pc_infos + pc_table_size};
  CHECK_EQ(pc_table.size(), pc_table_size);
  return pc_table;
}

//---------------------- NewCoverageLogger
std::string CoverageLogger::ObserveAndDescribeIfNew(
    Coverage::PCIndex pc_index) {
  if (pc_table_.empty()) return "";  // Fast-path return (symbolization is off).
  absl::MutexLock l(&mu_);
  if (!observed_indices_.insert(pc_index).second) return "";
  std::ostringstream os;
  if (pc_index >= pc_table_.size()) {
    os << "FUNC/EDGE index: " << pc_index;
  } else {
    os << (pc_table_[pc_index].has_flag(Coverage::PCInfo::kFuncEntry)
               ? "FUNC: "
               : "EDGE: ");
    os << symbols_.full_description(pc_index);
    if (!observed_descriptions_.insert(os.str()).second) return "";
  }
  return os.str();
}

Coverage::PCTable Coverage::GetPcTableFromBinaryWithTracePC(
    std::string_view binary_path, std::string_view tmp_path) {
  // Assumes objdump in PATH.
  // Run objdump -d on the binary.
  Command cmd("objdump", {"-d", std::string(binary_path)}, {}, tmp_path,
              "/dev/null");
  int system_exit_code = cmd.Execute();
  if (system_exit_code) {
    LOG(INFO) << __func__ << " objdump failed: " << system_exit_code;
    return PCTable();
  }
  PCTable pc_table;
  std::ifstream in(std::string{tmp_path});
  bool saw_new_function = false;

  // std::string::ends_with is not yet available.
  auto ends_with = [](std::string_view str, std::string_view end) -> bool {
    return end.size() <= str.size() && str.find(end) == str.size() - end.size();
  };

  // Read the objdump output, find lines that start a function
  // and lines that have a call to __sanitizer_cov_trace_pc.
  // Reconstruct the PCTable from those.
  for (std::string line; std::getline(in, line);) {
    if (ends_with(line, ">:")) {  // new function.
      saw_new_function = true;
      continue;
    }
    if (!ends_with(line, "<__sanitizer_cov_trace_pc>")) continue;
    std::istringstream iss(line);
    uintptr_t pc;
    iss >> std::hex >> pc;
    uintptr_t flags = saw_new_function ? PCInfo::kFuncEntry : 0;
    saw_new_function = false;  // next trace_pc will be in the same function.
    pc_table.push_back({pc, flags});
  }
  std::filesystem::remove(tmp_path);
  return pc_table;
}

FunctionFilter::FunctionFilter(std::string_view functions_to_filter,
                               const SymbolTable &symbols) {
  // set pcs_[idx] to 1, for any idx that belongs to a filtered function.
  // keep pcs_ empty, if no filtered functions are found in symbols.
  for (auto &func : absl::StrSplit(functions_to_filter, ',')) {
    for (size_t idx = 0, n = symbols.size(); idx < n; ++idx) {
      if (func == symbols.func(idx)) {
        if (pcs_.empty()) {
          pcs_.resize(n);
        }
        pcs_[idx] = 1;
      }
    }
  }
}

bool FunctionFilter::filter(const FeatureVec &features) const {
  if (pcs_.empty()) return true;
  for (auto feature : features) {
    if (!FeatureDomains::k8bitCounters.Contains(feature)) continue;
    size_t idx = Convert8bitCounterFeatureToPcIndex(feature);
    // idx should normally be within the range. Ignore it if it's not.
    if (idx >= pcs_.size()) continue;
    if (pcs_[idx]) return true;
  }
  return false;
}

}  // namespace centipede
