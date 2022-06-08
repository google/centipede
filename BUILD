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

package(default_visibility = ["//visibility:public"])

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
        ":environment",
        "@com_google_absl//absl/flags:parse",
    ],
)

################################################################################
#                             C++ libraries
################################################################################

# This lib must have zero dependencies (other than libc). See feature.h.
cc_library(
    name = "feature",
    hdrs = ["feature.h"],
)

cc_library(
    name = "logging",
    hdrs = ["logging.h"],
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
    deps = [
        ":defs",
        ":feature",
        ":logging",
        "@boringssl//:crypto",  # <openssl/sha.h>
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_map",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_absl//absl/synchronization",
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
        ":logging",
        ":shared_memory_blob_sequence",
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
    copts = ["-msse4.2"],  # __builtin_ia32_crc32di requires this under Bazel
    deps = [
        ":coverage",
        ":defs",
        ":feature",
        ":logging",
        ":util",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

cc_library(
    name = "command",
    srcs = ["command.cc"],
    hdrs = ["command.h"],
    deps = [
        ":logging",
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
        ":defs",
        ":environment",
        ":execution_result",
        ":feature",
        ":logging",
        ":shared_memory_blob_sequence",
        ":util",
        "@com_google_absl//absl/strings",
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
        ":util",
        "@com_google_absl//absl/base:core_headers",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/strings:cord",
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
        ":centipede_callbacks",
        ":centipede_lib",
        ":command",
        ":coverage",
        ":defs",
        ":environment",
        ":logging",
        ":remote_file",
        ":shared_memory_blob_sequence",
        ":util",
        "@com_google_absl//absl/strings",
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

# A fuzz target needs to link with this library (containing main()) in order to
# run with Centipede.
cc_library(
    name = "fuzz_target_runner",
    srcs = [
        "runner_fork_server.cc",
        "runner_interceptors.cc",
        "runner_main.cc",
        "runner_sancov.cc",
    ],
    hdrs = ["runner.h"],
    linkopts = ["-ldl"],  # for dlsym
    visibility = ["//visibility:public"],
    deps = [
        ":byte_array_mutator",
        ":defs",
        ":execution_result",
        ":feature",
        ":shared_memory_blob_sequence",
    ],
    alwayslink = 1,  # Otherwise the linker drops the fork server.
)
