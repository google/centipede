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

#include <signal.h>
#include <sys/wait.h>  // NOLINT(for WTERMSIG)

#include <cstdlib>
#include <string>

#include "googletest/include/gtest/gtest.h"
#include "absl/strings/substitute.h"
#include "./logging.h"
#include "./test_util.h"
#include "./util.h"

namespace centipede {
namespace {

TEST(CommandTest, ToString) {
  EXPECT_EQ(Command("x").ToString(), "x");
  EXPECT_EQ(Command("path", {"arg1", "arg2"}).ToString(),
            "path \\\narg1 \\\narg2");
  EXPECT_EQ(Command("x", {}, {"K1=V1", "K2=V2"}).ToString(),
            "K1=V1 \\\nK2=V2 \\\nx");
  EXPECT_EQ(Command("x", {}, {}, "out").ToString(), "x \\\n> out");
  EXPECT_EQ(Command("x", {}, {}, "", "err").ToString(), "x \\\n2> err");
  EXPECT_EQ(Command("x", {}, {}, "out", "err").ToString(),
            "x \\\n> out \\\n2> err");
  EXPECT_EQ(Command("x", {}, {}, "out", "out").ToString(),
            "x \\\n> out \\\n2>&1");
}

TEST(CommandTest, Execute) {
  // Check for default exit code.
  Command echo("echo");
  EXPECT_EQ(echo.Execute(), 0);
  EXPECT_FALSE(EarlyExitRequested());

  // Check for exit code 7.
  Command exit7("bash -c 'exit 7'");
  EXPECT_EQ(exit7.Execute(), 7);
  EXPECT_FALSE(EarlyExitRequested());
}

TEST(CommandDeathTest, Execute) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  // Test for interrupt handling.
  const auto self_sigint_lambda = []() {
    Command self_sigint("bash -c 'kill -SIGINT $$'");
    self_sigint.Execute();
    if (EarlyExitRequested()) {
      LOG(INFO) << "Early exit requested";
      exit(ExitCode());
    }
  };
  EXPECT_DEATH(self_sigint_lambda(), "Early exit requested");
}

TEST(CommandTest, ForkServer) {
  const std::string test_tmpdir = GetTestTempDir(test_info_->name());
  const std::string helper = GetDataDependencyFilepath("command_test_helper");

  // TODO(ussuri): Dedupe these testcases.

  {
    const std::string input = "success";
    const std::string log = std::filesystem::path{test_tmpdir} / input;
    Command cmd(helper, {input}, {}, log, log);
    EXPECT_TRUE(cmd.StartForkServer(test_tmpdir, "ForkServer"));
    EXPECT_EQ(cmd.Execute(), EXIT_SUCCESS);
    std::string log_contents;
    ReadFromLocalFile(log, log_contents);
    EXPECT_EQ(log_contents, absl::Substitute("Got input: $0", input));
  }

  {
    const std::string input = "fail";
    const std::string log = std::filesystem::path{test_tmpdir} / input;
    Command cmd(helper, {input}, {}, log, log);
    EXPECT_TRUE(cmd.StartForkServer(test_tmpdir, "ForkServer"));
    EXPECT_EQ(cmd.Execute(), EXIT_FAILURE);
    std::string log_contents;
    ReadFromLocalFile(log, log_contents);
    EXPECT_EQ(log_contents, absl::Substitute("Got input: $0", input));
  }

  {
    const std::string input = "ret42";
    const std::string log = std::filesystem::path{test_tmpdir} / input;
    Command cmd(helper, {input}, {}, log, log);
    EXPECT_TRUE(cmd.StartForkServer(test_tmpdir, "ForkServer"));
    EXPECT_EQ(cmd.Execute(), 42);
    std::string log_contents;
    ReadFromLocalFile(log, log_contents);
    EXPECT_EQ(log_contents, absl::Substitute("Got input: $0", input));
  }

  {
    const std::string input = "abort";
    const std::string log = std::filesystem::path{test_tmpdir} / input;
    Command cmd(helper, {input}, {}, log, log);
    EXPECT_TRUE(cmd.StartForkServer(test_tmpdir, "ForkServer"));
    EXPECT_EQ(WTERMSIG(cmd.Execute()), SIGABRT);
    std::string log_contents;
    ReadFromLocalFile(log, log_contents);
    EXPECT_EQ(log_contents, absl::Substitute("Got input: $0", input));
  }

  // TODO(kcc): [impl] test what happens if the child is interrupted.
}

TEST(CommandDeathTest, ForkServerHangingBinary) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  const std::string test_tmpdir = GetTestTempDir(test_info_->name());
  const std::string helper = GetDataDependencyFilepath("command_test_helper");
  const std::string input = "hang";
  const std::string log = std::filesystem::path{test_tmpdir} / input;
  EXPECT_DEATH(
      {
        Command cmd(helper, {"hang"}, {}, log, log);
        ASSERT_TRUE(cmd.StartForkServer(test_tmpdir, "ForkServer"));
        cmd.Execute();
      },
      "Timeout while waiting for fork server");
  std::string log_contents;
  ReadFromLocalFile(log, log_contents);
  EXPECT_EQ(log_contents,
            absl::Substitute("Got input: $0\nHanging...\n...Unhung", input))
      << log;
}

}  // namespace
}  // namespace centipede
