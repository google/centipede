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
#include <sys/poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "absl/log/check.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "./logging.h"
#include "./util.h"

namespace centipede {
namespace {

// See the definition of --fork_server flag.
inline constexpr std::string_view kCommandLineSeparator(" \\\n");
inline constexpr std::string_view kNoForkServerRequestPrefix("%f");

// Returns true if `cmd_path` should be executed using the fork server.
bool IsForkServerEnabled(std::string_view cmd_path) {
  return !absl::StartsWith(cmd_path, kNoForkServerRequestPrefix);
}

// Strips the fork server request prefix from `cmd_path`, if any.
std::string_view CleanseCommandPath(std::string_view cmd_path) {
  if (absl::StartsWith(cmd_path, kNoForkServerRequestPrefix)) {
    return cmd_path.substr(kNoForkServerRequestPrefix.size());
  } else {
    return cmd_path;
  }
}

// Returns the most suitable stdout or stderr redericetion path:
// - Always prefers an explicitly specified `out_or_err`.
// - Else, if the fork server is enabled for `cmd_path`, redirects to /dev/null:
// the fork server manipulates stdout/stderr and may mess them up for the parent
// process.
// - Else, (the fork server is disabled), returns an empty string (no
// redirection): there is no harm in the command writing to stdout/stderr in
// that case.
std::string_view ResolveOutOrErrPath(std::string_view cmd_path,
                                     std::string_view out_or_err) {
  if (!out_or_err.empty()) return out_or_err;
  if (IsForkServerEnabled(cmd_path)) return "/dev/null";
  return "";
}

}  // namespace

Command::Command(std::string_view path, std::vector<std::string> args,
                 std::vector<std::string> env, std::string_view out,
                 std::string_view err)
    : path_(CleanseCommandPath(path)),
      args_(std::move(args)),
      env_(std::move(env)),
      out_(ResolveOutOrErrPath(path, out)),
      err_(ResolveOutOrErrPath(path, err)) {}

std::string Command::ToString() const {
  std::vector<std::string> ss;
  ss.insert(ss.cend(), env_.cbegin(), env_.cend());
  ss.push_back(path_);
  ss.insert(ss.cend(), args_.cbegin(), args_.cend());
  if (!out_.empty()) {
    ss.emplace_back(absl::StrCat("> ", out_));
  }
  if (!err_.empty()) {
    ss.emplace_back(out_ != err_ ? absl::StrCat("2> ", err_) : "2>&1");
  }
  return absl::StrJoin(ss, kCommandLineSeparator);
}

bool Command::StartForkServer(std::string_view temp_dir_path,
                              std::string_view prefix) {
  if (!IsForkServerEnabled(path_)) {
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
    PCHECK(mkfifo(fifo_path_[i].c_str(), 0600) == 0)
        << VV(i) << VV(fifo_path_[i]);
  }

  const std::string command = absl::StrCat(
      "CENTIPEDE_FORK_SERVER_FIFO0=", fifo_path_[0], kCommandLineSeparator,
      "CENTIPEDE_FORK_SERVER_FIFO1=", fifo_path_[1], kCommandLineSeparator,
      command_line_, " &");
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

    // The fork server forks, the child is running. Block until some readable
    // data appears in the pipe (that is, after the fork server writes the
    // execution result to it).
    struct pollfd poll_fd = {
        .fd = pipe_[1],    // The file descriptor to wait for.
        .events = POLLIN,  // Wait until `fd` gets readable data written to it.
    };
    // TODO(ussuri): Parameterize the timeout.
    constexpr int kPollTimeoutMs = 30'000;
    const int poll_ret = poll(&poll_fd, 1, kPollTimeoutMs);
    if (poll_ret != 1 || (poll_fd.revents & POLLIN) == 0) {
      // The fork server errored out or timed out, or some other error occurred,
      // e.g. the syscall was interrupted.
      std::string fork_server_log = "<not dumped>";
      if (!out_.empty()) {
        ReadFromLocalFile(out_, fork_server_log);
      }
      if (poll_ret == 0) {
        LOG(FATAL) << "Timeout while waiting for fork server: "
                   << VV(kPollTimeoutMs) << VV(fork_server_log)
                   << VV(command_line_);
      } else {
        PLOG(FATAL) << "Error or interrupt while waiting for fork server: "
                    << VV(poll_ret) << VV(poll_fd.revents)
                    << VV(fork_server_log) << VV(command_line_);
      }
      __builtin_unreachable();
    }

    // The fork server wrote the execution result to the pipe: read it.
    CHECK_EQ(sizeof(exit_code), read(pipe_[1], &exit_code, sizeof(exit_code)));
  } else {
    // No fork server, use system().
    exit_code = system(command_line_.c_str());
  }
  if (WIFSIGNALED(exit_code) && (WTERMSIG(exit_code) == SIGINT))
    RequestEarlyExit(EXIT_FAILURE);
  if (WIFEXITED(exit_code)) return WEXITSTATUS(exit_code);
  return exit_code;
}

}  // namespace centipede
