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

workspace(name = "centipede")

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

###############################################################################
# Bazel Skylib (transitively required by com_google_absl).
###############################################################################

skylib_ver = "1.2.1"

http_archive(
    name = "bazel_skylib",
    sha256 = "f7be3474d42aae265405a592bb7da8e171919d74c16f082a5457840f06054728",
    url = "https://github.com/bazelbuild/bazel-skylib/releases/download/%s/bazel-skylib-%s.tar.gz" % (skylib_ver, skylib_ver),
)

###############################################################################
# C++ build rules
# Configure the bootstrapped Clang and LLVM toolchain for Bazel.
###############################################################################

rules_cc_ver = "262ebec3c2296296526740db4aefce68c80de7fa"

http_archive(
    name = "rules_cc",
    sha256 = "9a446e9dd9c1bb180c86977a8dc1e9e659550ae732ae58bd2e8fd51e15b2c91d",
    strip_prefix = "rules_cc-%s" % rules_cc_ver,
    url = "https://github.com/bazelbuild/rules_cc/archive/%s.zip" % rules_cc_ver,
)

###############################################################################
# Abseil
###############################################################################

abseil_ref = "tags"

abseil_ver = "20211102.0"

# Use these values to get the tip of the master branch:
# abseil_ref = "heads"
# abseil_ver = "master"

http_archive(
    name = "com_google_absl",
    sha256 = "dcf71b9cba8dc0ca9940c4b316a0c796be8fab42b070bb6b7cab62b48f0e66c4",
    strip_prefix = "abseil-cpp-%s" % abseil_ver,
    url = "https://github.com/abseil/abseil-cpp/archive/refs/%s/%s.tar.gz" % (abseil_ref, abseil_ver),
)

###############################################################################
# GoogleTest/GoogleMock
###############################################################################

# Version as of 2021-12-07.
googletest_ver = "4c5650f68866e3c2e60361d5c4c95c6f335fb64b"

http_archive(
    name = "com_google_googletest",
    sha256 = "770e61fa13d51320736c2881ff6279212e4eab8a9100709fff8c44759f61d126",
    strip_prefix = "googletest-%s" % googletest_ver,
    url = "https://github.com/google/googletest/archive/%s.tar.gz" % googletest_ver,
)

###############################################################################
# BoringSSL (Google's versions of OpenSSL)
###############################################################################

boringssl_ver = "18637c5f37b87e57ebde0c40fe19c1560ec88813"

http_archive(
    name = "boringssl",
    sha256 = "7514826d98f032c16531de9f4c05e7bd05e07545ca39201d1616fa7ba3deadbc",
    strip_prefix = "boringssl-%s" % boringssl_ver,
    url = "https://github.com/google/boringssl/archive/%s.tar.gz" % boringssl_ver,
)