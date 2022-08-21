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

# Test common scenarios of Centipede in a multi-threaded context.

set -eu

CENTIPEDE_TEST_SRCDIR="${TEST_SRCDIR}/${TEST_WORKSPACE}"

# Main Centipede binary.
CENTIPEDE_BINARY="${CENTIPEDE_TEST_SRCDIR}/centipede_main"
# Some real-life existing fuzz target prepackaged to be used with Centipede.
EXAMPLE_BINARY="${CENTIPEDE_TEST_SRCDIR}/testing/target_example"

# Shorthand for centipede --binary=target_example.
example_fuzz() {
  "${CENTIPEDE_BINARY}" --binary="${EXAMPLE_BINARY}" "$@"
}

ensure_empty_dir() {
  rm -rf "$1" && mkdir "$1"
}

# Creates a workdir passed in $1 and performs fuzzing in many threads.
run_some_threaded_fuzzing() {
  WD="$1"
  LOG="${TEST_TMPDIR}/log"
  ensure_empty_dir "${WD}"
  # Should crash: num_threads > total_shards
  example_fuzz --workdir=${WD} --num_runs=100 --v=-1 \
    --num_threads=5 --total_shards=3 2>&1 | grep Check.failed
  # Should crash: first_shard_index + num_threads > total_shards
  example_fuzz --workdir=${WD} --num_runs=100 --v=-1 \
    --num_threads=5 --total_shards=7 --first_shard_index=4 2>&1 | grep Check.failed

  for ((iter = 0; iter < 3; iter++)); do
    ensure_empty_dir "${WD}"
    # Actual run, empty WD
    example_fuzz --workdir="${WD}" --num_runs=1000 --v=-1 \
      --num_threads=5 --total_shards=5 2>&1 | tee "${LOG}"
    grep "end-fuzz:" "${LOG}"
    # Actual run, non-empty WD
    example_fuzz --workdir="${WD}" --num_runs=1000 --v=-1 \
      --num_threads=5 --total_shards=5 2>&1 | tee "${LOG}"
    grep "end-fuzz:" "${LOG}"
    # Check that the seeds are different in every thread.
    seed0=$(grep "shard: 0/5" "${LOG}" | grep -o "seed:.[0-9]*")
    seed1=$(grep "shard: 1/5" "${LOG}" | grep -o "seed:.[0-9]*")
    [[ "${seed0}" != "${seed1}" ]] || die "each thread must have its own seed"
  done
}

run_some_threaded_fuzzing "${TEST_TMPDIR}/WD"

echo "PASS"
