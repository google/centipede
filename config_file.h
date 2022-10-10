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

#ifndef THIRD_PARTY_CENTIPEDE_GOOGLE_CONFIG_FILE_H_
#define THIRD_PARTY_CENTIPEDE_GOOGLE_CONFIG_FILE_H_

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace centipede::config {

// A set of overloads to cast argv between vector<string> and main()-compatible
// vector<char*> or argc/argv pair in both directions. The result can be used
// like this:
//   AugmentedArgvWithCleanup new_argv{CastArgv(argc, argv), ...};
//   std::vector<std::string> leftover_argv =
//       CastArgv(absl::ParseCommandLine(
//           new_argv.argc(), CastArgv(new_argv.argv()).data());
std::vector<std::string> CastArgv(int argc, char** argv);
std::vector<std::string> CastArgv(const std::vector<char*>& argv);
// WARNING: Beware of the lifetimes. The returned vector<char*> referenced the
// passed `argv`, so `argv` must outlive it.
std::vector<char*> CastArgv(const std::vector<std::string>& argv);

// Constructs an augmented copy of `argv` with any substrings appearing in the
// original elements replaced according to a list replacements.
// TODO(ussuri): Make more robust. What we really want is replace any possible
//  form of --flag=value with an equivalent form of --new_flag=new_value.
class AugmentedArgvWithCleanup final {
 public:
  using Replacements = std::vector<std::pair<std::string, std::string>>;
  using BackingResourcesCleanup = std::function<void()>;

  // Ctor. The `orig_argc` and `orig_argv` are compatible with those passed to a
  // main(). The `replacements` map should map an old substring to a new one.
  // Only simple, one-stage string replacement is performed: no regexes,
  // placeholders, envvars or recursion. The `cleanup` callback should clean up
  // any temporary resources backing the modified flags, such as temporary
  // files.
  AugmentedArgvWithCleanup(const std::vector<std::string>& orig_argv,
                           const Replacements& replacements,
                           BackingResourcesCleanup&& cleanup);
  // Dtor. Invokes `cleanup_`.
  ~AugmentedArgvWithCleanup();

  // Movable by not copyable to prevent `cleanup_` from running twice.
  AugmentedArgvWithCleanup(const AugmentedArgvWithCleanup&) = delete;
  AugmentedArgvWithCleanup& operator=(const AugmentedArgvWithCleanup&) = delete;
  AugmentedArgvWithCleanup(AugmentedArgvWithCleanup&&) noexcept;
  AugmentedArgvWithCleanup& operator=(AugmentedArgvWithCleanup&&) noexcept;

  // The new argc. Currently, will always match the original argc.
  int argc() const { return static_cast<int>(argv_.size()); }
  // The new, possibly augmented argv. Note that all its char* elements are
  // backed by newly allocated std::strings, so they will all be different from
  // their counterparts in the original argv.
  const std::vector<std::string>& argv() const { return argv_; }
  // Whether the original argv has been augmented from the original, i.e. if any
  // of the requested string replacements actually occurred.
  bool was_augmented() const { return was_augmented_; }

 private:
  std::vector<std::string> argv_;
  bool was_augmented_;
  BackingResourcesCleanup cleanup_;
};

}  // namespace centipede::config

#endif  // THIRD_PARTY_CENTIPEDE_GOOGLE_CONFIG_FILE_H_
