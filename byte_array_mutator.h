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

#ifndef THIRD_PARTY_CENTIPEDE_BYTE_ARRAY_MUTATOR_H_
#define THIRD_PARTY_CENTIPEDE_BYTE_ARRAY_MUTATOR_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "./defs.h"

namespace centipede {

// This class allows to mutate a ByteArray in different ways.
// All mutations expect and guarantee that `data` remains non-empty
// since there is only one possible empty input and it's uninteresting.
//
// This class is thread-compatible.
// Typical usage is to have one such object per thread.
class ByteArrayMutator {
 public:
  // CTOR. Initializes the internal RNG with `seed`.
  ByteArrayMutator(uintptr_t seed) : rng_(seed) {}

  // Adds `dict_entries` to an internal dictionary.
  void AddToDictionary(const std::vector<ByteArray> &dict_entries);

  // Opens the Centipede corpus file from `path` and adds its entries
  // to the dictionary.
  // Returns the number of entries added.
  // Does nothing and returns 0 if `path` is empty.
  size_t LoadDictionaryFromCorpusFile(std::string_view path);

  // Mutates all elements in `data_vec`.
  // If `allow_crossover`, may randomly apply CrossOver across the elements.
  void MutateMany(std::vector<ByteArray> &data_vec, bool allow_crossover);

  // Mutates `data` by inserting a random part from `other`.
  void CrossOverInsert(ByteArray &data, const ByteArray &other);

  // Mutates `data` by overwriting some of it with a random part of `other`.
  void CrossOverOverwrite(ByteArray &data, const ByteArray &other);

  // Applies one of {CrossOverOverwrite, CrossOverInsert}.
  void CrossOver(ByteArray &data, const ByteArray &other);

  // Type for a Mutator member-function.
  // Every mutator function takes a ByteArray& as an input, mutates it in place
  // and returns true if mutation took place. In some cases mutation may fail
  // to happen, e.g. if EraseBytes() is called on a 1-byte input.
  // Fn is test-only public.
  using Fn = bool (ByteArrayMutator::*)(ByteArray &);

  // All public functions below are mutators.
  // They return true iff a mutation took place.

  // Applies some random mutation to data.
  bool Mutate(ByteArray &data);

  // Applies some random mutation that doesn't change size.
  bool MutateSameSize(ByteArray &data);

  // Applies some random mutation that decreases size.
  bool MutateDecreaseSize(ByteArray &data);

  // Applies some random mutation that increases size.
  bool MutateIncreaseSize(ByteArray &data);

  // Flips a random bit.
  bool FlipBit(ByteArray &data);

  // Swaps two bytes.
  bool SwapBytes(ByteArray &data);

  // Changes a random byte to a random value.
  bool ChangeByte(ByteArray &data);

  // Overwrites a random part of `data` with a random dictionary entry.
  bool OverwriteFromDictionary(ByteArray &data);

  // Inserts random bytes.
  bool InsertBytes(ByteArray &data);

  // Inserts a random dictionary entry at random position.
  bool InsertFromDictionary(ByteArray &data);

  // Erases random bytes.
  bool EraseBytes(ByteArray &data);

 private:
  // Applies fn[random_index] to data, returns true if a mutation happened.
  template <size_t kArraySize>
  bool ApplyOneOf(const std::array<Fn, kArraySize> &fn, ByteArray &data) {
    // Individual mutator may fail to mutate and return false.
    // So we iterate a few times and expect one of the mutations will succeed.
    for (int iter = 0; iter < 10; iter++) {
      size_t mut_idx = rng_() % kArraySize;
      if ((this->*fn[mut_idx])(data)) return true;
    }
    return false;  // May still happen periodically.
  }

  Rng rng_;
  std::vector<ByteArray> dictionary_;
};

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_BYTE_ARRAY_MUTATOR_H_
