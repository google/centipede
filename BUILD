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

# The "master" rule that builds the main Centipede executable named
# `centipede_main`. Dependent rules must use this one in their `deps` attribute.
cc_binary(
    name = "centipede_main",
    srcs = ["centipede_main.cc"],
    deps = [
        ":centipede_default_callbacks",
        ":centipede_interface",
        ":environment",
        "@com_google_absl//absl/flags:parse",
    ],
)

# An "alias" rule that copies the `centipede_main` executable to `centipede`.
# End-users may use this one to build a more conveniently named executable.
genrule(
    name = "centipede",
    srcs = [":centipede_main"],
    outs = ["centipede"],
    # NOTE: If would seem natural to `mv` instead, but Bazel/Bazel both end up
    # still copying the file, not moving it.
    cmd = "cp $< $@",
    executable = True,
    output_to_bindir = True,
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

# simple definitions only, no code, no deps.
cc_library(
    name = "defs",
    hdrs = ["defs.h"],
)

# Various utilities.
cc_library(
    name = "util",
    srcs = [
        "hash.cc",
        "util.cc",
    ],
    hdrs = ["util.h"],
    linkopts = ["-lcrypto"],
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
    name = "stats",
    srcs = ["stats.cc"],
    hdrs = ["stats.h"],
    deps = [
        ":environment",
        "@centipede//:logging",
        "@com_google_absl//absl/types:span",
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
        # used in fuzz_target_runner.
        ":feature",
        ":shared_memory_blob_sequence",
    ],
)

cc_library(
    name = "execution_request",
    srcs = ["execution_request.cc"],
    hdrs = ["execution_request.h"],
    deps = [
        # This target must have a minimal set of dependencies since it is
        # used in fuzz_target_runner.
        ":shared_memory_blob_sequence",
        ":defs",
    ],
)

cc_library(
    name = "byte_array_mutator",
    srcs = ["byte_array_mutator.cc"],
    hdrs = ["byte_array_mutator.h"],
    # Avoid dependencies here, as this library will be linked to target
    # binaries.
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
        "@com_google_absl//absl/strings",
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
        ":shard_reader",
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
        ":blob_file",
        ":centipede_callbacks",
        ":centipede_lib",
        ":command",
        ":coverage",
        ":defs",
        ":environment",
        ":logging",
        ":remote_file",
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
        "@com_google_absl//absl/flags:flag",
        "@com_google_absl//absl/strings",
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
        ":util",
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
    name = "runner_interface",
    hdrs = ["runner_interface.h"],
)

# A fuzz target needs to link with this library in order to run with Centipede.
# The fuzz target must provide its own main().
#
# NOTE: This target's own sources must never be sancov-instrumented (unlike the
# sources of a fuzz target to which it is being linked!).
#
cc_library(
    name = "fuzz_target_runner_no_main",
    srcs = [
        "runner.cc",
        "runner_interceptors.cc",
        "runner_sancov.cc",
    ],
    hdrs = ["runner.h"],
    copts = ["-fsanitize-coverage=0"],
    linkopts = ["-ldl"],  # for dlsym
    deps = [
        ":byte_array_mutator",
        ":defs",
        ":execution_request",
        ":execution_result",
        ":feature",
        ":runner_fork_server",
        ":runner_interface",
        ":shared_memory_blob_sequence",
    ],
)

# A fuzz target needs to link with this library (containing main()) in order to
# run with Centipede.
#
# NOTE: This target's own sources must never be sancov-instrumented (unlike the
# sources of a fuzz target to which it is being linked!).
#
cc_library(
    name = "fuzz_target_runner",
    srcs = ["runner_main.cc"],
    copts = ["-fsanitize-coverage=0"],
    deps = [
        ":fuzz_target_runner_no_main",  # buildcleaner: keep
        ":runner_interface",
    ],
)

# A full self-contained library archive that external clients should link to their
# fuzz targets to make them compatible with the Centipede main binary (the
# `:centipede` target in this BUILD).
# TODO(ussuri): Find a way to merge this with fuzz_target_runner: the list of
#  the inputs sources is identical.
cc_library(
    name = "centipede_runner",
    srcs = [
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
        "runner_fork_server.cc",
        "runner_interceptors.cc",
        "runner_interface.h",
        "runner_main.cc",
        "runner_sancov.cc",
        "shared_memory_blob_sequence.cc",
        "shared_memory_blob_sequence.h",
    ],
    # NOTE: Centipede's own sources must never be sancov-instrumented.
    copts = ["-fsanitize-coverage=0"],
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
        ":feature",
        ":logging",
        ":util",
        "@com_google_absl//absl/container:flat_hash_map",
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
