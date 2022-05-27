// Copyright 2022 Google LLC.
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
#include <elf.h>
#include <limits.h>
#include <link.h>  // dl_iterate_phdr
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <cstdint>
#include <mutex>  // NOLINT

#include "./byte_array_mutator.h"
#include "./defs.h"
#include "./execution_result.h"
#include "./feature.h"
#include "./runner.h"
#include "./shared_memory_blob_sequence.h"

namespace centipede {

GlobalRunnerState state;
thread_local ThreadLocalRunnerState tls;

void ThreadLocalRunnerState::OnThreadStart() {
  std::scoped_lock<std::mutex> lock(state.tls_list_mu);
  // Add myself to state.tls_list.
  auto *old_list = state.tls_list;
  tls.next = old_list;
  state.tls_list = &tls;
  if (old_list) old_list->prev = &tls;
}

void ThreadLocalRunnerState::OnThreadStop() {
  std::scoped_lock<std::mutex> lock(state.tls_list_mu);
  // Remove myself from state.tls_list. The list never
  // becomes empty because the main thread does not call OnThreadStop().
  if (&tls == state.tls_list) {
    state.tls_list = tls.next;
    tls.prev = nullptr;
  } else {
    auto *prev = tls.prev;
    auto *next = tls.next;
    prev->next = next;
    if (next) next->prev = prev;
  }
}

static size_t GetPeakRSSMb() {
  struct rusage usage = {};
  if (getrusage(RUSAGE_SELF, &usage)) return 0;
  // On Linux, ru_maxrss is in KiB
  return usage.ru_maxrss >> 10;
}

static void CheckOOM() {
  if (state.run_time_flags.rss_limit_mb > 0) {
    size_t current_rss_mb = GetPeakRSSMb();
    if (current_rss_mb > state.run_time_flags.rss_limit_mb) {
      fprintf(stderr,
              "========= OOM, RSS limit of %zdMb exceeded (%zdMb); aborting\n",
              state.run_time_flags.rss_limit_mb, current_rss_mb);
      abort();
    }
  }
}

static void CheckTimeout() {
  time_t start_time = state.timer;
  time_t curr_time = time(nullptr);
  if (state.run_time_flags.timeout_in_seconds != 0) {
    if (curr_time - start_time > state.run_time_flags.timeout_in_seconds) {
      fprintf(stderr, "========= timeout of %zd seconds exceeded; aborting\n",
              state.run_time_flags.timeout_in_seconds);
      abort();
    }
  }
}

// Timer thread. Periodically checks if it's time to abort due to a timeout/OOM.
static void *TimerThread(void *unused) {
  while (true) {
    sleep(1);
    if (state.timer == 0) continue;  // No calls to ResetTimer() yet.

    CheckTimeout();
    CheckOOM();
  }
  return nullptr;
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

}  // namespace centipede

using centipede::state;
using centipede::tls;

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

// This is the header-less interface of libFuzzer, see
// https://llvm.org/docs/LibFuzzer.html.
extern "C" {
int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size);
__attribute__((weak)) int LLVMFuzzerInitialize(int *argc, char ***argv);
__attribute__((weak)) size_t LLVMFuzzerCustomMutator(uint8_t *data, size_t size,
                                                     size_t max_size,
                                                     unsigned int seed);
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
  auto cov_size = state.cov_8bit_end - state.cov_8bit_beg;
  memset(state.cov_8bit_beg, 0, cov_size);
  state.data_flow_feature_set.clear();
  state.cmp_feature_set.clear();
  state.path_feature_set.clear();
  state.pc_feature_set.clear();
  state.ForEachTls([](centipede::ThreadLocalRunnerState &tls) {
    tls.path_ring_buffer.clear();
  });
}

// Post-processes all coverage data, puts it all into `features`.
// Returns the number of features in `features`.
__attribute__((noinline))  // so that we see it in profile.
static void
PostProcessCoverage() {
  PrintErrorAndExitIf(state.cov_8bit_end < state.cov_8bit_beg, "broken state");
  auto cov_size = state.cov_8bit_end - state.cov_8bit_beg;
  features.clear();
  // Convert counters to features.
  centipede::ForEachNonZeroByte(
      state.cov_8bit_beg, cov_size, [](size_t idx, uint8_t value) {
        features.push_back(centipede::FeatureDomains::k8bitCounters.ConvertToMe(
            centipede::Convert8bitCounterToNumber(idx, value)));
      });

  // Convert data flow bit set to features.
  state.data_flow_feature_set.ForEachNonZeroBit([](size_t idx) {
    features.push_back(centipede::FeatureDomains::kDataFlow.ConvertToMe(idx));
  });
  // Convert cmp bit set to features.
  state.cmp_feature_set.ForEachNonZeroBit([](size_t idx) {
    features.push_back(centipede::FeatureDomains::kCMP.ConvertToMe(idx));
  });
  // Convert path bit set to features.
  state.path_feature_set.ForEachNonZeroBit([](size_t idx) {
    features.push_back(
        centipede::FeatureDomains::kBoundedPath.ConvertToMe(idx));
  });
  // Convert pc bit set to features.
  // TODO(kcc): [impl] this and the above Convert8bitCounterToNumber both
  // add the same features to k8BitCounters.
  // Right now, if both instrumentation variants are enabed, we will add
  // every such feature twice. This needs to be resolved eventually.
  state.pc_feature_set.ForEachNonZeroBit([](size_t idx) {
    features.push_back(centipede::FeatureDomains::k8bitCounters.ConvertToMe(
        centipede::Convert8bitCounterToNumber(idx, 1)));
  });

  // If there are no features to report, we insert one fake feature.
  // We do it for two reasons:
  // * So that Centipede doesn't need to specially handle zero features case.
  // * If this process crashes, Centipede knows which was the last good input.
  if (features.size() == 0) features.push_back(0);
}

// Runs one input provided in file `input_path`.
// Produces coverage data in file `input_path`-features.
__attribute__((noinline))  // so that we see it in profile.
static void
ReadOneInputExecuteItAndDumpCoverage(const char *input_path) {
  // Read the input.
  FILE *input_file = fopen(input_path, "r");
  PrintErrorAndExitIf(!input_file, "can't open the input file");
  auto num_bytes_read = fread(input_data, 1, kMaxDataSize, input_file);
  fclose(input_file);

  // Run and collect coverage.
  PrepareCoverage();
  state.ResetTimer();
  LLVMFuzzerTestOneInput(input_data, num_bytes_read);
  centipede::CheckOOM();
  PostProcessCoverage();

  // Dump features to a file.
  char features_file_path[PATH_MAX];
  snprintf(features_file_path, sizeof(features_file_path), "%s-features",
           input_path);
  FILE *features_file = fopen(features_file_path, "w");
  PrintErrorAndExitIf(!features_file, "can't open coverage file");
  WriteFeaturesToFile(features_file, features.data(), features.size());
  fclose(features_file);
}

// Reads inputs from one shared memory location (`shmem_name_in`),
// runs them, stores coverage features to another shared memory location
// (`shmem_name_out`);
void ReadInputsFromShmemAndRun(const char *shmem_name_in,
                               const char *shmem_name_out) {
  fprintf(stderr, "ReadInputsFromShmemAndRun %s %s\n", shmem_name_in,
          shmem_name_out);
  centipede::SharedMemoryBlobSequence inputs_blobseq(shmem_name_in);
  centipede::SharedMemoryBlobSequence feature_blobseq(shmem_name_out);
  while (true) {
    auto blob = inputs_blobseq.Read();
    if (blob.size == 0) break;
    // Copy from blob to data so that to not pass the shared memory further.
    memcpy(input_data, blob.data, blob.size);

    // Run and collect coverage.
    // TODO(kcc): [impl] move this into a separate function.
    PrepareCoverage();
    state.ResetTimer();
    LLVMFuzzerTestOneInput(input_data, blob.size);
    centipede::CheckOOM();
    PostProcessCoverage();

    // Copy features to shared memory.
    if (!centipede::BatchResult::WriteOneFeatureVec(
            features.data(), features.size(), feature_blobseq)) {
      break;
    }
  }
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

// Mutates inputs read from `shmem_name_in`,
// writes the mutants to `shmem_name_out`
// Returns EXIT_SUCCESS on success and EXIT_FAILURE on failure
// so that main() can return its result.
//
// The first input blob is the number of requested mutants.
// All other input blobs are the inputs to mutate.
// See also: MutateViaExternalBinary.
// TODO(kcc): [impl] make use of LLVMFuzzerCustomCrossOver, if available.
static int MutateInputsFromShmem(const char *shmem_name_in,
                                 const char *shmem_name_out) {
  // The binary must implement LLVMFuzzerCustomMutator.
  if (LLVMFuzzerCustomMutator == nullptr) return EXIT_FAILURE;
  // Set up shmem blob sequences.
  centipede::SharedMemoryBlobSequence inputs_blobseq(shmem_name_in);
  centipede::SharedMemoryBlobSequence feature_blobseq(shmem_name_out);
  unsigned int seed = GetRandomSeed();
  // Read max_num_mutants.
  size_t max_num_mutants = 0;
  auto first_blob = inputs_blobseq.Read();
  if (first_blob.size != sizeof(max_num_mutants)) return EXIT_FAILURE;
  memcpy(&max_num_mutants, first_blob.data, sizeof(max_num_mutants));
  // Produce mutants.
  for (size_t num_mutants = 0; num_mutants < max_num_mutants;) {
    auto blob = inputs_blobseq.Read();
    if (blob.size == 0) {      // No more inputs.
      inputs_blobseq.Reset();  // Start reading from the beginning again.
      inputs_blobseq.Read();   // Skip the first blob.
      continue;
    }

    if (blob.size > kMaxDataSize) continue;  // Ignore large inputs.

    // Copy blob to input_data so that to not pass the shared memory further.
    memcpy(input_data, blob.data, blob.size);
    size_t new_size =
        LLVMFuzzerCustomMutator(input_data, blob.size, kMaxDataSize, seed);

    if (!feature_blobseq.Write({1 /*unused tag*/, new_size, input_data})) break;

    ++num_mutants;  // increment here, because we count only successful mutants.
    ++seed;         // Use different seed for every mutation.
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

// If HasFlag(:dump_pc_table:), dump the pc table to argv[1].
//   Used to import the pc table into the caller process.
//
// If HasFlag(:mutate:), mutate inputs from shmem location argv[1],
//   and write mutants to shmem location argv[2].
//
// If HasFlag(:shmem:), argv[1] and argv[2] are the names
//  of in/out shared memory locations.
//  Read inputs and write features to shared memory.
//
// Default: Execute ReadOneInputExecuteItAndDumpCoverage() for all inputs.
int main(int argc, char **argv) {
  tls.OnThreadStart();
  fprintf(stderr, "Centipede fuzz target runner; argv[0]: %s flags: %s\n",
          argv[0], state.centipede_runner_flags);
  state.StartTimerThread();

  SetLimits();

  // Compute main_object_start_address, main_object_size.
  dl_iterate_phdr(dl_iterate_phdr_callback, nullptr);

  // Dump the pc table, if instructed.
  if (state.HasFlag(":dump_pc_table:")) {
    if (argc != 2) return EXIT_FAILURE;
    DumpPcTable(argv[1]);
    return EXIT_SUCCESS;
  }

  // Mutate inputs.
  if (state.HasFlag(":mutate:")) {
    if (argc != 3) return EXIT_FAILURE;
    state.byte_array_mutator = new centipede::ByteArrayMutator(GetRandomSeed());
    return MutateInputsFromShmem(argv[1], argv[2]);
  }

  // All further actions will execute LLVMFuzzerTestOneInput,
  // so we need to call LLVMFuzzerInitialize.
  if (LLVMFuzzerInitialize) {
    LLVMFuzzerInitialize(&argc, &argv);
  }

  // Inputs / outputs from shmem.
  if (state.HasFlag(":shmem:")) {
    if (argc != 3) return 1;
    auto shmem_name_in = argv[1];
    auto shmem_name_out = argv[2];
    ReadInputsFromShmemAndRun(shmem_name_in, shmem_name_out);
    return EXIT_SUCCESS;
  }

  // By default, run every iput file one-by-one.
  for (int i = 1; i < argc; i++) {
    ReadOneInputExecuteItAndDumpCoverage(argv[i]);
  }
  return EXIT_SUCCESS;
}
