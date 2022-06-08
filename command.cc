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

#include "./command.h"

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <sstream>
#include <string>

#include "absl/strings/str_cat.h"
#include "./logging.h"

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

bool Command::StartForkServer(std::string_view temp_dir_path,
                              std::string_view prefix) {
  LOG(INFO) << "starting the fork server for " << path();

  std::string fifo_path[2] = {std::filesystem::path(temp_dir_path)
                                  .append(absl::StrCat(prefix, "_FIFO0")),
                              std::filesystem::path(temp_dir_path)
                                  .append(absl::StrCat(prefix, "_FIFO1"))};

  for (int i = 0; i < 2; ++i) {
    CHECK_EQ(0, mkfifo(fifo_path[i].c_str(), 0600)) << "errno:" << errno;
  }
  std::stringstream ss;
  auto command = absl::StrCat("CENTIPEDE_FORK_SERVER_FIFO0=", fifo_path[0], " ",
                              " CENTIPEDE_FORK_SERVER_FIFO1=", fifo_path[1],
                              " ", ToString(), " &");
  int ret = system(command.c_str());
  CHECK_EQ(ret, 0) << "command failed: " << command;

  pipe_[0] = open(fifo_path[0].c_str(), O_WRONLY);
  pipe_[1] = open(fifo_path[1].c_str(), O_RDONLY);
  if (pipe_[0] < 0 || pipe_[1] < 0) {
    LOG(INFO) << "failed to start the fork server; will proceed without it";
    return false;
  }
  return true;
}

Command::~Command() {
  for (int i = 0; i < 2; ++i) {
    if (pipe_[i] >= 0) close(pipe_[i]);
  }
}

int Command::Execute() {
  if (pipe_[0] >= 0 && pipe_[1] >= 0) {
    // Wake up the fork server.
    char x = ' ';
    CHECK_EQ(1, write(pipe_[0], &x, 1));
    // The fork server forks, the child is running.
    // Block until we hear back from the fork server.
    int status = 0;
    CHECK_EQ(sizeof(status), read(pipe_[1], &status, sizeof(status)));
    return status;
  } else {
    // No fork server, use system().
    int ret = system(full_command_string_.c_str());
    was_interrupted_ = WIFSIGNALED(ret) && (WTERMSIG(ret) == SIGINT);
    if (WIFEXITED(ret)) return WEXITSTATUS(ret);
    return ret;
  }
}

}  // namespace centipede
