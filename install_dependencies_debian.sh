#!/bin/bash
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

# Tested on Debian GNU/Linux 11 (bullseye)
#
# * git: to get the Centipede sources.
# * bazel: to build Centipede.
# * libssl-dev: to link Centipede (it uses SHA1).
# * binutils: Centipede uses objdump.
# * clang: to build Centipede and the targets.
#   For most of the functionality clang 11 or newer will work.
#   To get all of the functionality you may need to install fresh clang from
#   source: https://llvm.org/.
#   The functionality currently requiring fresh clang from source:
#     * -fsanitize-coverage=trace-loads
#     (https://clang.llvm.org/docs/SanitizerCoverage.html#tracing-data-flow)

# Add Bazel distribution URI as a package source following:
# https://docs.bazel.build/versions/main/install-ubuntu.html
apt install -y curl gnupg apt-transport-https
curl -fsSL https://bazel.build/bazel-release.pub.gpg \
  | gpg --dearmor > /etc/apt/trusted.gpg.d/bazel.gpg
echo "deb [arch=amd64] https://storage.googleapis.com/bazel-apt stable jdk1.8" \
  | tee /etc/apt/sources.list.d/bazel.list
apt update

# Install dependencies.
apt install -y git bazel binutils libssl-dev

# Get Clang-14, the earlist version that supports dataflow tracing:
#   * Download Clang from Chromium to support old OS (e.g. Ubuntu 16).
#   * Alternatively, download the fresh Clang from https://releases.llvm.org/
mkdir /clang
curl https://commondatastorage.googleapis.com/chromium-browser-clang/Linux_x64/clang-llvmorg-14-init-9436-g65120988-1.tgz -o /clang-14.tgz
tar zxvf /clang-14.tgz -C /clang

# * TODO(kcc): llvm-symbolizer is required for running Centipede.
#   it comes with clang, but may be called e.g. llvm-symbolizer-11
