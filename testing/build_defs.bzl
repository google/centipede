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

"""This module contains rules that build a cc_fuzz_target with go/sancov.

To instrument a target with sancov, we apply a bazel transition
(go/bazel-configurability-api#transitions) to change its configuration (i.e.,
add the necessary compilation flags). The configuration will affect all its
transitive dependencies as well.
"""

load("//security/fuzzing/bazel:cc_fuzz_target.bzl", "cc_fuzz_target")

# Change the flags from the default ones to sancov:
# https://clang.llvm.org/docs/SanitizerCoverage.html.
def _sancov_transition_impl(settings, attr):
    features_to_strip = ["asan", "tsan", "msan"]
    filtered_features = [x for x in settings["//command_line_option:features"] if x not in features_to_strip]

    # some of the valid sancov flag combinations:
    # trace-pc-guard,pc-table
    # trace-pc-guard,pc-table,trace-cmp
    # trace-pc-guard,pc-table,trace-loads
    sancov = "-fsanitize-coverage=" + attr.sancov

    return {
        "//command_line_option:copt": settings["//command_line_option:copt"] + [
            "-O1",
            "-fno-builtin",  # prevent memcmp & co from inlining.
            sancov,
            "-gline-tables-only",  # debug info, for coverage reporting tools.
            # https://llvm.org/docs/LibFuzzer.html#fuzzer-friendly-build-mode
            "-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION",
        ],
        "//command_line_option:linkopt": settings["//command_line_option:linkopt"] + [
            # runner_interceptors defines its own memcmp/etc,
            # which triggers the linker's --warn-backrefs. Selectively disable the warning.
            # https://lld.llvm.org/ELF/warn_backrefs.html
            "-Wl,--warn-backrefs-exclude=*/centipede*runner_interceptors.o",
        ],
        # Disable tcmalloc to avoid coverage features from it.
        "//command_line_option:compilation_mode": "opt",
        "//command_line_option:custom_malloc": "//base:system_malloc",
        "//security/fuzzing/bazel:fuzzing_engine": "centipede",
        "//command_line_option:strip": "never",  # preserve debug info.
        "//command_line_option:features": filtered_features,
        "//command_line_option:compiler": None,
    }

sancov_transition = transition(
    implementation = _sancov_transition_impl,
    inputs = [
        "//command_line_option:copt",
        "//command_line_option:linkopt",
        "//command_line_option:features",
    ],
    outputs = [
        "//command_line_option:copt",
        "//command_line_option:linkopt",
        "//security/fuzzing/bazel:fuzzing_engine",
        "//command_line_option:compilation_mode",
        "//command_line_option:custom_malloc",
        "//command_line_option:strip",
        "//command_line_option:features",
        "//command_line_option:compiler",
    ],
)

def __sancov_fuzz_target_impl(ctx):
    # We need to copy the executable because starlark doesn't allow
    # providing an executable not created by the rule
    executable_src = ctx.executable.fuzz_target
    executable_dst = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.run_shell(
        inputs = [executable_src],
        outputs = [executable_dst],
        command = "cp %s %s" % (executable_src.path, executable_dst.path),
    )

    # See https://docs.bazel.build/versions/master/skylark/lib/DefaultInfo.html
    runfiles = ctx.runfiles(
        collect_data = True,
    )
    return [DefaultInfo(runfiles = runfiles, executable = executable_dst)]

__sancov_fuzz_target = rule(
    implementation = __sancov_fuzz_target_impl,
    attrs = {
        "fuzz_target": attr.label(
            cfg = sancov_transition,
            executable = True,
            mandatory = True,
        ),
        "_allowlist_function_transition": attr.label(
            default = "//tools/allowlists/function_transition_allowlist",
        ),
        "sancov": attr.string(),
    },
    executable = True,
)

# Wrapper for cc_fuzz_target to build it with go/sancov.
# By default it uses some pre-defined set of sancov instrumentations.
# It can be overridden with more advanced ones, see _sancov_transition_impl.
def centipede_test_target_sancov(name, sancov = "trace-pc-guard,pc-table,trace-loads,trace-cmp", **kwargs):
    """Generates a cc_fuzz_target target instrumented with sancov.

    Args:
      name: A unique name for this target
      sancov: The sancov instrumentations to use
      **kwargs: All other args
    """

    # Using [] as default instead of None fails with package default
    # visibility.
    visibility = kwargs.pop("visibility", None)
    fuzz_target = kwargs.pop("fuzz_target", None)

    __sancov_fuzz_target(
        name = name,
        fuzz_target = fuzz_target,
        visibility = visibility,
        testonly = True,
        sancov = sancov,
    )

def centipede_test_target(name):
    cc_fuzz_target(
        name = name,
        srcs = [name + ".cc"],
        componentid = 1187448,  #Language Platforms > Sanitizers > Centipede
        tags = [
            # Don't test this fuzz target on TAP or autofuzz,
            # since this target is for testing Centipede, not for fuzzing itself.
            "noautofuzz",
            "notap",
            "manual",  # don't run as part of bazel test.
        ],
    )
