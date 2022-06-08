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

#include "./symbol_table.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "./command.h"
#include "./coverage.h"
#include "./logging.h"
#include "./util.h"

namespace centipede {

void SymbolTable::ReadFromLLVMSymbolizer(std::istream &in) {
  // We remove some useless file prefixes for better human readability.
  const std::string file_prefixes_to_remove[] = {"/proc/self/cwd/", "./"};
  while (in) {
    // We (mostly) blindly trust the input format is correct.
    std::string func, file, empty;
    std::getline(in, func);
    std::getline(in, file);
    std::getline(in, empty);
    CHECK(empty.empty());
    if (!in) break;
    for (auto &bad_prefix : file_prefixes_to_remove) {
      if (absl::StartsWith(file, bad_prefix)) {
        file = file.substr(bad_prefix.size());
      }
    }
    AddEntry(func, file);
  }
}

void SymbolTable::GetSymbolsFromBinary(const Coverage::PCTable &pc_table,
                                       std::string_view binary_path,
                                       std::string_view llvm_symbolizer_path,
                                       std::string_view tmp_path1,
                                       std::string_view tmp_path2) {
  auto pcs_path(tmp_path1);
  auto symbols_path(tmp_path2);
  // Create the input file (one PC per line).
  std::string pcs_string;
  for (auto &pc_info : pc_table) {
    absl::StrAppend(&pcs_string, "0x", absl::Hex(pc_info.pc), "\n");
  }
  WriteToLocalFile(pcs_path, pcs_string);
  // Run the symbolizer.
  Command cmd(llvm_symbolizer_path,
              {"--no-inlines", "-e", std::string(binary_path), "<",
               std::string(pcs_path)},
              {/*env*/}, symbols_path);
  int system_exit_code = cmd.Execute();
  if (system_exit_code) LOG(INFO) << "system() failed " << system_exit_code;
  // Get and process the symbolizer output.
  std::ifstream symbolizer_output(std::string{symbols_path});
  ReadFromLLVMSymbolizer(symbolizer_output);
  std::filesystem::remove(pcs_path);
  std::filesystem::remove(symbols_path);
}

}  // namespace centipede
