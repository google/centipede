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
#include <link.h>  // dl_iterate_phdr

#include <cstdio>
#include <cstring>

#include "./runner_utils.h"

namespace centipede {

namespace {
// Struct to pass to dl_iterate_phdr's callback.
struct DlCallbackParam {
  // Full path to the instrumented library or nullptr for the main binary.
  const char *dl_path;
  // DlInfo to set on success.
  DlInfo &result;
};

}  // namespace

// See man dl_iterate_phdr.
// `param_voidptr` is cast to a `DlCallbackParam *param`.
// Looks for the dynamic library with `dlpi_name == param->dl_path`
// or for the main binary if `param->dl_path == nullptr`.
// The code assumes that the main binary is the first one to be iterated on.
// If the desired library is found, sets result.start_address and result.size,
// otherwise leaves result unchanged.
static int DlIteratePhdrCallback(struct dl_phdr_info *info, size_t size,
                                 void *param_voidptr) {
  DlCallbackParam *param = static_cast<DlCallbackParam *>(param_voidptr);
  DlInfo &result = param->result;
  RunnerCheck(!result.IsSet(), "result is already set");
  if (param->dl_path == nullptr ||
      strcmp(param->dl_path, info->dlpi_name) == 0) {
    result.start_address = info->dlpi_addr;
    // TODO(kcc): verify the correctness of these computations.
    for (int j = 0; j < info->dlpi_phnum; j++) {
      uintptr_t end_offset =
          info->dlpi_phdr[j].p_vaddr + info->dlpi_phdr[j].p_memsz;
      if (result.size < end_offset) result.size = end_offset;
    }
    RunnerCheck(result.size != 0,
                "DlIteratePhdrCallback failed to compute result.size");
  }
  return result.IsSet();  // return 1 if we found what we were looking for.
}

DlInfo GetDlInfo(const char *dl_path) {
  DlInfo result;
  DlCallbackParam callback_param = {dl_path, result};
  dl_iterate_phdr(DlIteratePhdrCallback, &callback_param);
  return result;
}

}  // namespace centipede
