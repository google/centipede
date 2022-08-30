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

#include <cstdlib>
#include <filesystem>
#include <string_view>

#include "./logging.h"

namespace centipede {

std::string GetTestTempDir() {
  if (auto* path = std::getenv("TEST_TMPDIR"); path != nullptr) return path;
  if (auto* path = std::getenv("TMPDIR"); path != nullptr) return path;
  return "/tmp";
}

std::filesystem::path GetTestRunfilesDir() {
  const char* test_srcdir = std::getenv("TEST_SRCDIR");
  CHECK(test_srcdir != nullptr)
      << "TEST_SRCDIR envvar is expected to be set by build system";
  const char* test_workspace = std::getenv("TEST_WORKSPACE");
  CHECK(test_workspace != nullptr)
      << "TEST_WORKSPACE envvar is expected to be set by build system";
  auto path = std::filesystem::path{test_srcdir}.append(test_workspace);
  CHECK(std::filesystem::exists(path))  //
      << "No such dir: " << VV(path) << VV(test_srcdir) << VV(test_workspace);
  return path;
}

std::filesystem::path GetDataDependencyFilepath(std::string_view rel_path) {
  const auto runfiles_dir = GetTestRunfilesDir();
  auto path = runfiles_dir;
  path.append(rel_path);
  CHECK(std::filesystem::exists(path))  //
      << "No such path: " << VV(path) << VV(runfiles_dir) << VV(rel_path);
  return path;
}

}  // namespace centipede
