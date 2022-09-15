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
  set -x
  "${CENTIPEDE_BINARY}" --binary="${TEST_TARGET_BINARY}" "$@" 2>&1
  set +x
}

# Shorthand for centipede --binary=abort_fuzz_target
abort_test_fuzz() {
  set -x
  "${CENTIPEDE_BINARY}" --binary="${ABORT_TEST_TARGET_BINARY}" "$@" 2>&1
  set +x
}

# Creates workdir ($1) and tests fuzzing with a target that crashes.
test_crashing_target() {
  FUNC="${FUNCNAME[0]}"
  WD="${TEST_TMPDIR}/${FUNC}/WD"
  TMPCORPUS="${TEST_TMPDIR}/${FUNC}/C"
  LOG="${TEST_TMPDIR}/${FUNC}/log"
  centipede::ensure_empty_dir "${WD}"
  centipede::ensure_empty_dir "${TMPCORPUS}"

  # Create a corpus with one crasher and one other input.
  echo -n "AbOrT" > "${TMPCORPUS}/AbOrT"  # induces abort in the target.
  echo -n "foo" > "${TMPCORPUS}/foo"  # just some input.
  abort_test_fuzz --workdir="${WD}" --export_corpus_from_local_dir="${TMPCORPUS}"

  # Run fuzzing with num_runs=0, i.e. only run the inputs from the corpus.
  # Expecting a crash to be observed and reported.
  abort_test_fuzz --workdir="${WD}" --num_runs=0 | tee "${LOG}"
  centipede::assert_regex_in_file "2 inputs to rerun" "${LOG}"
  centipede::assert_regex_in_file "Batch execution failed; exit code:" "${LOG}"

  # Comes from test_fuzz_target.cc
  centipede::assert_regex_in_file "I AM ABOUT TO ABORT" "${LOG}"
}

# Creates workdir ($1) and tests how dictionaries are loaded.
test_dictionary() {
  FUNC="${FUNCNAME[0]}"
  WD="${TEST_TMPDIR}/${FUNC}/WD"
  TMPCORPUS="${TEST_TMPDIR}/${FUNC}/C"
  DICT="${TEST_TMPDIR}/${FUNC}/dict"
  LOG="${TEST_TMPDIR}/${FUNC}/log"
  centipede::ensure_empty_dir "${WD}"
  centipede::ensure_empty_dir "${TMPCORPUS}"

  echo "============ ${FUNC}: testing non-existing dictionary file"
  test_fuzz  --workdir="${WD}" --num_runs=0 --dictionary=/dev/null | tee "${LOG}"
  centipede::assert_regex_in_file "Empty or corrupt dictionary file: /dev/null" "${LOG}"

  echo "============ ${FUNC}: testing plain text dictionary file"
  echo '"blah"' > "${DICT}"
  echo '"boo"' >> "${DICT}"
  echo '"bazz"' >> "${DICT}"
  cat "${DICT}"
  test_fuzz  --workdir="${WD}" --num_runs=0 --dictionary="${DICT}" | tee "${LOG}"
  centipede::assert_regex_in_file "Loaded 3 dictionary entries from AFL/libFuzzer dictionary ${DICT}" "${LOG}"

  echo "============ ${FUNC}: creating a binary dictionary file with 2 entries"
  echo "foo" > "${TMPCORPUS}"/foo
  echo "bat" > "${TMPCORPUS}"/binary
  centipede::ensure_empty_dir "${WD}"
  test_fuzz  --workdir="${WD}" --export_corpus_from_local_dir "${TMPCORPUS}"
  cp "${WD}/corpus.0" "${DICT}"

  echo "============ ${FUNC}: testing binary dictionary file"
  centipede::ensure_empty_dir "${WD}"
  test_fuzz  --workdir="${WD}" --num_runs=0 --dictionary="${DICT}" | tee "${LOG}"
  centipede::assert_regex_in_file "Loaded 2 dictionary entries from ${DICT}" "${LOG}"
}

# Creates workdir ($1) and tests --for_each_blob.
test_for_each_blob() {
  FUNC="${FUNCNAME[0]}"
  WD="${TEST_TMPDIR}/${FUNC}/WD"
  TMPCORPUS="${TEST_TMPDIR}/${FUNC}/C"
  LOG="${TEST_TMPDIR}/${FUNC}/log"
  centipede::ensure_empty_dir "${WD}"
  centipede::ensure_empty_dir "${TMPCORPUS}"

  echo "FoO" > "${TMPCORPUS}"/a
  echo "bAr" > "${TMPCORPUS}"/b

  test_fuzz  --workdir="${WD}" --export_corpus_from_local_dir "${TMPCORPUS}"
  echo "============ ${FUNC}: test for_each_blob"
  test_fuzz --for_each_blob="cat %P"  "${WD}"/corpus.0 | tee "${LOG}"
  centipede::assert_regex_in_file "Running 'cat %P' on ${WD}/corpus.0" "${LOG}"
  centipede::assert_regex_in_file FoO "${LOG}"
  centipede::assert_regex_in_file bAr "${LOG}"
}

# Creates workdir ($1) and tests --use_pcpair_features.
test_pcpair_features() {
  FUNC="${FUNCNAME[0]}"
  WD="${TEST_TMPDIR}/${FUNC}/WD"
  LOG="${TEST_TMPDIR}/${FUNC}/log"
  centipede::ensure_empty_dir "${WD}"

  echo "============ ${FUNC}: fuzz with --use_pcpair_features=1"
  test_fuzz --workdir="${WD}" --use_pcpair_features=1  --num_runs=10000 \
    --symbolizer_path="${LLVM_SYMBOLIZER}" | tee "${LOG}"
  centipede::assert_regex_in_file "end-fuzz.*pair: [^0]" "${LOG}"

  echo "============ ${FUNC}: fuzz with --use_pcpair_features=1 w/o symbolizer"
  test_fuzz --workdir="${WD}" --use_pcpair_features=1  --num_runs=10000 \
    --symbolizer_path=/dev/null  | tee "${LOG}"
  centipede::assert_regex_in_file "end-fuzz.*pair: [^0]" "${LOG}"
}

test_crashing_target
test_dictionary
test_for_each_blob
test_pcpair_features

echo "PASS"
