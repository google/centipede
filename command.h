// Copyright 2022 Google LLC.
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

#ifndef THIRD_PARTY_CENTIPEDE_COMMAND_H_
#define THIRD_PARTY_CENTIPEDE_COMMAND_H_

#include <string>
#include <string_view>
#include <vector>

namespace centipede {
class Command final {
 public:
  // Move-constructible only.
  Command(const Command& other) = delete;
  Command& operator=(const Command& other) = delete;
  Command(Command&& other) = default;
  Command& operator=(Command&& other) = delete;

  // Constructs a command:
  // `path`: path to the binary.
  // `args`: arguments.
  // `env`: environment variables/values (in the form "KEY=VALUE").
  // `out`: stdout redirect path (empty means none).
  // `err`: stderr redirect path (empty means none).
  // If `out` == `err` and both are non-empty, stdout/stderr are combined.
  Command(std::string_view path, const std::vector<std::string> &args = {},
          const std::vector<std::string> &env = {}, std::string_view out = "",
          std::string_view err = "")
      : path_(path), args_(args), env_(env), out_(out), err_(err) {}

  // Returns a string representing the command, e.g. like this
  // "ENV1=VAL1 path arg1 arg2 > out 2>& err"
  std::string ToString() const;
  // Executes the command, returns the exit status.
  // Can be called more than once.
  int Execute();
  // Returns true iff the last Execute() was killed by SIGINT.
  bool WasInterrupted() const { return was_interrupted_; }

  // Accessors.
  const std::string& path() const { return path_; }

 private:
  const std::string path_;
  const std::vector<std::string> args_;
  const std::vector<std::string> env_;
  const std::string out_;
  const std::string err_;
  std::string full_command_string_ = ToString();
  // Execute() sets was_interrupted_ to true iff the execution was interrupted.
  bool was_interrupted_ = false;
};

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_COMMAND_H_
