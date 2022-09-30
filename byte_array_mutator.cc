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

#include "./byte_array_mutator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "./defs.h"

namespace centipede {

//============= CmpDictionary ===============
bool CmpDictionary::SetFromCmpData(ByteSpan cmp_data) {
  dictionary_.clear();
  for (size_t i = 0; i < cmp_data.size();) {
    auto size = cmp_data[i];
    if (size > DictEntry::kMaxEntrySize) return false;
    if (i + 2 * size > cmp_data.size()) return false;
    ByteSpan a(cmp_data.begin() + i + 1, size);
    ByteSpan b(cmp_data.begin() + i + size + 1, size);
    // TODO(kcc): disregard boring CMP pairs, such as e.g. `1 CMP 0`.
    dictionary_.emplace_back(a, b);
    dictionary_.emplace_back(b, a);
    i += 1 + 2 * size;
  }
  std::sort(dictionary_.begin(), dictionary_.end());
  return true;
}

void CmpDictionary::SuggestReplacement(
    ByteSpan bytes, std::vector<ByteSpan> &suggestions) const {
  if (!suggestions.capacity()) return;
  suggestions.clear();
  if (bytes.size() < DictEntry::kMinEntrySize) return;
  // Use binary search to find the first entry that starts with the
  // same kMinEntrySize bytes as `bytes`.
  // This is not supper efficient.
  // We need to see the real usage before optimizing.
  // TODO(kcc): investigate using absl/container/btree_map.h instead.
  DictEntry prefix({bytes.begin(), DictEntry::kMinEntrySize});
  auto iter = std::lower_bound(
      dictionary_.begin(), dictionary_.end(), Pair{prefix, prefix},
      [](const Pair &a, const Pair &b) { return a.first < b.first; });
  // Iterate from the first entry that has the same first bytes as `bytes`
  // to the last such entry.
  for (; iter != dictionary_.end(); ++iter) {
    const auto &a = iter->first;
    const auto &b = iter->second;
    if (bytes.size() < a.size()) break;
    if (suggestions.size() == suggestions.capacity()) break;
    if (!std::equal(bytes.begin(), bytes.begin() + DictEntry::kMinEntrySize,
                    a.begin())) {
      break;
    }
    if (std::equal(a.begin(), a.end(), bytes.begin()))
      suggestions.emplace_back(b.begin(), b.size());
  }
}

//============= ByteArrayMutator ===============
size_t ByteArrayMutator::RoundUpToAdd(size_t curr_size, size_t to_add) {
  const size_t remainder = (curr_size + to_add) % size_alignment_;
  return (remainder == 0) ? to_add : (to_add + size_alignment_ - remainder);
}

size_t ByteArrayMutator::RoundDownToRemove(size_t curr_size, size_t to_remove) {
  if (curr_size <= size_alignment_) return 0;
  if (to_remove >= curr_size) return curr_size - size_alignment_;

  size_t result_size = curr_size - to_remove;
  result_size -= (result_size % size_alignment_);
  to_remove = curr_size - result_size;
  return (result_size == 0) ? to_remove - size_alignment_ : to_remove;
}

bool ByteArrayMutator::Mutate(ByteArray &data) {
  return ApplyOneOf<3>(
      {&ByteArrayMutator::MutateSameSize, &ByteArrayMutator::MutateIncreaseSize,
       &ByteArrayMutator::MutateDecreaseSize},
      data);
}

bool ByteArrayMutator::MutateSameSize(ByteArray &data) {
  return ApplyOneOf<4>(
      {&ByteArrayMutator::FlipBit, &ByteArrayMutator::SwapBytes,
       &ByteArrayMutator::ChangeByte,
       &ByteArrayMutator::OverwriteFromDictionary},
      data);
}

bool ByteArrayMutator::MutateIncreaseSize(ByteArray &data) {
  return ApplyOneOf<2>(
      {&ByteArrayMutator::InsertBytes, &ByteArrayMutator::InsertFromDictionary},
      data);
}

bool ByteArrayMutator::MutateDecreaseSize(ByteArray &data) {
  return ApplyOneOf<1>({&ByteArrayMutator::EraseBytes}, data);
}

bool ByteArrayMutator::FlipBit(ByteArray &data) {
  uintptr_t random = rng_();
  size_t bit_idx = random % (data.size() * 8);
  size_t byte_idx = bit_idx / 8;
  bit_idx %= 8;
  uint8_t mask = 1 << bit_idx;
  data[byte_idx] ^= mask;
  return true;
}

bool ByteArrayMutator::SwapBytes(ByteArray &data) {
  size_t idx1 = rng_() % data.size();
  size_t idx2 = rng_() % data.size();
  std::swap(data[idx1], data[idx2]);
  return true;
}

bool ByteArrayMutator::ChangeByte(ByteArray &data) {
  size_t idx = rng_() % data.size();
  data[idx] = rng_();
  return true;
}

bool ByteArrayMutator::InsertBytes(ByteArray &data) {
  // Don't insert too many bytes at once.
  const size_t kMaxInsertSize = 20;
  size_t num_new_bytes = rng_() % kMaxInsertSize + 1;
  num_new_bytes = RoundUpToAdd(data.size(), num_new_bytes);
  if (num_new_bytes > kMaxInsertSize) {
    num_new_bytes -= size_alignment_;
  }
  // There are N+1 positions to insert something into an array of N.
  size_t pos = rng_() % (data.size() + 1);
  // Fixed array to avoid memory allocation.
  std::array<uint8_t, kMaxInsertSize> new_bytes;
  for (size_t i = 0; i < num_new_bytes; i++) new_bytes[i] = rng_();
  data.insert(data.begin() + pos, new_bytes.begin(),
              new_bytes.begin() + num_new_bytes);
  return true;
}

bool ByteArrayMutator::EraseBytes(ByteArray &data) {
  if (data.size() <= size_alignment_) return false;
  // Ok to erase a sizable chunk since small inputs are good (if they
  // produce good features).
  size_t num_bytes_to_erase = rng_() % (data.size() / 2) + 1;
  num_bytes_to_erase = RoundDownToRemove(data.size(), num_bytes_to_erase);
  if (num_bytes_to_erase == 0) return false;
  size_t pos = rng_() % (data.size() - num_bytes_to_erase + 1);
  data.erase(data.begin() + pos, data.begin() + pos + num_bytes_to_erase);
  return true;
}

void ByteArrayMutator::AddToDictionary(
    const std::vector<ByteArray> &dict_entries) {
  for (const ByteArray &entry : dict_entries) {
    if (entry.size() > DictEntry::kMaxEntrySize) continue;
    dictionary_.emplace_back(entry);
  }
}

bool ByteArrayMutator::OverwriteFromDictionary(ByteArray &data) {
  if (dictionary_.empty()) return false;
  size_t dict_entry_idx = rng_() % dictionary_.size();
  const auto &dic_entry = dictionary_[dict_entry_idx];
  if (dic_entry.size() > data.size()) return false;
  size_t overwrite_pos = rng_() % (data.size() - dic_entry.size() + 1);
  std::copy(dic_entry.begin(), dic_entry.end(), data.begin() + overwrite_pos);
  return true;
}

bool ByteArrayMutator::InsertFromDictionary(ByteArray &data) {
  if (dictionary_.empty()) return false;
  size_t dict_entry_idx = rng_() % dictionary_.size();
  const auto &dict_entry = dictionary_[dict_entry_idx];
  // There are N+1 positions to insert something into an array of N.
  size_t pos = rng_() % (data.size() + 1);
  data.insert(data.begin() + pos, dict_entry.begin(), dict_entry.end());
  return true;
}

void ByteArrayMutator::CrossOverInsert(ByteArray &data,
                                       const ByteArray &other) {
  if ((data.size() % size_alignment_) + other.size() < size_alignment_) return;
  // insert other[first:first+size] at data[pos]
  size_t size = 1 + rng_() % other.size();
  size = RoundUpToAdd(data.size(), size);
  if (size > other.size()) {
    size -= size_alignment_;
  }
  size_t first = rng_() % (other.size() - size + 1);
  size_t pos = rng_() % (data.size() + 1);
  data.insert(data.begin() + pos, other.begin() + first,
              other.begin() + first + size);
}

void ByteArrayMutator::CrossOverOverwrite(ByteArray &data,
                                          const ByteArray &other) {
  // Overwrite data[pos:pos+size] with other[first:first+size].
  // Overwrite no more than half of data.
  size_t max_size = std::max(1UL, data.size() / 2);
  size_t first = rng_() % other.size();
  max_size = std::min(max_size, other.size() - first);
  size_t size = 1 + rng_() % max_size;
  size_t max_pos = data.size() - size;
  size_t pos = rng_() % (max_pos + 1);
  std::copy(other.begin() + first, other.begin() + first + size,
            data.begin() + pos);
}

void ByteArrayMutator::CrossOver(ByteArray &data, const ByteArray &other) {
  if (rng_() % 2) {
    CrossOverInsert(data, other);
  } else {
    CrossOverOverwrite(data, other);
  }
}

void ByteArrayMutator::MutateMany(const std::vector<ByteArray> &inputs,
                                  size_t num_mutants, int crossover_level,
                                  std::vector<ByteArray> &mutants) {
  size_t num_inputs = inputs.size();
  mutants.resize(num_mutants);
  for (auto &mutant : mutants) {
    mutant = inputs[rng_() % num_inputs];
    if (rng_() % 100 < crossover_level) {
      // Perform crossover `crossover_level`% of the time.
      CrossOver(mutant, inputs[rng_() % num_inputs]);
    } else {
      Mutate(mutant);
    }
  }
}

}  // namespace centipede
