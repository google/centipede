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

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "./logging.h"
#include "./util.h"

namespace centipede {

// See the definition of --fork_server flag.
inline constexpr std::string_view kCommandLineSeparator(" \\\n");
inline constexpr std::string_view kNoForkServerRequestPrefix("%f");

std::string Command::ToString() const {
  std::vector<std::string> ss;
  // env.
  ss.reserve(env_.size());
  for (auto &env : env_) {
    ss.emplace_back(env);
  }
  // path.
  std::string path = path_;
  // Strip the % prefixes, if any.
  if (absl::StartsWith(path, kNoForkServerRequestPrefix)) {
    path = path.substr(kNoForkServerRequestPrefix.size());
  }
  ss.emplace_back(path);
  // args.
  for (auto &arg : args_) {
    ss.emplace_back(arg);
  }
  // out/err.
  if (!out_.empty()) {
    ss.emplace_back(absl::StrCat("> ", out_));
  }
  if (!err_.empty()) {
    if (out_ != err_) {
      ss.emplace_back(absl::StrCat("2> ", err_));
    } else {
      ss.emplace_back("2>&1");
    }
  }
  // Trim trailing space and return.
  return absl::StrJoin(ss, kCommandLineSeparator);
}

bool Command::StartForkServer(std::string_view temp_dir_path,
                              std::string_view prefix) {
  if (absl::StartsWith(path_, kNoForkServerRequestPrefix)) {
    LOG(INFO) << "Fork server disabled for " << path();
    return false;
  }
  LOG(INFO) << "Starting fork server for " << path();

  fifo_path_[0] = std::filesystem::path(temp_dir_path)
                      .append(absl::StrCat(prefix, "_FIFO0"));
  fifo_path_[1] = std::filesystem::path(temp_dir_path)
                      .append(absl::StrCat(prefix, "_FIFO1"));
  (void)std::filesystem::create_directory(temp_dir_path);  // it may not exist.
  for (int i = 0; i < 2; ++i) {
    CHECK_EQ(mkfifo(fifo_path_[i].c_str(), 0600), 0)
        << VV(errno) << VV(fifo_path_[i]);
  }
  const std::string command = absl::StrCat(
      "CENTIPEDE_FORK_SERVER_FIFO0=", fifo_path_[0], kCommandLineSeparator,
      "CENTIPEDE_FORK_SERVER_FIFO1=", fifo_path_[1], kCommandLineSeparator,
      full_command_string_, " &");
  LOG(INFO) << "Fork server command:\n" << command;
  int ret = system(command.c_str());
  CHECK_EQ(ret, 0) << "Failed to start fork server using command:\n" << command;

  pipe_[0] = open(fifo_path_[0].c_str(), O_WRONLY);
  pipe_[1] = open(fifo_path_[1].c_str(), O_RDONLY);
  if (pipe_[0] < 0 || pipe_[1] < 0) {
    LOG(INFO) << "Failed to establish communication with fork server; will "
                 "proceed without it";
    return false;
  }
  return true;
}

Command::~Command() {
  for (int i = 0; i < 2; ++i) {
    if (pipe_[i] >= 0) CHECK_EQ(close(pipe_[i]), 0);
    if (!fifo_path_[i].empty())
      CHECK(std::filesystem::remove(fifo_path_[i])) << fifo_path_[i];
  }
}

int Command::Execute() {
  int exit_code = 0;
  if (pipe_[0] >= 0 && pipe_[1] >= 0) {
    // Wake up the fork server.
    char x = ' ';
    CHECK_EQ(1, write(pipe_[0], &x, 1));
    // The fork server forks, the child is running.
    // Block until we hear back from the fork server.
    CHECK_EQ(sizeof(exit_code), read(pipe_[1], &exit_code, sizeof(exit_code)));
  } else {
    // No fork server, use system().
    exit_code = system(full_command_string_.c_str());
  }
  if (WIFSIGNALED(exit_code) && (WTERMSIG(exit_code) == SIGINT))
    RequestEarlyExit(EXIT_FAILURE);
  if (WIFEXITED(exit_code)) return WEXITSTATUS(exit_code);
  return exit_code;
}

}  // namespace centipede
