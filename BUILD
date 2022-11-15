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

# Description:
#   Centipede: an experimental distributed fuzzing engine.

VISIBILITY = ["//visibility:public"]

package(default_visibility = VISIBILITY)

licenses(["notice"])

exports_files([
    "LICENSE",
])

################################################################################
#                                  Binaries
################################################################################

cc_binary(
    name = "centipede",
    srcs = ["centipede_main.cc"],
    deps = [
        ":centipede_default_callbacks",
        ":centipede_interface",
        ":config_file",
        ":environment",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:flags",
        "@com_google_absl//absl/log:globals",
        "@com_google_absl//absl/log:initialize",
    ],
)

################################################################################
#                             C++ libraries
################################################################################

# This lib must have zero dependencies (other than libc). See feature.h.
cc_library(
    name = "feature",
    srcs = ["feature.cc"],
    hdrs = ["feature.h"],
)

cc_library(
    name = "logging",
    hdrs = ["logging.h"],
    deps = [
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
    ],
)

# simple definitions only, no code, no deps other than span.
cc_library(
    name = "defs",
    hdrs = ["defs.h"],
    deps = ["@com_google_absl//absl/types:span"],
)

# Various utilities.
cc_library(
    name = "util",
    srcs = [
        "hash.cc",
        "util.cc",
    ],
    hdrs = ["util.h"],
    linkopts = ["-Wl,-Bstatic -lcrypto -Wl,-Bdynamic -ldl"],
    deps = [
        ":defs",
        ":feature",
        ":logging",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "config_util",
    srcs = ["config_util.cc"],
    hdrs = ["config_util.h"],
    deps = [
        "@com_google_absl//absl/flags:reflection",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "config_file",
    srcs = ["config_file.cc"],
    hdrs = ["config_file.h"],
    deps = [
        ":config_util",
        ":logging",
        ":remote_file",
        ":util",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/flags:parse",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "rusage_stats",
    srcs = ["rusage_stats.cc"],
    hdrs = ["rusage_stats.h"],
    deps = [
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "rusage_profiler",
    srcs = ["rusage_profiler.cc"],
    hdrs = ["rusage_profiler.h"],
    deps = [
        ":rusage_stats",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "stats",
    srcs = ["stats.cc"],
    hdrs = ["stats.h"],
    deps = [
        ":environment",
        ":logging",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "analyze_corpora",
    srcs = ["analyze_corpora.cc"],
    hdrs = ["analyze_corpora.h"],
    deps = [
        ":corpus",
        ":coverage",
        ":feature",
        "@centipede//:logging",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

cc_library(
    name = "blob_file",
    srcs = ["blob_file.cc"],
    hdrs = ["blob_file.h"],
    deps = [
        ":defs",
        ":logging",
        ":remote_file",
        ":util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "shared_memory_blob_sequence",
    srcs = ["shared_memory_blob_sequence.cc"],
    hdrs = ["shared_memory_blob_sequence.h"],
    linkopts = ["-lrt"],  # for shm_open.
    # don't add any dependencies.
)

cc_library(
    name = "execution_result",
    srcs = ["execution_result.cc"],
    hdrs = ["execution_result.h"],
    deps = [
        # This target must have a minimal set of dependencies since it is
        # used in centipede_runner.
        ":feature",
        ":runner_cmp_trace",
        ":shared_memory_blob_sequence",
    ],
)

cc_library(
    name = "execution_request",
    srcs = ["execution_request.cc"],
    hdrs = ["execution_request.h"],
    deps = [
        # This target must have a minimal set of dependencies since it is
        # used in centipede_runner.
        ":shared_memory_blob_sequence",
        ":defs",
    ],
)

cc_library(
    name = "byte_array_mutator",
    srcs = ["byte_array_mutator.cc"],
    hdrs = ["byte_array_mutator.h"],
    # Avoid dependencies here, as this library will be linked to target binaries.
    deps = [
        ":defs",
    ],
)

# Library for dealing with code coverage data from
# https://clang.llvm.org/docs/SanitizerCoverage.html.
cc_library(
    name = "coverage",
    srcs = [
        "coverage.cc",
        "symbol_table.cc",
    ],
    hdrs = [
        "coverage.h",
        "symbol_table.h",
    ],
    deps = [
        ":command",
        ":defs",
        ":logging",
        ":util",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
    ],
)

# Library for dealing with control flow data from
# https://clang.llvm.org/docs/SanitizerCoverage.html#tracing-control-flow.
cc_library(
    name = "control_flow",
    srcs = [
        "control_flow.cc",
    ],
    hdrs = [
        "control_flow.h",
    ],
    deps = [
        ":coverage",
        ":logging",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

# Library for dealing with call graph data from
# https://clang.llvm.org/docs/SanitizerCoverage.html#tracing-control-flow.
cc_library(
    name = "call_graph",
    srcs = [
        "call_graph.cc",
    ],
    hdrs = [
        "call_graph.h",
    ],
    deps = [
        ":coverage",
        ":logging",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/log:check",
    ],
)

cc_library(
    name = "remote_file",
    srcs = ["remote_file.cc"],
    hdrs = ["remote_file.h"],
    deps = [
        ":defs",
        ":logging",
        "@com_google_absl//absl/base:core_headers",
    ],
)

# TODO(kcc): [impl] add dedicated unittests.
cc_library(
    name = "corpus",
    srcs = ["corpus.cc"],
    hdrs = ["corpus.h"],
    deps = [
        ":coverage",
        ":defs",
        ":feature",
        ":logging",
        ":util",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "command",
    srcs = ["command.cc"],
    hdrs = ["command.h"],
    deps = [
        ":logging",
        ":util",
        "@com_google_absl//absl/log:check",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/time",
    ],
)

cc_library(
    name = "centipede_callbacks",
    srcs = [
        "centipede_callbacks.cc",
    ],
    hdrs = [
        "centipede_callbacks.h",
    ],
    deps = [
        ":byte_array_mutator",
        ":command",
        ":coverage",
        ":defs",
        ":environment",
        ":execution_request",
        ":execution_result",
        ":logging",
        ":shared_memory_blob_sequence",
        ":util",
        "@com_google_absl//absl/strings",
    ],
)

cc_library(
    name = "shard_reader",
    hdrs = ["shard_reader.h"],
    deps = [
        ":blob_file",
        ":defs",
        ":feature",
        ":util",
        "@com_google_absl//absl/container:flat_hash_map",
    ],
)

cc_library(
    name = "centipede_lib",
    srcs = [
        "centipede.cc",
    ],
    hdrs = [
        "centipede.h",
    ],
    deps = [
        ":blob_file",
        ":centipede_callbacks",
        ":command",
        ":corpus",
        ":coverage",
        ":defs",
        ":environment",
        ":execution_result",
        ":feature",
        ":logging",
        ":remote_file",
        ":rusage_stats",
        ":shard_reader",
        ":stats",
        ":util",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "centipede_interface",
    srcs = [
        "centipede_interface.cc",
    ],
    hdrs = [
        "centipede_interface.h",
    ],
    deps = [
        ":analyze_corpora",
        ":blob_file",
        ":centipede_callbacks",
        ":centipede_lib",
        ":command",
        ":coverage",
        ":defs",
        ":environment",
        ":logging",
        ":remote_file",
        ":shard_reader",
        ":stats",
        ":util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:span",
    ],
)

cc_library(
    name = "environment",
    srcs = [
        "environment.cc",
    ],
    hdrs = [
        "environment.h",
    ],
    deps = [
        ":logging",
        ":util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
    ],
)

cc_library(
    name = "centipede_default_callbacks",
    srcs = ["centipede_default_callbacks.cc"],
    hdrs = ["centipede_default_callbacks.h"],
    deps = [
        ":centipede_interface",
        ":defs",
        ":environment",
        ":execution_result",
        ":logging",
    ],
)

cc_library(
    name = "weak_sancov_stubs",
    srcs = ["weak_sancov_stubs.cc"],
    alwayslink = 1,
)

# runner_fork_server can be linked to a binary or used directly as a .so via LD_PRELOAD.
cc_library(
    name = "runner_fork_server",
    srcs = ["runner_fork_server.cc"],
    alwayslink = 1,  # Otherwise the linker drops the fork server.
)

cc_library(
    name = "runner_cmp_trace",
    hdrs = ["runner_cmp_trace.h"],
)

# The runner library is special:
#   * It must not be instrumented with asan, sancov, etc.
#   * It must not have heavy dependencies, and ideally not at all.
#     Exceptions are STL and absl::span (temporarily, until we can switch to
#     std::span).
#   * The bazel rule :centipede_runner must produce a self-contained .a file
#     with all
#     objects in it, which means the build rule must not depend other .a rules.
#
#  Some of the .cc and .h files used by the runner are also used by the engine,
#  e.g. feature.cc. These files are compiled by the engine and the runner
#  separately, with different compiler flags.
RUNNER_SOURCES_NO_MAIN = [
    "byte_array_mutator.cc",
    "byte_array_mutator.h",
    "defs.h",
    "execution_request.cc",
    "execution_request.h",
    "execution_result.cc",
    "execution_result.h",
    "feature.cc",
    "feature.h",
    "runner.cc",
    "runner.h",
    "runner_cmp_trace.h",
    "runner_fork_server.cc",
    "runner_interceptors.cc",
    "runner_interface.h",
    "runner_sancov.cc",
    "shared_memory_blob_sequence.cc",
    "shared_memory_blob_sequence.h",
]

RUNNER_SOURCES_WITH_MAIN = RUNNER_SOURCES_NO_MAIN + ["runner_main.cc"]

# TODO(kcc): ensure asan/tsan/msan/ubsan instrumentation is disabled for runner.
RUNNER_COPTS = ["-fsanitize-coverage=0"]

RUNNER_LINKOPTS = [
    "-ldl",  # for dlsym
    "-lrt",  # for shm_open
]

RUNNER_DEPS = ["@com_google_absl//absl/types:span"]  # WARNING: be careful with more deps.

# A fuzz target needs to link with this library in order to run with Centipede.
# The fuzz target must provide its own main().
#
# See also comments above RUNNER_SOURCES_NO_MAIN.
#
cc_library(
    name = "centipede_runner_no_main",
    srcs = RUNNER_SOURCES_NO_MAIN,
    copts = RUNNER_COPTS,
    linkopts = RUNNER_LINKOPTS,
    deps = RUNNER_DEPS,
)

# A full self-contained library archive that external clients should link to
# their fuzz targets to make them compatible with the Centipede fuzzing engine
# (the `:centipede` target in this BUILD).
cc_library(
    name = "centipede_runner",
    srcs = RUNNER_SOURCES_WITH_MAIN,
    copts = RUNNER_COPTS,
    linkopts = RUNNER_LINKOPTS,
    deps = RUNNER_DEPS,
)

################################################################################
#                        General-purpose testing utilities
################################################################################

cc_library(
    name = "test_util",
    srcs = ["test_util.cc"],
    hdrs = ["test_util.h"],
    deps = [
        ":logging",
        "@com_google_absl//absl/strings",
    ],
)

sh_library(
    name = "test_util_sh",
    srcs = ["test_util.sh"],
)

################################################################################
#                               Unit tests
################################################################################

cc_test(
    name = "environment_test",
    srcs = ["environment_test.cc"],
    deps = [
        ":environment",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "util_test",
    srcs = ["util_test.cc"],
    deps = [
        ":defs",
        ":logging",
        ":test_util",
        ":util",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "config_util_test",
    srcs = ["config_util_test.cc"],
    deps = [
        # Include this just to get some flags external to the test itself.
        ":environment",  # buildcleaner:keep
        ":config_util",
        ":logging",
        "@com_google_googletest//:gtest_main",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "config_file_test",
    srcs = ["config_file_test.cc"],
    deps = [
        ":config_file",
        "@com_google_googletest//:gtest_main",
        # Defines FLAGS_flagfile.
        "@com_google_absl//absl/flags:parse",  # buildcleaner:keep
    ],
)

cc_test(
    name = "rusage_stats_test",
    size = "medium",
    timeout = "long",
    srcs = ["rusage_stats_test.cc"],
    deps = [
        ":logging",
        ":rusage_stats",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/functional:any_invocable",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "rusage_profiler_test",
    # Allocates large blocks of memory to fight small number volatility.
    size = "large",
    timeout = "long",
    srcs = ["rusage_profiler_test.cc"],
    deps = [
        ":rusage_profiler",
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/time",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "stats_test",
    srcs = ["stats_test.cc"],
    deps = [
        ":stats",
        "@centipede//:logging",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "analyze_corpora_test",
    srcs = ["analyze_corpora_test.cc"],
    deps = [
        ":analyze_corpora",
        "@centipede//:logging",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "blob_file_test",
    srcs = ["blob_file_test.cc"],
    deps = [
        ":blob_file",
        ":test_util",
        ":util",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/types:span",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "shared_memory_blob_sequence_test",
    srcs = ["shared_memory_blob_sequence_test.cc"],
    deps = [
        ":shared_memory_blob_sequence",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "execution_result_test",
    srcs = ["execution_result_test.cc"],
    deps = [
        ":execution_result",
        ":feature",
        ":shared_memory_blob_sequence",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "byte_array_mutator_test",
    srcs = ["byte_array_mutator_test.cc"],
    deps = [
        ":byte_array_mutator",
        ":defs",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "feature_test",
    srcs = ["feature_test.cc"],
    deps = [
        ":feature",
        ":logging",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "corpus_test",
    srcs = ["corpus_test.cc"],
    deps = [
        ":corpus",
        ":coverage",
        ":defs",
        ":feature",
        ":util",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_binary(
    name = "command_test_helper",
    srcs = ["command_test_helper.cc"],
    deps = [":runner_fork_server"],
)

cc_test(
    name = "command_test",
    srcs = ["command_test.cc"],
    data = [":command_test_helper"],
    deps = [
        ":command",
        ":logging",
        ":test_util",
        ":util",
        "@com_google_absl//absl/strings",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "runner_cmp_trace_test",
    srcs = ["runner_cmp_trace_test.cc"],
    deps = [
        ":runner_cmp_trace",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "control_flow_test",
    srcs = ["control_flow_test.cc"],
    deps = [
        "@centipede//:control_flow",
        "@centipede//:coverage",
        "@centipede//:logging",
        "@centipede//:test_util",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_test(
    name = "call_graph_test",
    srcs = ["call_graph_test.cc"],
    deps = [
        "@centipede//:call_graph",
        "@centipede//:coverage",
        "@centipede//:logging",
        "@com_google_googletest//:gtest_main",
    ],
)

################################################################################
#                               Other tests
################################################################################

# Verify that the `:centipede` target indeed creates a binary with the
# expected name.
sh_test(
    name = "centipede_binary_test",
    srcs = ["centipede_binary_test.sh"],
    data = [
        ":centipede",
        ":test_util_sh",
    ],
)
