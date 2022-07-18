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

"""BUILD rule for Centipede puzzles"""

def puzzle(name):
    """Generates a sancov-instrumented cc_binary target + two sh_test targets to run it.

    Args:
    name: A unique name for this target
    """

    native.cc_binary(
        name = name + "_centipede",
        copts = [
            "-O2",
            "-fsanitize-coverage=trace-pc-guard,pc-table,trace-cmp",
            "-fno-builtin",
            "-gline-tables-only",
        ],
        linkopts = ["-ldl -lrt -lpthread"],
        srcs = [name + ".cc"],
        deps = [
            "@centipede//:centipede_runner",
        ],
    )

    # We test every puzzle with two different seeds so that the result is more
    # trustworthy. The seeds are fixed so that we have some degree of
    # repeatability. Each sh_test performs a single run with a single seed, so
    # that the log is minimal.
    for seed in ["1", "2"]:
        native.sh_test(
            name = "run_" + seed + "_" + name,
            srcs = ["run_puzzle.sh"],
            data = [
                ":" + name + "_centipede",
                name + ".cc",
                "@centipede//:centipede",
            ],
        )
