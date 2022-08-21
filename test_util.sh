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

# Returns the path to Centipede TEST_SRCDIR subdirectory.
function centipede::get_centipede_test_srcdir() {
  set -u
  echo "${TEST_SRCDIR}/${TEST_WORKSPACE}"
}

# Returns the path to llvm-symbolizer.
function centipede::get_llvm_symbolizer_path() {
  set -u
  local path
  path="$(which llvm-symbolizer)"
  if (( $? != 0 )); then
    echo "FAILURE: llvm-symbolizer must be installed and findable via PATH:" \
      "use install_dependencies_debian.sh to fix" >&2
    exit 1
  fi
  echo "${path}"
}

# If the var named "$1" is unset, then sets it to "$2". If the var is set,
# doesn't change it. In either case, verifies that the final value is a path to
# an executable file.
function centipede::maybe_set_var_to_executable_path() {
  local var_name="$1"
  # NOTE: `local -n` creates a reference to the var named "$1".
  local -n var_ref="$1"
  local path="$2"
  if [[ -n "${var_ref+x}" ]]; then
    echo "Not overriding ${var_name} -- already set to '${var_ref}'" >&2
  else
    echo "Setting ${var_name} to '${path}'" >&2
    var_ref="${path}"
  fi
  if ! [[ -x "${var_ref}" ]]; then
    echo "FAILURE: Path '${var_ref}' doesn't exist or is not executable" >&2
    exit 1
  fi
}
