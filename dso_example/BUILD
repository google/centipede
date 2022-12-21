# Copyright 2022 The Centipede Authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Build file for the dso_example

# Shared library that we want to fuzz, built with (some) coverage instrumentation.
# It links the centipede runtime dynamically.
cc_library(
    name = "fuzz_me",
    srcs = ["fuzz_me.cc"],
    copts = ["-fsanitize-coverage=trace-pc"],
    linkstatic = False,
    deps = ["@centipede//:centipede_runner_no_main"],
)

# The main binary that calls into :fuzz_me, which is linked dynamically.
# Not instrumented with coverage.
cc_binary(
    name = "main",
    srcs = ["main.cc"],
    linkstatic = False,
    deps = [
        ":fuzz_me",
    ],
)

# TODO(kcc): add a test that runs centipede on :main
