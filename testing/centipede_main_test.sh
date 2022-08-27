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
  EXAMPLE_TARGET_BINARY "${CENTIPEDE_TEST_SRCDIR}/testing/example_fuzz_target"
centipede::maybe_set_var_to_executable_path \
  TEST_TARGET_BINARY "${CENTIPEDE_TEST_SRCDIR}/testing/test_fuzz_target"
centipede::maybe_set_var_to_executable_path \
  ABORT_TEST_TARGET_BINARY "${CENTIPEDE_TEST_SRCDIR}/testing/abort_fuzz_target"
centipede::maybe_set_var_to_executable_path \
  LLVM_SYMBOLIZER "$(centipede::get_llvm_symbolizer_path)"

# Shorthand for centipede --binary=example_fuzz_target.
example_fuzz() {
  "${CENTIPEDE_BINARY}" --binary="${EXAMPLE_TARGET_BINARY}" "$@"
}
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

# Roundtrip test for save_corpus_to_local_dir and export_corpus_from_local_dir.
# Saves the corpus from workdir to dir1, exports dir1 to temporary workdir,
# then saves it again to dir2. dir1 and dir2 should have the same files.
# Also tests --corpus_dir.
# $1 is an existing workdir with a non-empty corpus.
# $2 is total_shards.
test_save_and_export() {
  WD="$1"
  N_SHARDS="$2"
  SAVE_TO_DIR1="${TEST_TMPDIR}/saveto1"
  SAVE_TO_DIR2="${TEST_TMPDIR}/saveto2"
  TEMP_WD1="${TEST_TMPDIR}/TEMP_WD1"
  ensure_empty_dir "${SAVE_TO_DIR1}"
  ensure_empty_dir "${SAVE_TO_DIR2}"
  ensure_empty_dir "${TEMP_WD1}"

  echo "========== save_corpus_to_local_dir: ${WD} => ${SAVE_TO_DIR1}"
  example_fuzz -workdir="${WD}" --total_shards="${N_SHARDS}" \
    --save_corpus_to_local_dir="${SAVE_TO_DIR1}"

  [[ "$(ls -A "${SAVE_TO_DIR1}")" ]] || die "${SAVE_TO_DIR1} is empty"

  echo "========== export_corpus_from_local_dir: ${SAVE_TO_DIR1} => ${TEMP_WD1}"
  example_fuzz -workdir="${TEMP_WD1}" --total_shards="${N_SHARDS}" \
    --export_corpus_from_local_dir="${SAVE_TO_DIR1}"

  echo "========== save_corpus_to_local_dir: ${TEMP_WD1} => ${SAVE_TO_DIR2}"
  example_fuzz -workdir="${TEMP_WD1}" --total_shards="${N_SHARDS}" \
    --save_corpus_to_local_dir="${SAVE_TO_DIR2}"

  echo "========== diff -r ${SAVE_TO_DIR1} ${SAVE_TO_DIR2} -- must be equal"
  diff -r "${SAVE_TO_DIR1}" "${SAVE_TO_DIR2}"

  echo "========== fuzz with --corpus_dir=${SAVE_TO_DIR1}"
  ensure_empty_dir "${TEMP_WD1}"
  example_fuzz -workdir="${TEMP_WD1}"  --corpus_dir="${SAVE_TO_DIR1}" \
    --num_runs=1000
  # SAVE_TO_DIR1 should have some files that SAVE_TO_DIR2 doesn't have.
  diff -r "${SAVE_TO_DIR1}" "${SAVE_TO_DIR2}" | grep "Only in ${SAVE_TO_DIR1}"

  echo "========== fuzz with an empty corpus element in --corpus_dir"
  touch "${SAVE_TO_DIR1}"/empty_file  # file with zero bytes.
  example_fuzz -workdir="${TEMP_WD1}"  --corpus_dir="${SAVE_TO_DIR1}" \
    --num_runs=10000
}

# Creates a workdir passed in $1 and performs some basic fuzzing runs.
run_some_fuzzing() {
  WD="$1"
  LOG="${TEST_TMPDIR}/log"
  rm -r -f "${WD}"
  mkdir "${WD}"
  echo "========== First run: 100 runs in batches of 7"
  example_fuzz -workdir="${WD}" -num_runs 100 --batch_size=7 2>&1 | tee "${LOG}"
  grep '\[100\] end-fuzz:' "${LOG}"  # Check the number of runs.
  grep_log "${LOG}"
  grep 'No custom mutator detected in the target' "${LOG}"
  ls -l "${WD}"
  echo "========== Second run: 300 runs in batches of 8"
  example_fuzz -workdir="${WD}" -num_runs 300 --batch_size=8 2>&1 | tee "${LOG}"
  grep '\[300\] end-fuzz:' "${LOG}"  # Check the number of runs.
  grep_log "${LOG}"
  ls -l "${WD}"

  N_SHARDS=3
  echo "========== Running ${N_SHARDS} shards"
  for ((s=0; s < "${N_SHARDS}"; s++)); do
    example_fuzz --workdir="${WD}" -num_runs 100 --first_shard_index="$s" \
      --total_shards="${N_SHARDS}" 2>&1 | tee "${LOG}.${s}" &
  done
  wait
  echo "========== Shards finished, checking output"
  for ((s=0; s < "${N_SHARDS}"; s++)); do
    grep -q "centipede.cc.*end-fuzz:" "${LOG}.${s}"
  done
  grep_log "${LOG}".*

  ls -l "${WD}"

  test_save_and_export "${WD}" "${N_SHARDS}"

  rm -r "${WD}"
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

# Creates workdir ($1) and tests how the debug symbols are shown in the output.
test_debug_symbols() {
  WD="$1"
  TMPCORPUS="${TEST_TMPDIR}/C"
  LOG="${TEST_TMPDIR}/log"
  ensure_empty_dir "${WD}"
  ensure_empty_dir "${TMPCORPUS}"
  echo -n "func1" > "${TMPCORPUS}/func1"  # induces a call to SingleEdgeFunc.
  echo -n "func2-A" > "${TMPCORPUS}/func2-A"  # induces a call to MultiEdgeFunc.

  echo ============ run for the first time, with empty seed corpus, with --v=1
  test_fuzz --v=1 --workdir="${WD}" --seed=1 --num_runs=1000 \
    --symbolizer_path="${LLVM_SYMBOLIZER}" 2>&1 | tee "${LOG}"
  grep 'Custom mutator detected in the target, will use it' "${LOG}"
  echo "============ ensure we have LLVMFuzzerTestOneInput in the output."
  # Note: the test assumes LLVMFuzzerTestOneInput is defined on a specific line.
  grep "FUNC: LLVMFuzzerTestOneInput third_party/centipede/testing/test_fuzz_target.cc:53" "${LOG}"
  grep "EDGE: LLVMFuzzerTestOneInput third_party/centipede/testing/test_fuzz_target.cc" "${LOG}"
  grep "FUNC:" "${LOG}"
  echo "============  add func1/func2-A inputs to the corpus."
  test_fuzz --workdir="${WD}" --export_corpus_from_local_dir="${TMPCORPUS}" \
    --alsologtostderr=false
  echo "============ run again, append to the same LOG file."
  test_fuzz --v=2 --workdir="${WD}" --seed=1 --num_runs=0 \
    --symbolizer_path="${LLVM_SYMBOLIZER}" 2>&1 | tee -a "${LOG}"
  echo "============ ensure we have SingleEdgeFunc/MultiEdgeFunc in the output."
  grep "FUNC: SingleEdgeFunc" "${LOG}"
  grep "FUNC: MultiEdgeFunc" "${LOG}"
  grep "EDGE: MultiEdgeFunc" "${LOG}"
  echo "============ checking the coverage report"
  COV_REPORT="${WD}/coverage-report-$(basename "${TEST_TARGET_BINARY}").0.txt"
  grep "GenerateCoverageReport: ${COV_REPORT}" "${LOG}"
  grep "FULL: SingleEdgeFunc" "${COV_REPORT}"
  grep "PARTIAL: LLVMFuzzerTestOneInput" "${COV_REPORT}"
  echo "============ run w/o the symbolizer, everything else should still work."
  ensure_empty_dir "${WD}"
  test_fuzz --workdir="${WD}" --seed=1 --num_runs=1000 \
    --symbolizer_path=/dev/null 2>&1 | tee "${LOG}"
  grep "symbolization failed, debug symbols will not be used" "${LOG}"
  grep "end-fuzz:" "${LOG}"
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

run_some_fuzzing "${TEST_TMPDIR}/WD"
test_crashing_target "${TEST_TMPDIR}/WD"
test_debug_symbols "${TEST_TMPDIR}/WD"
test_dictionary "${TEST_TMPDIR}/WD"
test_for_each_blob "${TEST_TMPDIR}/WD"
test_pcpair_features "${TEST_TMPDIR}/WD"

echo "PASS"
