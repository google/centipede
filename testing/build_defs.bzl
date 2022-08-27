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

"""This module contains rules that build a fuzz target that is
sancov-instrumented (https://clang.llvm.org/docs/SanitizerCoverage.html).
"""

def centipede_fuzz_target(
        name,
        srcs = None,
        sancov = "trace-pc-guard,pc-table,trace-loads,trace-cmp"):
    """Generates a cc_binary rule for a sancov-instrumented fuzz target.

    Args:
      name: A unique name for this target
      srcs: Test source(s); the default is [`name` + ".cc"]
      sancov: The sancov instrumentations to use, e.g. "trace-pc-guard,pc-table"
    """

    native.cc_binary(
        name = name,
        copts = [
            "-O2",
            "-fsanitize-coverage=" + sancov,
            "-fno-builtin",
            "-gline-tables-only",
            "-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION",
        ],
        linkopts = ["-ldl -lrt -lpthread"],
        srcs = srcs or [name + ".cc"],
        deps = [
            "@centipede//:centipede_runner",
        ],
    )
