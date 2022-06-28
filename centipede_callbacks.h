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

#ifndef THIRD_PARTY_CENTIPEDE_CENTIPEDE_CALLBACKS_H_
#define THIRD_PARTY_CENTIPEDE_CENTIPEDE_CALLBACKS_H_

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "./byte_array_mutator.h"
#include "./command.h"
#include "./defs.h"
#include "./environment.h"
#include "./execution_result.h"
#include "./shared_memory_blob_sequence.h"
#include "./util.h"

namespace centipede {

// User must inherit from this class and override at least the
// pure virtual functions.
//
// The classes inherited from this one must be thread-compatible.
class CentipedeCallbacks {
 public:
  // `env` is used to pass flags to `this`, it must outlive `this`.
  CentipedeCallbacks(const Environment &env)
      : env_(env), byte_array_mutator_(GetRandomSeed(env.seed)) {}
  virtual ~CentipedeCallbacks() {}
  // Feeds `inputs` into the `binary`, for every input populates `batch_result`.
  // Old contents of `batch_result` are cleared.
  // Returns true on success, false on failure.
  // Post-condition:
  // `batch_result` has results for every `input`, even on failure.
  virtual bool Execute(std::string_view binary,
                       const std::vector<ByteArray> &inputs,
                       BatchResult &batch_result) = 0;
  // Mutates every input in `inputs`.
  virtual void Mutate(std::vector<ByteArray> &inputs) = 0;
  // Returns some simple non-empty valid input.
  virtual ByteArray DummyValidInput() { return {0}; }

 protected:
  // Helpers that the user-defined class may use if needed.

  // Same as ExecuteCentipedeSancovBinary, but uses shared memory.
  // Much faster for fast targets since it uses fewer system calls.
  int ExecuteCentipedeSancovBinaryWithShmem(
      std::string_view binary, const std::vector<ByteArray> &inputs,
      BatchResult &batch_result);

  // Constructs a string CENTIPEDE_RUNNER_FLAGS=":flag1:flag2:...",
  // where the flags are determined by `env` and also include `extra_flags`.
  // If `disable_coverage`, coverage options are not added.
  std::string ConstructRunnerFlags(std::string_view extra_flags = "",
                                   bool disable_coverage = false);

  // Uses an external binary `binary` to mutate `inputs`.
  // The binary should be linked against :fuzz_target_runner and
  // implement the Structure-Aware Fuzzing interface, as described here:
  // github.com/google/fuzzing/blob/master/docs/structure-aware-fuzzing.md
  //
  // Produces `mutants.size()` mutants,
  // replacing the existing elements of `mutants`.
  //
  // Returns true on success.
  bool MutateViaExternalBinary(std::string_view binary,
                               const std::vector<ByteArray> &inputs,
                               std::vector<ByteArray> &mutants);

  // Loads the dictionary from `dictionary_path`,
  // returns the number of dictionary entries loaded.
  size_t LoadDictionary(std::string_view dictionary_path);

 protected:
  const Environment &env_;
  ByteArrayMutator byte_array_mutator_;

 private:
  // Returns a Command object with matching `binary` from commands_,
  // creates one if needed.
  Command &GetOrCreateCommandForBinary(std::string_view binary);

  // Variables required for ExecuteCentipedeSancovBinaryWithShmem.
  // They are computed in CTOR, to avoid extra computation in the hot loop.
  std::string temp_dir_ = TemporaryLocalDirPath();
  const std::string execute_log_path_ =
      std::filesystem::path(temp_dir_).append("log");
  std::string failure_description_path_ =
      std::filesystem::path(temp_dir_).append("failure_description");
  const std::string shmem_name1_ = ProcessAndThreadUniqueID("/centipede-shm1-");
  const std::string shmem_name2_ = ProcessAndThreadUniqueID("/centipede-shm2-");

  // Set the shared memory sizes to 1Gb. There seem to be no penalty
  // for creating large shared memory regions if they are not actually
  // fully utilized.
  const size_t kBlobSeqSize = 1 << 30;  // 1Gb.
  SharedMemoryBlobSequence inputs_blobseq_ = {shmem_name1_.c_str(),
                                              kBlobSeqSize};
  SharedMemoryBlobSequence outputs_blobseq_ = {shmem_name2_.c_str(),
                                               kBlobSeqSize};

  std::vector<Command> commands_;
};

// Abstract class for creating/destroying CentipedeCallbacks objects.
// A typical implementation would simply new/delete objects of appropriate type,
// see DefaultCallbacksFactory below.
// Other implementations (e.g. for tests) may take the object from elsewhere
// and not actually delete it.
class CentipedeCallbacksFactory {
 public:
  virtual CentipedeCallbacks *create(const Environment &env) = 0;
  virtual void destroy(CentipedeCallbacks *callbacks) = 0;
  virtual ~CentipedeCallbacksFactory() {}
};

// This is the typical way to implement a CentipedeCallbacksFactory for a Type.
template <typename Type>
class DefaultCallbacksFactory : public CentipedeCallbacksFactory {
 public:
  CentipedeCallbacks *create(const Environment &env) override {
    return new Type(env);
  }
  void destroy(CentipedeCallbacks *callbacks) override { delete callbacks; }
};

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_CENTIPEDE_CALLBACKS_H_
