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

#include "./runner_dl_info.h"

#include <elf.h>
#include <limits.h>
#include <link.h>     // dl_iterate_phdr

#include "./runner_utils.h"

namespace centipede {

// See man dl_iterate_phdr.
// `result_voidptr` is cast to a `DlInfo &result`.
// Sets result.start_address and result.size.
// The code assumes that the main binary is the first one to be iterated on.
// TODO(kcc): this needs to be extended to handle DSOs other than main binary.
static int DlIteratePhdrCallback(struct dl_phdr_info *info, size_t size,
                                 void *result_voidptr) {
  DlInfo &result = *reinterpret_cast<DlInfo *>(result_voidptr);
  RunnerCheck(!result.IsSet(), "result is already set");
  result.start_address = info->dlpi_addr;
  for (int j = 0; j < info->dlpi_phnum; j++) {
    uintptr_t end_offset =
        info->dlpi_phdr[j].p_vaddr + info->dlpi_phdr[j].p_memsz;
    if (result.size < end_offset) result.size = end_offset;
  }
  auto some_code_address = reinterpret_cast<uintptr_t>(&DlIteratePhdrCallback);
  RunnerCheck(!(result.start_address > some_code_address),
              "start_address is above the code");
  RunnerCheck(!(result.start_address + result.size < some_code_address),
              "start_address + size is below the code");
  return 1;  // we need only the first header, so return 1.
}

DlInfo GetDlInfo() {
  DlInfo result;
  dl_iterate_phdr(DlIteratePhdrCallback, &result);
  return result;
}

}  // namespace centipede
