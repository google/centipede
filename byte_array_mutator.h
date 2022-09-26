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
#include <cstring>
#include <string_view>
#include <vector>

#include "./defs.h"

namespace centipede {

// A simple class representing an array of up to kMaxEntrySize bytes.
struct DictEntry {
  static constexpr uint8_t kMaxEntrySize = 15;

 public:
  DictEntry(const uint8_t *bytes, uint8_t size)
      : bytes_{},  // initialize bytes_ to all zeros
        size_(size) {
    if (size_ > kMaxEntrySize) __builtin_trap();
    memcpy(bytes_, bytes, size);
  }
  const uint8_t *begin() const { return bytes_; }
  const uint8_t *end() const { return bytes_ + size_; }
  size_t size() const { return size_; }
  bool operator<(const DictEntry &other) const {
    return memcmp(this, &other, sizeof(*this)) < 0;
  }

 private:
  // bytes_ must go first so that operator < is lexicographic.
  uint8_t bytes_[kMaxEntrySize];
  uint8_t size_;  // between 1 and kMaxEntrySize.
};

// This class allows to mutate a ByteArray in different ways.
// All mutations expect and guarantee that `data` remains non-empty
// since there is only one possible empty input and it's uninteresting.
//
// This class is thread-compatible.
// Typical usage is to have one such object per thread.
class ByteArrayMutator {
 public:
  // CTOR. Initializes the internal RNG with `seed` (`seed` != 0).
  ByteArrayMutator(uintptr_t seed) : rng_(seed) {
    if (seed == 0) __builtin_trap();  // We don't include logging.h here.
  }

  // Adds `dict_entries` to an internal dictionary.
  void AddToDictionary(const std::vector<ByteArray> &dict_entries);

  // Takes non-empty `inputs`, produces `num_mutants` mutations in `mutants`.
  // Old contents of `mutants` are discarded.
  // `crossover_level` should be in [0,100].
  // 0 means no crossover. Larger values mean more aggressive crossover.
  void MutateMany(const std::vector<ByteArray> &inputs, size_t num_mutants,
                  int crossover_level, std::vector<ByteArray> &mutants);

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

  // Set size alignment for mutants with modified sizes. Some mutators do not
  // change input size, but mutators that insert or erase bytes will produce
  // mutants with aligned sizes (if possible).
  void set_size_alignment(size_t size_alignment) {
    size_alignment_ = size_alignment;
  }

 private:
  FRIEND_TEST(ByteArrayMutator, RoundUpToAddCorrectly);
  FRIEND_TEST(ByteArrayMutator, RoundDownToRemoveCorrectly);

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

  // Given a current size and a number of bytes to add, returns the number of
  // bytes that should be added for the resulting size to be properly aligned.
  //
  // If the original to_add would result in an unaligned input size, we round up
  // to the next larger aligned size.
  size_t RoundUpToAdd(size_t curr_size, size_t to_add);

  // Given a current size and a number of bytes to remove, returns the number of
  // bytes that should be removed for the resulting size to be property aligned.
  //
  // If the original to_remove would result in an unaligned input size, we
  // round down to the next smaller aligned size.
  //
  // However, we never return a number of bytes to remove that would result in a
  // 0 size. In this case, the resulting size will be the smaller of
  // curr_size and size_alignment_.
  size_t RoundDownToRemove(size_t curr_size, size_t to_remove);

  // Size alignment in bytes to generate mutants.
  //
  // For example, if size_alignment_ is 1, generated mutants can have any
  // number of bytes. If size_alignment_ is 4, generated mutants will have sizes
  // that are 4-byte aligned.
  size_t size_alignment_ = 1;

  Rng rng_;
  std::vector<DictEntry> dictionary_;
};

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_BYTE_ARRAY_MUTATOR_H_
