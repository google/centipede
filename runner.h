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

#ifndef THIRD_PARTY_CENTIPEDE_RUNNER_H_
#define THIRD_PARTY_CENTIPEDE_RUNNER_H_

#include <pthread.h>  // NOLINT: use pthread to avoid extra dependencies.
#include <string.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "./byte_array_mutator.h"
#include "./execution_result.h"
#include "./feature.h"

namespace centipede {

// Like std::lock_guard, but for pthread_mutex_t.
class LockGuard {
 public:
  explicit LockGuard(pthread_mutex_t &mu) : mu_(mu) { pthread_mutex_lock(&mu); }
  ~LockGuard() { pthread_mutex_unlock(&mu_); }

 private:
  pthread_mutex_t &mu_;
};

// Flags derived from CENTIPEDE_RUNNER_FLAGS.
// Flags used in instrumentation callbacks are bit-packed for efficiency.
struct RunTimeFlags {
  uint64_t use_pc_features : 1;
  uint64_t use_path_features : 1;
  uint64_t use_dataflow_features : 1;
  uint64_t use_cmp_features : 1;
  uint64_t use_counter_features : 1;
  uint64_t timeout_in_seconds;
  uint64_t rss_limit_mb;
};

// One such object is created in runner's TLS.
// There is no CTOR, since we don't want to use the brittle and lazy TLS CTORs.
// All data members are zero-initialized during thread creation.
struct ThreadLocalRunnerState {
  // Intrusive doubly-linked list of TLS objects.
  // Guarded by state.tls_list_mu.
  ThreadLocalRunnerState *next, *prev;
  // The pthread_create() interceptor calls OnThreadStart()/OnThreadStop()
  // before/after the thread callback.
  // The main thread calls OnThreadStart().
  void OnThreadStart();
  void OnThreadStop();

  // Paths are thread-local, so we maintain the current bounded path here.
  // TODO(kcc): [impl] this may still cause unbounded path explosion.
  static constexpr size_t kBoundedPathLength = 16;
  HashedRingBuffer<kBoundedPathLength> path_ring_buffer;
};

// One global object of this type is created by the runner at start up.
// All data members will be initialized to zero, unless they have initializers.
// Accesses to the subobjects should be fast, so we are trying to avoid
// extra memory references where possible.
struct GlobalRunnerState {
  // Used by LLVMFuzzerMutate and initialized in main().
  ByteArrayMutator *byte_array_mutator = nullptr;

  // Runner reads flags from a dedicated env var, CENTIPEDE_RUNNER_FLAGS.
  // We don't use flags passed via argv so that argv flags can be passed
  // directly to LLVMFuzzerInitialize, w/o filtering. The flags passed in
  // CENTIPEDE_RUNNER_FLAGS are separated with ':' on both sides, i.e. like
  // this: CENTIPEDE_RUNNER_FLAGS=":flag1:flag2:". We do it this way to make the
  // flag parsing code extremely simple. The interface is private between
  // Centipede and the runner and may change.
  const char *centipede_runner_flags = getenv("CENTIPEDE_RUNNER_FLAGS");
  const char *arg1 = GetStringFlag(":arg1=");
  const char *arg2 = GetStringFlag(":arg2=");
  // The path to a file where the runner may write the description of failure.
  const char *failure_description_path =
      GetStringFlag(":failure_description_path=");

  // Flags.
  RunTimeFlags run_time_flags = {
      .use_pc_features = HasFlag(":use_pc_features:"),
      .use_path_features = HasFlag(":use_path_features:"),
      .use_dataflow_features = HasFlag(":use_dataflow_features:"),
      .use_cmp_features = HasFlag(":use_cmp_features:"),
      .use_counter_features = HasFlag(":use_counter_features:"),
      .timeout_in_seconds = HasFlag(":timeout_in_seconds=", 0),
      .rss_limit_mb = HasFlag(":rss_limit_mb=", 0)};

  // Returns true iff `flag` is present.
  // Typical usage: pass ":some_flag:", i.e. the flag name surrounded with ':'.
  bool HasFlag(const char *flag) {
    if (!centipede_runner_flags) return false;
    return strstr(centipede_runner_flags, flag) != 0;
  }

  // If a flag=value pair is present, returns value,
  // otherwise returns `default_value`.
  // Typical usage: pass ":some_flag=".
  uint64_t HasFlag(const char *flag, uint64_t default_value) {
    if (!centipede_runner_flags) return default_value;
    const char *beg = strstr(centipede_runner_flags, flag);
    if (!beg) return default_value;
    return atoll(beg + strlen(flag));  // NOLINT: can't use strto64, etc.
  }

  // If a :flag=value: pair is present returns value, otherwise returns nullptr.
  // The result is obtained by calling strndup, so make sure to save
  // it in `this` to avoid a leak.
  // Typical usage: pass ":some_flag=".
  const char *GetStringFlag(const char *flag) {
    if (!centipede_runner_flags) return nullptr;
    // Exctract "value" from ":flag=value:" inside centipede_runner_flags.
    const char *beg = strstr(centipede_runner_flags, flag);
    if (!beg) return nullptr;
    const char *value_beg = beg + strlen(flag);
    const char *end = strstr(value_beg, ":");
    if (!end) return nullptr;
    return strndup(value_beg, end - value_beg);
  }

  // Doubly linked list of TLSs of all live threads.
  ThreadLocalRunnerState *tls_list;
  pthread_mutex_t tls_list_mu;  // Guards tls_list.
  // Iterates all TLS objects under tls_list_mu.
  // Calls `callback()` on every TLS.
  template <typename Callback>
  void ForEachTls(Callback callback) {
    LockGuard lock(tls_list_mu);
    for (auto *it = tls_list; it; it = it->next) callback(*it);
  }

  // The variables below are computed by dl_iterate_phdr_callback.
  // Main object is the executable binary containing main()
  // and most of the executable code (we assume that the target is
  // built in mostly-static mode, i.e. -dynamic_mode=off).
  static constexpr uintptr_t kInvalidStartAddress = -1;
  uintptr_t main_object_start_address = kInvalidStartAddress;
  uintptr_t main_object_size;

  // State for SanitizerCoverage.
  // See https://clang.llvm.org/docs/SanitizerCoverage.html.
  const uintptr_t *pcs_beg, *pcs_end;
  static const size_t kBitSetSize = 1 << 18;  // Arbitrary large size.
  ConcurrentBitSet<kBitSetSize> data_flow_feature_set;

  // Tracing CMP instructions.
  // https://clang.llvm.org/docs/SanitizerCoverage.html#tracing-data-flow
  ConcurrentBitSet<kBitSetSize> cmp_feature_set;

  // trace-pc-guard callbacks (edge instrumentation).
  // https://clang.llvm.org/docs/SanitizerCoverage.html#tracing-pcs-with-guards
  //
  // We use edge instrumentation w/ callbacks to implement bounded-path
  // coverage.
  // * The current PC is converted to an offset (a PC index).
  // * The offset is pushed to a HashedRingBuffer, producing a hash.
  // * The resulting hash represents N most recent PCs, we use it as a feature.
  //
  // WARNING: this is highly experimental.
  // This is far from perfect and may be not sensitive enough in some cases
  // and create exponential number of features in other cases.
  // Some areas to experiment with:
  // * Handle only function-entry PCs, i.e. use call paths, not branch paths.
  // * Play with the length of the path (kBoundedPathLength)
  // * Use call stacks instead of paths (via unwinding or other
  // instrumentation).

  uint32_t *pc_guard_start;
  uint32_t *pc_guard_stop;

  // Observed paths.
  ConcurrentBitSet<kBitSetSize> path_feature_set;
  // Observed individual PCs.
  ConcurrentBitSet<kBitSetSize> pc_feature_set;

  // Control flow edge counters.
  inline static const size_t kCounterArraySize = 1 << 15;  // Some large size.
  CounterArray<kCounterArraySize> counter_array;

  // Execution stats for the currently executed input.
  ExecutionResult::Stats stats;

  // Timeout-related machinery.

  // If the timeout_in_seconds flag is passed, initializes the timer thread.
  void StartTimerThread();
  // Resets the timer. Call this before executing every input.
  void ResetTimer();
  // Initially, zero. ResetTimer() sets it to the current time.
  std::atomic<time_t> timer;
};

extern GlobalRunnerState state;
extern thread_local ThreadLocalRunnerState tls;

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_RUNNER_H_
