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

#include "./command.h"

#include <signal.h>
#include <stdlib.h>

#include <sstream>
#include <string>

namespace centipede {
std::string Command::ToString() const {
  std::stringstream ss;
  // env.
  for (auto &env : env_) {
    ss << env << " ";
  }
  // path.
  ss << path_ << " ";
  // args.
  for (auto &arg : args_) {
    ss << arg << " ";
  }
  // out/err.
  if (!out_.empty()) {
    ss << "> " << out_ << " ";
  }
  if (!err_.empty()) {
    if (out_ != err_) {
      ss << "2> " << err_ << " ";
    } else {
      ss << "2>&1 ";
    }
  }
  // Trim trailing space and return.
  auto result = ss.str();
  result.erase(result.find_last_not_of(' ') + 1);
  return result;
}

int Command::Execute() {
  // TODO(kcc): [as-needed] we may want to replace system with something faster.
  int ret = system(full_command_string_.c_str());
  was_interrupted_ = WIFSIGNALED(ret) && (WTERMSIG(ret) == SIGINT);
  if (WIFEXITED(ret)) return WEXITSTATUS(ret);
  return ret;
}

}  // namespace centipede
