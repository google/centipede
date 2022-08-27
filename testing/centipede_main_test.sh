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

# Test common scenarios of Centipede.

set -eu

source "$(dirname "$0")/../test_util.sh"

CENTIPEDE_TEST_SRCDIR="$(centipede::get_centipede_test_srcdir)"

# The following variables can be overridden externally by passing --test_env to
# the build command, e.g. --test_env=EXAMPLE_TARGET_BINARY="/some/path".
centipede::maybe_set_var_to_executable_path \
  CENTIPEDE_BINARY "${CENTIPEDE_TEST_SRCDIR}/centipede_main"
centipede::maybe_set_var_to_executable_path \
  TEST_TARGET_BINARY "${CENTIPEDE_TEST_SRCDIR}/testing/test_fuzz_target"
centipede::maybe_set_var_to_executable_path \
  ABORT_TEST_TARGET_BINARY "${CENTIPEDE_TEST_SRCDIR}/testing/abort_fuzz_target"
centipede::maybe_set_var_to_executable_path \
  LLVM_SYMBOLIZER "$(centipede::get_llvm_symbolizer_path)"

# Shorthand for centipede --binary=test_fuzz_target
test_fuzz() {
  "${CENTIPEDE_BINARY}" --binary="${TEST_TARGET_BINARY}" "$@"
}
# Shorthand for centipede --binary=abort_fuzz_target
abort_test_fuzz() {
  "${CENTIPEDE_BINARY}" --binary="${ABORT_TEST_TARGET_BINARY}" "$@"
}

grep_log() {
  echo "====== LOGS: $*"
  for f in "init-done:" "end-fuzz:"; do
    grep "centipede.*${f}" "$@"
    echo
  done
}

ensure_empty_dir() {
  rm -rf "$1" && mkdir "$1"
}

# Creates workdir ($1) and tests fuzzing with a target that crashes.
test_crashing_target() {
  WD="$1"
  TMPCORPUS="${TEST_TMPDIR}/C"
  LOG="${TEST_TMPDIR}/log"
  ensure_empty_dir "${WD}"
  ensure_empty_dir "${TMPCORPUS}"
  # Create a corpus with one crasher and one other input.
  echo -n "AbOrT" > "${TMPCORPUS}/AbOrT"  # induces abort in the target.
  echo -n "foo" > "${TMPCORPUS}/foo"  # just some input.
  abort_test_fuzz --workdir="${WD}" --export_corpus_from_local_dir="${TMPCORPUS}"
  # Run fuzzing with num_runs=0, i.e. only run the inputs from the corpus.
  # Expecting a crash to be observed and reported.
  abort_test_fuzz --workdir="${WD}" --num_runs=0 2>&1 | tee "${LOG}"
  cat "${LOG}"
  grep "2 inputs to rerun" "${LOG}"
  grep "Batch execution failed; exit code:" "${LOG}"
  # Comes from test_fuzz_target.cc
  grep "I AM ABOUT TO ABORT" "${LOG}"
}

# Creates workdir ($1) and tests how dictionaries are loaded.
test_dictionary() {
  WD="$1"
  TMPCORPUS="${TEST_TMPDIR}/C"
  DICT="${TEST_TMPDIR}/dict"
  ensure_empty_dir "${WD}"
  ensure_empty_dir "${TMPCORPUS}"

  echo "======================= testing non-existing dictionary file"
  test_fuzz  --workdir="${WD}" --num_runs=0 --dictionary=/dev/null 2>&1 |\
    grep "empty or corrupt dictionary file: /dev/null"

  echo "======================= testing plain text dictionary file"
  echo '"blah"' > "${DICT}"
  echo '"boo"' >> "${DICT}"
  echo '"bazz"' >> "${DICT}"
  cat "${DICT}"
  test_fuzz  --workdir="${WD}" --num_runs=0 --dictionary="${DICT}" 2>&1 |\
    grep "loaded 3 dictionary entries from AFL/libFuzzer dictionary ${DICT}"

  echo "====================== creating a binary dictionary file with 2 entries"
  echo "foo" > "${TMPCORPUS}"/foo
  echo "bat" > "${TMPCORPUS}"/binary
  ensure_empty_dir "${WD}"
  test_fuzz  --workdir="${WD}" --export_corpus_from_local_dir "${TMPCORPUS}"
  cp "${WD}/corpus.0" "${DICT}"

  echo "====================== testing binary dictionary file"
  ensure_empty_dir "${WD}"
  test_fuzz  --workdir="${WD}" --num_runs=0 --dictionary="${DICT}" 2>&1 |\
    grep "loaded 2 dictionary entries from ${DICT}"
}

# Creates workdir ($1) and tests --for_each_blob.
test_for_each_blob() {
  LOG="${TEST_TMPDIR}/log"
  WD="$1"
  TMPCORPUS="${TEST_TMPDIR}/C"
  ensure_empty_dir "${WD}"
  ensure_empty_dir "${TMPCORPUS}"
  echo "FoO" > "${TMPCORPUS}"/a
  echo "bAr" > "${TMPCORPUS}"/b
  test_fuzz  --workdir="${WD}" --export_corpus_from_local_dir "${TMPCORPUS}"
  echo "============== test for_each_blob"
  test_fuzz --for_each_blob="cat %P"  "${WD}"/corpus.0 > "${LOG}" 2>&1
  grep "Running 'cat %P' on ${WD}/corpus.0" "${LOG}"
  grep FoO "${LOG}"
  grep bAr "${LOG}"
}

# Creates workdir ($1) and tests --use_pcpair_features.
test_pcpair_features() {
  LOG="${TEST_TMPDIR}/log"
  WD="$1"
  ensure_empty_dir "${WD}"

  echo "================= fuzz with --use_pcpair_features=1"
  test_fuzz --workdir="${WD}" --use_pcpair_features=1  --num_runs=10000 \
   --symbolizer_path="${LLVM_SYMBOLIZER}"  > "${LOG}" 2>&1
  grep "end-fuzz.*pair: [^0]" "${LOG}"  # check the output

  echo "================= fuzz with --use_pcpair_features=1 w/o symbolizer"
  test_fuzz --workdir="${WD}" --use_pcpair_features=1  --num_runs=10000 \
   --symbolizer_path=/dev/null  > "${LOG}" 2>&1
  grep "end-fuzz.*pair: [^0]" "${LOG}"  # check the output
}

test_crashing_target "${TEST_TMPDIR}/WD"
test_dictionary "${TEST_TMPDIR}/WD"
test_for_each_blob "${TEST_TMPDIR}/WD"
test_pcpair_features "${TEST_TMPDIR}/WD"

echo "PASS"
