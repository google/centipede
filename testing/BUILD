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

load("//tools/build_defs/testing:bzl_library.bzl", "bzl_library")
load(":build_defs.bzl", "centipede_test_target", "centipede_test_target_sancov")

package(default_visibility = ["//visibility:public"])

licenses(["notice"])

################################################################################
#                         Targets for fuzz tests
################################################################################

# TODO(b/229424516): [impl] simplify these targets by either moving them to
#  puzzles/ subdir, or making the build rules in this file simpler.

# Example of using the bazel transitions to build the fuzz target binary
# with the custom flags required to run under Centipede.
# re2_fuzzer is chosen as an arbitrary third_party fuzz target.
centipede_test_target_sancov(
    name = "target_example",
    fuzz_target = "//third_party/re2/fuzzing:re2_fuzzer",
)

centipede_test_target(
    name = "abort_fuzz_target",
)

# Transition of :abort_fuzz_target.
centipede_test_target_sancov(
    name = "abort_fuzz_target_sancov",
    fuzz_target = "abort_fuzz_target",
)

# This fuzz target is not currently used by any automated tests
# and is here for manual tests only.
centipede_test_target(
    name = "multi_sanitizer_test",
)

centipede_test_target(
    name = "path_stress_fuzz_target",
)

# TODO(kcc): [impl] create a test that ensures we can find a crashing input in
#  this target.
centipede_test_target_sancov(
    name = "path_stress_fuzz_target_sancov",
    fuzz_target = "path_stress_fuzz_target",
    sancov = "trace-pc-guard",  # Only this instrumentation to ensure it works.
)

# Simple fuzz target used for testing.
centipede_test_target(
    name = "test_fuzz_target",
)

# Transition of :test_fuzz_target.
centipede_test_target_sancov(
    name = "test_fuzz_target_sancov",
    fuzz_target = "test_fuzz_target",
)

# Target instrumented with -fsanitize-coverage=trace-pc.
centipede_test_target_sancov(
    name = "test_fuzz_target_trace_pc",
    fuzz_target = "test_fuzz_target",
    sancov = "trace-pc",
)

# Empty fuzz target for testing weak_sancov_stubs.
centipede_test_target(
    name = "empty_fuzz_target",
    data = [
        "//third_party/centipede",
    ],
)

centipede_test_target_sancov(
    name = "empty_fuzz_target_sancov",
    fuzz_target = "empty_fuzz_target",
)

# TODO(ussuri): [impl] move this and all other
#  cc_fuzz_target/centipede_test_target_sancov into a separate subdir, and
#  simplify the BUILD syntax, so that adding more test targets is easier.
centipede_test_target(
    name = "threaded_fuzz_target",
)

# Test fuzz target with lots of threads.
centipede_test_target_sancov(
    name = "threaded_fuzz_target_sancov",
    fuzz_target = "threaded_fuzz_target",
    sancov = "trace-pc-guard",  # Only this isntrumentation to ensure it works.
)

################################################################################
#                            Helper binaries
################################################################################

sh_binary(
    name = "test_input_filter",
    srcs = ["test_input_filter.sh"],
)

################################################################################
#                              Fuzz tests
################################################################################

cc_test(
    name = "util_test",
    srcs = ["util_test.cc"],
    deps = [
        "//testing/base/public:gunit_main",
        "//third_party/centipede:defs",
        "//third_party/centipede:logging",
        "//third_party/centipede:util",
    ],
)

cc_test(
    name = "blob_file_test",
    srcs = ["blob_file_test.cc"],
    deps = [
        "//testing/base/public:gunit_main",
        "//third_party/centipede:blob_file",
        "//third_party/centipede:util",
        "@com_google_absl//absl/status",
    ],
)

cc_test(
    name = "shared_memory_blob_sequence_test",
    srcs = ["shared_memory_blob_sequence_test.cc"],
    deps = [
        "//testing/base/public:gunit_main",
        "//third_party/centipede:shared_memory_blob_sequence",
    ],
)

cc_test(
    name = "execution_result_test",
    srcs = ["execution_result_test.cc"],
    deps = [
        "//testing/base/public:gunit_main",
        "//third_party/centipede:execution_result",
        "//third_party/centipede:feature",
        "//third_party/centipede:shared_memory_blob_sequence",
    ],
)

cc_test(
    name = "byte_array_mutator_test",
    srcs = ["byte_array_mutator_test.cc"],
    deps = [
        "//testing/base/public:gunit_main",
        "//third_party/centipede:byte_array_mutator",
        "//third_party/centipede:defs",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

cc_test(
    name = "coverage_test",
    srcs = ["coverage_test.cc"],
    data = [
        ":path_stress_fuzz_target_sancov",
        ":test_fuzz_target_sancov",
        ":test_fuzz_target_trace_pc",
        ":threaded_fuzz_target_sancov",
        "//third_party/crosstool/google3_users:llvm-symbolizer",
    ],
    deps = [
        "//devtools/build/runtime:get_runfiles_dir",
        "//testing/base/public:gunit_main",
        "//third_party/centipede:centipede_interface",
        "//third_party/centipede:coverage",
        "//third_party/centipede:defs",
        "//third_party/centipede:environment",
        "//third_party/centipede:execution_result",
        "//third_party/centipede:feature",
        "//third_party/centipede:logging",
        "//third_party/centipede:util",
        "@com_google_absl//absl/strings",
    ],
)

cc_test(
    name = "feature_test",
    srcs = ["feature_test.cc"],
    deps = [
        "//testing/base/public:gunit_main",
        "//third_party/centipede:feature",
        "//third_party/centipede:logging",
        "@com_google_absl//absl/container:flat_hash_set",
    ],
)

cc_test(
    name = "corpus_test",
    srcs = ["corpus_test.cc"],
    deps = [
        "//testing/base/public:gunit_main",
        "//third_party/centipede:corpus",
        "//third_party/centipede:defs",
        "//third_party/centipede:util",
    ],
)

cc_test(
    name = "command_test",
    srcs = ["command_test.cc"],
    deps = [
        "//testing/base/public:gunit_main",
        "//third_party/centipede:command",
        "//third_party/centipede:util",
    ],
)

cc_test(
    name = "centipede_test",
    srcs = ["centipede_test.cc"],
    data = [
        ":abort_fuzz_target_sancov",
        ":empty_fuzz_target_sancov",
        ":test_fuzz_target_sancov",
        ":test_input_filter",
    ],
    deps = [
        "//devtools/build/runtime:get_runfiles_dir",
        "//testing/base/public:gunit_main",
        "//third_party/centipede:centipede_callbacks",
        "//third_party/centipede:centipede_interface",
        "//third_party/centipede:centipede_lib",
        "//third_party/centipede:defs",
        "//third_party/centipede:environment",
        "//third_party/centipede:execution_result",
        "//third_party/centipede:feature",
        "//third_party/centipede:logging",
        "//third_party/centipede:util",
        "@com_google_absl//absl/container:flat_hash_set",
        "@com_google_absl//absl/strings",
    ],
)

# Runs common Centipede scenarios with local files.
sh_test(
    name = "centipede_main_test",
    srcs = ["centipede_main_test.sh"],
    data = [
        ":abort_fuzz_target_sancov",
        ":empty_fuzz_target_sancov",
        ":target_example",
        ":test_fuzz_target_sancov",
        "//third_party/centipede",
    ],
    deps = [
        "//testing/shbase",
    ],
)

# Runs common Centipede scenarios in multiple threads.
sh_test(
    name = "centipede_threaded_test",
    srcs = ["centipede_threaded_test.sh"],
    data = [
        ":target_example",
        "//third_party/centipede",
    ],
    deps = [
        "//testing/shbase",
    ],
)

# Test for fuzz_target_runner.
sh_test(
    name = "runner_test",
    srcs = ["runner_test.sh"],
    data = [
        ":test_fuzz_target_sancov",
    ],
    deps = [
        "//testing/shbase",
    ],
)

bzl_library(
    name = "build_defs_bzl",
    srcs = ["build_defs.bzl"],
    parse_tests = False,
    visibility = ["//visibility:private"],
    deps = ["//security/fuzzing/bazel:cc_fuzz_target_bzl"],
)
