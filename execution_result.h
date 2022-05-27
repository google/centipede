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

#ifndef THIRD_PARTY_CENTIPEDE_EXECUTION_RESULT_H_
#define THIRD_PARTY_CENTIPEDE_EXECUTION_RESULT_H_

#include <cstdlib>
#include <string>
#include <vector>

#include "./feature.h"
#include "./shared_memory_blob_sequence.h"

namespace centipede {

// It represents the results of the execution of one input by the runner.
// TODO(kcc): [impl] move the serialization code from callbacks and runner here.
class ExecutionResult {
 public:
  // Movable, not Copyable.
  ExecutionResult(ExecutionResult&& other) = default;
  ExecutionResult& operator=(ExecutionResult&& other) = default;

  ExecutionResult() {}
  explicit ExecutionResult(const FeatureVec& features) : features_(features) {}
  const FeatureVec& features() const { return features_; }
  FeatureVec& mutable_features() { return features_; }

  // Clears the data, but doesn't deallocate the heap storage.
  void clear() { features_.clear(); }

 private:
  FeatureVec features_;  // Features produced by the target on one input.
};

// BatchResult is the communication API between Centipede and its runner.
// In consists of a vector of ExecutionResult objects, one per executed input,
// and optionally some other details about the execution of the input batch.
//
// The runner uses static methods Write*() to write to a blobseq.
// Centipede uses Read() to get all the data from blobseq.
class BatchResult {
 public:
  // If BatchResult is used in a hot loop, define it outside the loop and
  // use ClearAndResize() on every iteration.
  // This will reduce the number of mallocs.
  BatchResult() {}

  // Not movable.
  BatchResult(BatchResult&& other) = delete;
  BatchResult& operator=(BatchResult&& other) = delete;

  // Clears all data, but usually does not deallocate heap storage.
  void ClearAndResize(size_t new_size) {
    for (auto& result : results_) result.clear();
    results_.resize(new_size);
    log_.clear();
    exit_code_ = EXIT_SUCCESS;
    num_outputs_read_ = 0;
  }

  // Writes one FeaturVec (from `vec` and `size`) to `blobseq`.
  // Returns true iff successful.
  // Called by the runner.
  // When executing N inputs, the runner will call this at most N times.
  static bool WriteOneFeatureVec(const feature_t* vec, size_t size,
                                 SharedMemoryBlobSequence& blobseq);

  // Reads everything written by the runner to `blobseq` into `this`.
  // Returns true iff successful.
  // When running N inputs, ClearAndResize(N) must be called before Read().
  bool Read(SharedMemoryBlobSequence& blobseq);

  // Accessors.
  std::vector<ExecutionResult>& results() { return results_; }
  const std::vector<ExecutionResult>& results() const { return results_; }
  std::string &log() { return log_; }
  const std::string &log() const { return log_; }
  int& exit_code() { return exit_code_; }
  int exit_code() const { return exit_code_; }
  int num_outputs_read() const { return num_outputs_read_; }

 private:
  std::vector<ExecutionResult> results_;
  std::string log_;  // log_ is populated optionally, e.g. if there was a crash.
  int exit_code_ = EXIT_SUCCESS;  // Process exit code.
  size_t num_outputs_read_ = 0;
};

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_EXECUTION_RESULT_H_
