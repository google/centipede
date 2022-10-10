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

#include "./config_file.h"

#include <utility>

#include "absl/log/check.h"
#include "absl/strings/str_replace.h"
#include "./logging.h"

namespace centipede::config {

std::vector<char*> CastArgv(const std::vector<std::string>& argv) {
  std::vector<char*> ret_argv;
  ret_argv.reserve(argv.size());
  for (const auto& arg : argv) {
    ret_argv.push_back(const_cast<char*>(arg.c_str()));
  }
  return ret_argv;
}

std::vector<std::string> CastArgv(const std::vector<char*>& argv) {
  return {argv.cbegin(), argv.cend()};
}

std::vector<std::string> CastArgv(int argc, char** argv) {
  return {argv, argv + argc};
}

AugmentedArgvWithCleanup::AugmentedArgvWithCleanup(
    const std::vector<std::string>& orig_argv, const Replacements& replacements,
    BackingResourcesCleanup&& cleanup)
    : was_augmented_{false}, cleanup_{cleanup} {
  argv_.reserve(orig_argv.size());
  for (const auto& old_arg : orig_argv) {
    const std::string& new_arg =
        argv_.emplace_back(absl::StrReplaceAll(old_arg, replacements));
    if (new_arg != old_arg) {
      VLOG(1) << "Augmented argv arg:\n" << VV(old_arg) << "\n" << VV(new_arg);
      was_augmented_ = true;
    }
  }
}

AugmentedArgvWithCleanup::AugmentedArgvWithCleanup(
    AugmentedArgvWithCleanup&& rhs) noexcept {
  *this = std::move(rhs);
}

AugmentedArgvWithCleanup& AugmentedArgvWithCleanup::operator=(
    AugmentedArgvWithCleanup&& rhs) noexcept {
  argv_ = std::move(rhs.argv_);
  was_augmented_ = rhs.was_augmented_;
  cleanup_ = std::move(rhs.cleanup_);
  // Prevent rhs from calling the cleanup in dtor (moving an std::function
  // leaves the moved object in a valid, but undefined, state).
  rhs.cleanup_ = {};
  return *this;
}

AugmentedArgvWithCleanup::~AugmentedArgvWithCleanup() {
  if (cleanup_) cleanup_();
}

}  // namespace centipede::config
