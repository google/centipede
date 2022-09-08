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

#ifndef THIRD_PARTY_CENTIPEDE_INTERNAL_TEST_UTIL_H_
#define THIRD_PARTY_CENTIPEDE_INTERNAL_TEST_UTIL_H_

#include <filesystem>
#include <string>

namespace centipede {

// Returns a temp dir for use inside tests. The dir is chosen in the following
// order of precedence:
// - $TEST_TMPDIR (highest)
// - $TMPDIR
// - /tmp
std::string GetTestTempDir();

// Returns the root directory filepath for a test's "runfiles".
std::filesystem::path GetTestRunfilesDir();

// Returns the filepath of a test's data dependency file.
std::filesystem::path GetDataDependencyFilepath(std::string_view rel_path);

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_INTERNAL_TEST_UTIL_H_
