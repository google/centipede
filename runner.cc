// Copyright 2022 The Centipede Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Fuzz target runner (engine) for Centipede.
// Reads the input files and feeds their contents to
// the fuzz target (LLVMFuzzerTestOneInput), then dumps the coverage data.
// If the input path is "/path/to/foo",
// the coverage features are dumped to "/path/to/foo-features"
//
// WARNING: please avoid any C++ libraries here, such as Absl and (most of) STL,
// in order to avoid creating new coverage edges in the binary.
#include "./runner.h"

#include <elf.h>
#include <limits.h>
#include <link.h>  // dl_iterate_phdr
#include <pthread.h>  // NOLINT: use pthread to avoid extra dependencies.
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include "./byte_array_mutator.h"
#include "./defs.h"
#include "./execution_request.h"
#include "./execution_result.h"
#include "./feature.h"
#include "./runner_interface.h"
#include "./shared_memory_blob_sequence.h"

namespace centipede {

GlobalRunnerState state;
thread_local ThreadLocalRunnerState tls;

// Tries to write `description` to `state.failure_description_path`.
static void WriteFailureDescription(const char *description) {
  if (!state.failure_description_path) return;
  FILE *f = fopen(state.failure_description_path, "w");
  if (!f) return;
  fwrite(description, 1, strlen(description), f);
  fclose(f);
}

void ThreadLocalRunnerState::OnThreadStart() {
  LockGuard lock(state.tls_list_mu);
  // Add myself to state.tls_list.
  auto *old_list = state.tls_list;
  tls.next = old_list;
  state.tls_list = &tls;
  if (old_list) old_list->prev = &tls;
}

void ThreadLocalRunnerState::OnThreadStop() {
  LockGuard lock(state.tls_list_mu);
  // Remove myself from state.tls_list. The list never
  // becomes empty because the main thread does not call OnThreadStop().
  if (&tls == state.tls_list) {
    state.tls_list = tls.next;
    tls.prev = nullptr;
  } else {
    auto *prev_tls = tls.prev;
    auto *next_tls = tls.next;
    prev_tls->next = next_tls;
    if (next_tls) next_tls->prev = prev_tls;
  }
}

static size_t GetPeakRSSMb() {
  struct rusage usage = {};
  if (getrusage(RUSAGE_SELF, &usage)) return 0;
  // On Linux, ru_maxrss is in KiB
  return usage.ru_maxrss >> 10;
}

// Returns the current time in microseconds.
static uint64_t TimeInUsec() {
  struct timeval tv = {};
  constexpr size_t kUsecInSec = 1000000;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec * kUsecInSec + tv.tv_usec;
}

static void CheckOOM() {
  if (state.run_time_flags.rss_limit_mb > 0) {
    size_t current_rss_mb = GetPeakRSSMb();
    if (current_rss_mb > state.run_time_flags.rss_limit_mb) {
      fprintf(stderr,
              "========= OOM, RSS limit of %zdMb exceeded (%zdMb); exiting\n",
              state.run_time_flags.rss_limit_mb, current_rss_mb);
      WriteFailureDescription("out-of-memory");
      _exit(EXIT_FAILURE);
    }
  }
}

static void CheckTimeout() {
  time_t start_time = state.timer;
  time_t curr_time = time(nullptr);
  if (state.run_time_flags.timeout_in_seconds != 0) {
    if (curr_time - start_time >
        static_cast<time_t>(state.run_time_flags.timeout_in_seconds)) {
      fprintf(stderr, "========= timeout of %zd seconds exceeded; exiting\n",
              state.run_time_flags.timeout_in_seconds);
      WriteFailureDescription("timeout-exceeded");
      _exit(EXIT_FAILURE);
    }
  }
}

// Timer thread. Periodically checks if it's time to abort due to a timeout/OOM.
[[noreturn]] static void *TimerThread(void *unused) {
  while (true) {
    sleep(1);
    if (state.timer == 0) continue;  // No calls to ResetTimer() yet.

    CheckTimeout();
    CheckOOM();
  }
}

void GlobalRunnerState::StartTimerThread() {
  if (state.run_time_flags.timeout_in_seconds == 0 &&
      state.run_time_flags.rss_limit_mb == 0) {
    return;
  }
  fprintf(stderr, "timeout_in_seconds: %zd rss_limit_mb: %zd\n",
          state.run_time_flags.timeout_in_seconds,
          state.run_time_flags.rss_limit_mb);
  pthread_t timer_thread;
  pthread_create(&timer_thread, nullptr, TimerThread, nullptr);
  pthread_detach(timer_thread);
}

void GlobalRunnerState::ResetTimer() { timer = time(nullptr); }

// Byte array mutation fallback for a custom mutator, as defined here:
// https://github.com/google/fuzzing/blob/master/docs/structure-aware-fuzzing.md
extern "C" size_t LLVMFuzzerMutate(uint8_t *data, size_t size,
                                   size_t max_size) {
  // TODO(kcc): [as-needed] fix the interface mismatch.
  // LLVMFuzzerMutate is an array-based interface (for compatibility reasons)
  // while centipede::ByteArray has a vector-based interface.
  // This incompatibility causes us to do extra allocate/copy per mutation.
  // It may not cause big problems in practice though.
  centipede::ByteArray array(data, data + size);
  state.byte_array_mutator->Mutate(array);
  if (array.size() > max_size) {
    array.resize(max_size);
  }
  memcpy(data, array.data(), array.size());
  return array.size();
}

// An arbitrary large size for input data.
static const size_t kMaxDataSize = 1 << 20;
// Input data.
// Every input is stored in `input_data` and then passed to the target.
// We allocate `input_data` from heap at startup to avoid malloc in a loop.
// We don't make it a global because it will make data flow instrumentation
// treat every input byte touched as a separate feature, which will cause
// arbitrary growth of input size.
static uint8_t *input_data = (uint8_t *)malloc(kMaxDataSize);

// An arbitrary large size.
static const size_t kMaxFeatures = 1 << 20;
// FeatureArray used to accumulate features from all sources.
static centipede::FeatureArray<kMaxFeatures> features;

static void PrintErrorAndExitIf(bool condition, const char *error) {
  if (!condition) return;
  fprintf(stderr, "error: %s\n", error);
  exit(1);
}

static void WriteFeaturesToFile(FILE *file,
                                const centipede::feature_t *features,
                                size_t size) {
  if (!size) return;
  auto bytes_written = fwrite(features, 1, sizeof(features[0]) * size, file);
  PrintErrorAndExitIf(bytes_written != size * sizeof(features[0]),
                      "wrong number of bytes written for coverage");
}

// Clears all coverage data.
__attribute__((noinline))  // so that we see it in profile.
static void
PrepareCoverage() {
  if (state.run_time_flags.use_counter_features) state.counter_array.Clear();
  if (state.run_time_flags.use_dataflow_features)
    state.data_flow_feature_set.clear();
  if (state.run_time_flags.use_cmp_features) state.cmp_feature_set.clear();
  if (state.run_time_flags.use_pc_features) state.pc_feature_set.clear();
  if (state.run_time_flags.path_level) {
    state.path_feature_set.clear();
    state.ForEachTls([](centipede::ThreadLocalRunnerState &tls) {
      tls.path_ring_buffer.clear();
    });
  }
}

// Post-processes all coverage data, puts it all into `features`.
// `target_return_value` is the value returned by LLVMFuzzerTestOneInput.
//
// If `target_return_value == -1`, sets `features` to empty.  This way,
// the engine will reject any input that causes the target to return -1.
// LibFuzzer supports this return value as of 2022-07:
// https://llvm.org/docs/LibFuzzer.html#rejecting-unwanted-inputs
__attribute__((noinline))  // so that we see it in profile.
static void
PostProcessCoverage(int target_return_value) {
  features.clear();

  if (target_return_value == -1) return;

  // Convert counters to features.
  if (state.run_time_flags.use_counter_features) {
    centipede::ForEachNonZeroByte(
        state.counter_array.data(), state.counter_array.size(),
        [](size_t idx, uint8_t value) {
          features.push_back(
              centipede::FeatureDomains::k8bitCounters.ConvertToMe(
                  centipede::Convert8bitCounterToNumber(idx, value)));
        });
  }

  // Convert data flow bit set to features.
  if (state.run_time_flags.use_dataflow_features) {
    state.data_flow_feature_set.ForEachNonZeroBit([](size_t idx) {
      features.push_back(centipede::FeatureDomains::kDataFlow.ConvertToMe(idx));
    });
  }

  // Convert cmp bit set to features.
  if (state.run_time_flags.use_cmp_features) {
    state.cmp_feature_set.ForEachNonZeroBit([](size_t idx) {
      features.push_back(centipede::FeatureDomains::kCMP.ConvertToMe(idx));
    });
  }

  // Convert path bit set to features.
  if (state.run_time_flags.path_level) {
    state.path_feature_set.ForEachNonZeroBit([](size_t idx) {
      features.push_back(
          centipede::FeatureDomains::kBoundedPath.ConvertToMe(idx));
    });
  }

  // Convert pc bit set to features, only if not use_counter_features.
  if (state.run_time_flags.use_pc_features &&
      !state.run_time_flags.use_counter_features) {
    state.pc_feature_set.ForEachNonZeroBit([](size_t idx) {
      features.push_back(centipede::FeatureDomains::k8bitCounters.ConvertToMe(
          centipede::Convert8bitCounterToNumber(idx, 1)));
    });
  }
}

static void RunOneInput(const uint8_t *data, size_t size,
                        FuzzerTestOneInputCallback test_one_input_cb) {
  state.stats = {};
  size_t last_time_usec = 0;
  auto UsecSinceLast = [&last_time_usec]() {
    uint64_t t = centipede::TimeInUsec();
    uint64_t ret_val = t - last_time_usec;
    last_time_usec = t;
    return ret_val;
  };
  UsecSinceLast();
  PrepareCoverage();
  state.stats.prep_time_usec = UsecSinceLast();
  state.ResetTimer();
  int target_return_value = test_one_input_cb(data, size);
  state.stats.exec_time_usec = UsecSinceLast();
  centipede::CheckOOM();
  PostProcessCoverage(target_return_value);
  state.stats.post_time_usec = UsecSinceLast();
  state.stats.peak_rss_mb = centipede::GetPeakRSSMb();
}

// Runs one input provided in file `input_path`.
// Produces coverage data in file `input_path`-features.
__attribute__((noinline))  // so that we see it in profile.
static void
ReadOneInputExecuteItAndDumpCoverage(
    const char *input_path, FuzzerTestOneInputCallback test_one_input_cb) {
  // Read the input.
  FILE *input_file = fopen(input_path, "r");
  PrintErrorAndExitIf(!input_file, "can't open the input file");
  auto num_bytes_read = fread(input_data, 1, kMaxDataSize, input_file);
  fclose(input_file);

  RunOneInput(input_data, num_bytes_read, test_one_input_cb);

  // Dump features to a file.
  char features_file_path[PATH_MAX];
  snprintf(features_file_path, sizeof(features_file_path), "%s-features",
           input_path);
  FILE *features_file = fopen(features_file_path, "w");
  PrintErrorAndExitIf(!features_file, "can't open coverage file");
  WriteFeaturesToFile(features_file, features.data(), features.size());
  fclose(features_file);
}

// Handles an ExecutionRequest, see RequestExecution().
// Reads inputs from `inputs_blobseq`, runs them,
// saves coverage features to `feature_blobseq`.
// Returns EXIT_SUCCESS on success and EXIT_FAILURE otherwise.
static int ExecuteInputsFromShmem(
    centipede::SharedMemoryBlobSequence &inputs_blobseq,
    centipede::SharedMemoryBlobSequence &feature_blobseq,
    FuzzerTestOneInputCallback test_one_input_cb) {
  size_t num_inputs = 0;
  if (!execution_request::IsExecutionRequest(inputs_blobseq.Read()))
    return EXIT_FAILURE;
  if (!execution_request::IsNumInputs(inputs_blobseq.Read(), num_inputs))
    return EXIT_FAILURE;
  for (size_t i = 0; i < num_inputs; i++) {
    auto blob = inputs_blobseq.Read();
    if (!execution_request::IsDataInput(blob)) return EXIT_FAILURE;
    if (!blob.IsValid()) return EXIT_FAILURE;

    // TODO(kcc): [impl] handle sizes larger than kMaxDataSize.
    size_t size = std::min(kMaxDataSize, blob.size);
    // Copy from blob to data so that to not pass the shared memory further.
    memcpy(input_data, blob.data, size);

    // Starting execution of one more input.
    if (!centipede::BatchResult::WriteInputBegin(feature_blobseq)) break;

    RunOneInput(input_data, size, test_one_input_cb);

    // Copy features to shared memory.
    if (!centipede::BatchResult::WriteOneFeatureVec(
            features.data(), features.size(), feature_blobseq)) {
      break;
    }
    // Write the stats.
    if (!centipede::BatchResult::WriteStats(state.stats, feature_blobseq))
      break;
    // We are done with this input.
    if (!centipede::BatchResult::WriteInputEnd(feature_blobseq)) break;
  }
  return EXIT_SUCCESS;
}

// See man dl_iterate_phdr.
// Sets main_object_start_address and main_object_size.
// The code assumes that the main binary is the first one to be iterated on.
static int dl_iterate_phdr_callback(struct dl_phdr_info *info, size_t size,
                                    void *data) {
  PrintErrorAndExitIf(
      state.main_object_start_address != state.kInvalidStartAddress,
      "main_object_start_address is already set");
  state.main_object_start_address = info->dlpi_addr;
  for (int j = 0; j < info->dlpi_phnum; j++) {
    uintptr_t end_offset =
        info->dlpi_phdr[j].p_vaddr + info->dlpi_phdr[j].p_memsz;
    if (state.main_object_size < end_offset)
      state.main_object_size = end_offset;
  }
  uintptr_t some_code_address =
      reinterpret_cast<uintptr_t>(&dl_iterate_phdr_callback);
  PrintErrorAndExitIf(state.main_object_start_address > some_code_address,
                      "main_object_start_address is above the code");
  PrintErrorAndExitIf(
      state.main_object_start_address + state.main_object_size <
          some_code_address,
      "main_object_start_address + main_object_size is below the code");
  return 1;  // we need only the first header, so return 1.
}

// Dumps the pc table to `output_path`.
// Assumes that main_object_start_address is already computed.
static void DumpPcTable(const char *output_path) {
  PrintErrorAndExitIf(
      state.main_object_start_address == state.kInvalidStartAddress,
      "main_object_start_address is not set");
  FILE *output_file = fopen(output_path, "w");
  PrintErrorAndExitIf(!output_file, "can't open output file");
  // Make a local copy of the pc table, and subtract the ASLR base
  // (i.e. main_object_start_address) from every PC before dumping the table.
  // Otherwise, we need to pass this ASLR offset at the symbolization time,
  // e.g. via `llvm-symbolizer --adjust-vma=<ASLR offset>`.
  // Another alternative is to build the binary w/o -fPIE or with -static.
  const uintptr_t *data = state.pcs_beg;
  const size_t data_size_in_words = state.pcs_end - state.pcs_beg;
  const size_t data_size_in_bytes = data_size_in_words * sizeof(*state.pcs_beg);
  PrintErrorAndExitIf((data_size_in_words % 2) != 0, "bad data_size_in_words");
  uintptr_t *data_copy = new uintptr_t[data_size_in_words];
  for (size_t i = 0; i < data_size_in_words; i += 2) {
    // data_copy is an array of pairs. First element is the pc, which we need to
    // modify. The second element is the pc flags, we just copy it.
    data_copy[i] = data[i] - state.main_object_start_address;
    data_copy[i + 1] = data[i + 1];
  }
  // Dump the modified table.
  auto num_bytes_written =
      fwrite(data_copy, 1, data_size_in_bytes, output_file);
  PrintErrorAndExitIf(num_bytes_written != data_size_in_bytes,
                      "wrong number of bytes written for pc table");
  fclose(output_file);
  delete[] data_copy;
}

// Returns a random seed. No need for a more sophisticated seed.
// TODO(kcc): [as-needed] optionally pass an external seed.
static unsigned GetRandomSeed() { return time(nullptr); }

// Handles a Mutation Request, see RequestMutation().
// Mutates inputs read from `inputs_blobseq`,
// writes the mutants to `outputs_blobseq`
// Returns EXIT_SUCCESS on success and EXIT_FAILURE on failure
// so that main() can return its result.
// If both `custom_mutator_cb` and `custom_crossover_cb` are nullptr,
// returns EXIT_FAILURE.
//
// TODO(kcc): [impl] make use of custom_crossover_cb, if available.
static int MutateInputsFromShmem(
    SharedMemoryBlobSequence &inputs_blobseq,
    SharedMemoryBlobSequence &outputs_blobseq,
    FuzzerCustomMutatorCallback custom_mutator_cb,
    FuzzerCustomCrossOverCallback custom_crossover_cb) {
  if (custom_mutator_cb == nullptr) return EXIT_FAILURE;
  unsigned int seed = GetRandomSeed();
  // Read max_num_mutants.
  size_t num_mutants = 0;
  size_t num_inputs = 0;
  if (!execution_request::IsMutationRequest(inputs_blobseq.Read()))
    return EXIT_FAILURE;
  if (!execution_request::IsNumMutants(inputs_blobseq.Read(), num_mutants))
    return EXIT_FAILURE;
  if (!execution_request::IsNumInputs(inputs_blobseq.Read(), num_inputs))
    return EXIT_FAILURE;

  // TODO(kcc): unclear if we can continue using std::vector (or other STL)
  // in the runner. But for now use std::vector.
  // Collect the inputs into a vector. We copy them instead of using pointers
  // into shared memory so that the user code doesn't touch the shared memory.
  std::vector<std::vector<uint8_t>> inputs;
  inputs.reserve(num_inputs);
  for (size_t i = 0; i < num_inputs; ++i) {
    auto blob = inputs_blobseq.Read();
    // If inputs_blobseq have overflown in the engine, we still want to
    // handle the first few inputs.
    if (!execution_request::IsDataInput(blob)) break;
    inputs.emplace_back(blob.data, blob.data + blob.size);
  }

  // Use `input_data` as a scratch to produce mutants.
  uint8_t *mutant = input_data;
  constexpr size_t kMaxMutantSize = kMaxDataSize;

  // Produce mutants.
  for (size_t i = 0; i < num_mutants; ++i) {
    const auto &input = inputs[rand_r(&seed) % num_inputs];

    size_t size = std::min(input.size(), kMaxMutantSize);
    memcpy(mutant, input.data(), size);
    size_t new_size = 0;
    if (rand_r(&seed) % 100 < state.run_time_flags.crossover_level) {
      // Perform crossover `crossover_level`% of the time.
      const auto &other = inputs[rand_r(&seed) % num_inputs];
      new_size = custom_crossover_cb(input.data(), input.size(), other.data(),
                                     other.size(), mutant, kMaxMutantSize,
                                     rand_r(&seed));
    } else {
      new_size = custom_mutator_cb(mutant, size, kMaxDataSize, rand_r(&seed));
    }

    if (!outputs_blobseq.Write({1 /*unused tag*/, new_size, mutant})) break;
  }
  return EXIT_SUCCESS;
}

// ASAN/TSAN/MSAN can not be used with RLIMIT_AS.
#if !defined(ADDRESS_SANITIZER) && !defined(THREAD_SANITIZER) && \
    !defined(MEMORY_SANITIZER)
constexpr bool kCanUseRlimitAs = true;
#else
constexpr bool kCanUseRlimitAs = false;
#endif

// Sets RLIMIT_CORE, RLIMIT_AS
static void SetLimits() {
  // no core files anywhere.
  struct rlimit rlimit_core = {0, 0};
  setrlimit(RLIMIT_CORE, &rlimit_core);

  // Set the address-space limit (RLIMIT_AS).
  // No-op under ASAN/TSAN/MSAN - those may still rely on rss_limit_mb.
  if constexpr (kCanUseRlimitAs) {
    size_t address_space_limit_mb =
        state.HasFlag(":address_space_limit_mb=", 0);
    if (address_space_limit_mb > 0) {
      size_t limit_in_bytes = address_space_limit_mb << 20;
      struct rlimit rlimit_as = {limit_in_bytes, limit_in_bytes};
      setrlimit(RLIMIT_AS, &rlimit_as);
    }
  }
}

// Create a fake reference to ForkServerCallMeVeryEarly() here so that the
// fork server module is not dropped during linking.
// Alternatives are
//  * Use -Wl,--whole-archive when linking with the runner archive.
//  * Use -Wl,-u,ForkServerCallMeVeryEarly when linking with the runner archive.
//    (requires ForkServerCallMeVeryEarly to be extern "C").
// These alternatives require extra flags and are thus more fragile.
// We declare ForkServerCallMeVeryEarly() here instead of doing it in some
// header file, because we want to keep the fork server header-free.
extern void ForkServerCallMeVeryEarly();
auto fake_reference_for_fork_server = &ForkServerCallMeVeryEarly;

}  // namespace centipede

// If HasFlag(:dump_pc_table:), dump the pc table to state.arg1.
//   Used to import the pc table into the caller process.
//
// If HasFlag(:shmem:), state.arg1 and state.arg2 are the names
//  of in/out shared memory locations.
//  Read inputs and write outputs via shared memory.
//
//  Default: Execute ReadOneInputExecuteItAndDumpCoverage() for all inputs.//
//
//  Note: argc/argv are used only for two things:
//    * ReadOneInputExecuteItAndDumpCoverage()
//    * LLVMFuzzerInitialize()
extern "C" int CentipedeRunnerMain(
    int argc, char **argv, FuzzerTestOneInputCallback test_one_input_cb,
    FuzzerInitializeCallback initialize_cb,
    FuzzerCustomMutatorCallback custom_mutator_cb,
    FuzzerCustomCrossOverCallback custom_crossover_cb) {
  using centipede::state;
  using centipede::tls;

  tls.OnThreadStart();
  fprintf(stderr, "Centipede fuzz target runner; argv[0]: %s flags: %s\n",
          argv[0], state.centipede_runner_flags);
  state.StartTimerThread();

  centipede::SetLimits();

  // Compute main_object_start_address, main_object_size.
  dl_iterate_phdr(centipede::dl_iterate_phdr_callback, nullptr);

  // Dump the pc table, if instructed.
  if (state.HasFlag(":dump_pc_table:")) {
    if (!state.arg1) return EXIT_FAILURE;
    centipede::DumpPcTable(state.arg1);
    return EXIT_SUCCESS;
  }

  // All further actions will execute code in the target,
  // so we need to call LLVMFuzzerInitialize.
  if (initialize_cb) {
    initialize_cb(&argc, &argv);
  }

  // Inputs / outputs from shmem.
  if (state.HasFlag(":shmem:")) {
    if (!state.arg1 || !state.arg2) return EXIT_FAILURE;
    centipede::SharedMemoryBlobSequence inputs_blobseq(state.arg1);
    centipede::SharedMemoryBlobSequence outputs_blobseq(state.arg2);
    // Read the first blob. It indicates what further actions to take.
    auto request_type_blob = inputs_blobseq.Read();
    if (centipede::execution_request::IsMutationRequest(request_type_blob)) {
      // Mutation request.
      inputs_blobseq.Reset();
      state.byte_array_mutator =
          new centipede::ByteArrayMutator(centipede::GetRandomSeed());
      return MutateInputsFromShmem(inputs_blobseq, outputs_blobseq,
                                   custom_mutator_cb, custom_crossover_cb);
    }
    if (centipede::execution_request::IsExecutionRequest(request_type_blob)) {
      // Execution request.
      inputs_blobseq.Reset();
      return ExecuteInputsFromShmem(inputs_blobseq, outputs_blobseq,
                                    test_one_input_cb);
    }
    return EXIT_FAILURE;
  }

  // By default, run every iput file one-by-one.
  for (int i = 1; i < argc; i++) {
    centipede::ReadOneInputExecuteItAndDumpCoverage(argv[i], test_one_input_cb);
  }
  return EXIT_SUCCESS;
}

extern "C" int LLVMFuzzerRunDriver(
    int *argc, char ***argv, FuzzerTestOneInputCallback test_one_input_cb) {
  return CentipedeRunnerMain(*argc, *argv, test_one_input_cb,
                             LLVMFuzzerInitialize, LLVMFuzzerCustomMutator,
                             LLVMFuzzerCustomCrossOver);
}
