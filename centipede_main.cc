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

#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/flags.h"
#include "absl/log/initialize.h"
#include "./centipede_default_callbacks.h"
#include "./centipede_interface.h"
#include "./environment.h"

int main(int argc, char **argv) {
  // By default, log everything to stderr. Explicit --stderrthreshold=N on the
  // command line takes precedence (ParseCommandLine() below resets the flag).
  // NB: The invocation order is important here.
  absl::SetFlag(&FLAGS_stderrthreshold,
                static_cast<int>(absl::LogSeverityAtLeast::kInfo));
  // Parse the command line.
  std::vector<char *> args = absl::ParseCommandLine(argc, argv);
  // Initialize the logging subsystem.
  absl::InitializeLog();

  // Reads flags; must happen after ParseCommandLine().
  centipede::Environment env(args.size(), args.data());
  centipede::DefaultCallbacksFactory<centipede::CentipedeDefaultCallbacks>
      callbacks;
  return CentipedeMain(env, callbacks);
}
