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
#include "./logging.h"
#include "./test_util.h"
#include "./util.h"

namespace centipede {
namespace {

TEST(Command, ToString) {
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

TEST(Command, Execute) {
  // Check for default exit code.
  Command echo("echo");
  EXPECT_EQ(echo.Execute(), 0);
  EXPECT_FALSE(EarlyExitRequested());

  // Check for exit code 7.
  Command exit7("bash -c 'exit 7'");
  EXPECT_EQ(exit7.Execute(), 7);
  EXPECT_FALSE(EarlyExitRequested());

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

TEST(Command, ForkServer) {
  Command bad_command("/dev/null");
  // TODO(kcc): [impl] currently a bad command will hang. Make it return false.

  const std::string helper = GetDataDependencyFilepath("command_test_helper");

  {
    Command ret0(helper);
    EXPECT_TRUE(ret0.StartForkServer(GetTestTempDir(), "ForkServer"));
    EXPECT_EQ(ret0.Execute(), EXIT_SUCCESS);
  }

  {
    Command fail(helper, {"fail"});
    EXPECT_TRUE(fail.StartForkServer(GetTestTempDir(), "ForkServer"));
    EXPECT_EQ(fail.Execute(), EXIT_FAILURE);
  }

  {
    Command ret7(helper, {"ret42"});
    EXPECT_TRUE(ret7.StartForkServer(GetTestTempDir(), "ForkServer"));
    EXPECT_EQ(ret7.Execute(), 42);
  }

  {
    Command abrt(helper, {"abort"});
    EXPECT_TRUE(abrt.StartForkServer(GetTestTempDir(), "ForkServer"));
    EXPECT_EQ(WTERMSIG(abrt.Execute()), SIGABRT);
  }

  // TODO(kcc): [impl] test what happens if the child is interrupted.
}

}  // namespace
}  // namespace centipede
