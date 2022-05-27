# Copyright 2022 Google LLC.
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

# Bazel Skylib (transitively required by com_google_absl).
http_archive(
    name = "bazel_skylib",
    sha256 = "f7be3474d42aae265405a592bb7da8e171919d74c16f082a5457840f06054728",
    urls = ["https://github.com/bazelbuild/bazel-skylib/releases/download/1.2.1/bazel-skylib-1.2.1.tar.gz"],
)

# Bazel requires rules_cc repo to build C++ code.
http_archive(
    name = "rules_cc",
    strip_prefix = "rules_cc-262ebec3c2296296526740db4aefce68c80de7fa",
    urls = ["https://github.com/bazelbuild/rules_cc/archive/262ebec3c2296296526740db4aefce68c80de7fa.zip"],
)

# ABSL.
#
# Replace by the commented-out version to get the tip of the master branch.
#
# http_archive(
#     name = "com_google_absl",
#     strip_prefix = "abseil-cpp-master",
#     urls = ["https://github.com/abseil/abseil-cpp/archive/refs/heads/master.zip"],
#)
#
http_archive(
    name = "com_google_absl",
    sha256 = "dcf71b9cba8dc0ca9940c4b316a0c796be8fab42b070bb6b7cab62b48f0e66c4",
    strip_prefix = "abseil-cpp-20211102.0",
    urls = [
        "https://github.com/abseil/abseil-cpp/archive/refs/tags/20211102.0.tar.gz",
    ],
)

# GoogleTest/GoogleMock.
http_archive(
    name = "com_google_googletest",
    sha256 = "eb70a6d4520f940956a6b3e37d205d92736bb104c6a1b2b9f82bfc41bd7a2b34",
    strip_prefix = "googletest-28e1da21d8d677bc98f12ccc7fc159ff19e8e817",
    urls = ["https://github.com/google/googletest/archive/28e1da21d8d677bc98f12ccc7fc159ff19e8e817.zip"],
)

# OpenSSL/BoringSSL.
#http_archive(
http_archive(
    name = "boringssl",
    sha256 = "bd923e59fca0d2b50db09af441d11c844c5e882a54c68943b7fc39a8cb5dd211",
    strip_prefix = "boringssl-18637c5f37b87e57ebde0c40fe19c1560ec88813",
    url = "https://github.com/google/boringssl/archive/18637c5f37b87e57ebde0c40fe19c1560ec88813.zip",
)
