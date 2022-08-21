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

#include <cstddef>
#include <string>
#include <vector>

#include "googletest/include/gtest/gtest.h"
#include "absl/container/flat_hash_set.h"
#include "./defs.h"

namespace centipede {

namespace {

// Tests that two mutators seeded with different rng seeds produce different
// results.
TEST(ByteArrayMutator, Randomness) {
  ByteArrayMutator mutator[2]{1, 2};

  std::vector<ByteArray> res[2];
  for (size_t i = 0; i < 2; i++) {
    ByteArray seed = {0};
    // Just run a few iterations.
    for (size_t iter = 0; iter < 100; iter++) {
      mutator[i].Mutate(seed);
      res[i].push_back(seed);
    }
  }
  EXPECT_NE(res[0], res[1]);
}

// Tests a callback `fn`: mutations of `seed` are expected to eventually
// match all of `expected_mutants`, but never any of `unexpected_mutants`.
// Mutators that do a single-step can be tested for `unexpected_mutants`,
// while for more complicated mutators `unexpected_mutants` should be empty.
void TestMutatorFn(ByteArrayMutator::Fn fn, const ByteArray &seed,
                   const std::vector<ByteArray> &expected_mutants,
                   const std::vector<ByteArray> &unexpected_mutants,
                   const std::vector<ByteArray> &dictionary = {},
                   size_t num_iterations = 100000000) {
  ByteArrayMutator mutator(1);
  mutator.AddToDictionary(dictionary);
  absl::flat_hash_set<ByteArray> expected(expected_mutants.begin(),
                                          expected_mutants.end());
  absl::flat_hash_set<ByteArray> unexpected(unexpected_mutants.begin(),
                                            unexpected_mutants.end());
  ByteArray mutant;  // create outside the loop to avoid malloc in the loop.
  for (size_t i = 0; i < num_iterations; i++) {
    mutant = seed;
    (mutator.*fn)(mutant);
    expected.erase(mutant);
    if (expected.empty()) break;
    EXPECT_FALSE(unexpected.contains(mutant));
  }
  EXPECT_TRUE(expected.empty());
}

TEST(ByteArrayMutator, ChangeByte) {
  TestMutatorFn(&ByteArrayMutator::ChangeByte, {1, 2, 3},
                {{1, 2, 4}, {42, 2, 3}, {1, 66, 3}},  // expected_mutants
                {{9, 9, 3}, {1, 8, 8}, {7, 2, 7}}     // unexpected_mutants
  );
}

TEST(ByteArrayMutator, FlipBit) {
  TestMutatorFn(&ByteArrayMutator::FlipBit, {0, 7, 10},
                {{1, 7, 10}, {0, 6, 10}, {0, 7, 11}},  // expected_mutants
                {{1, 6, 10}, {0, 6, 11}}               // unexpected_mutants
  );
}

TEST(ByteArrayMutator, SwapBytes) {
  TestMutatorFn(&ByteArrayMutator::SwapBytes, {0, 1, 2},
                {{0, 2, 1}, {1, 0, 2}, {2, 1, 0}},  // expected_mutants
                {{2, 0, 1}}                         // unexpected_mutants
  );
}

TEST(ByteArrayMutator, InsertBytes) {
  TestMutatorFn(&ByteArrayMutator::InsertBytes, {0, 1, 2},
                {
                    // expected_mutants
                    {0, 1, 2, 3},
                    {0, 3, 1, 2},
                    {3, 0, 1, 2},
                    {0, 1, 2, 3, 4},
                    {0, 3, 4, 1, 2},
                    {3, 4, 0, 1, 2},
                },
                // unexpected_mutants
                {{0, 1}, {0, 1, 2}, {0, 3, 1, 4, 2}});
}

// Currently, same as for InsertBytes. Will change in future as we add more
// mutators.
TEST(ByteArrayMutator, MutateIncreaseSize) {
  TestMutatorFn(&ByteArrayMutator::MutateIncreaseSize, {0, 1, 2},
                {
                    // expected_mutants
                    {0, 1, 2, 3},
                    {0, 3, 1, 2},
                    {3, 0, 1, 2},
                    {0, 1, 2, 3, 4},
                    {0, 3, 4, 1, 2},
                    {3, 4, 0, 1, 2},
                },
                // unexpected_mutants
                {{0, 1}, {0, 3, 1, 4, 2}});
}

TEST(ByteArrayMutator, EraseBytes) {
  TestMutatorFn(&ByteArrayMutator::EraseBytes, {0, 1, 2, 3},
                // expected_mutants
                {
                    {0, 1, 2},
                    {0, 1, 3},
                    {0, 2, 3},
                    {1, 2, 3},
                    {0, 1},
                    {0, 3},
                    {2, 3},
                },
                // unexpected_mutants
                {{0}, {1}, {2}});
}

// Currently, same as EraseBytes. Will change in future as we add more mutators.
TEST(ByteArrayMutator, MutateDecreaseSize) {
  TestMutatorFn(&ByteArrayMutator::MutateDecreaseSize, {0, 1, 2, 3},
                // expected_mutants
                {
                    {0, 1, 2},
                    {0, 1, 3},
                    {0, 2, 3},
                    {1, 2, 3},
                    {0, 1},
                    {0, 3},
                    {2, 3},
                },
                // unexpected_mutants
                {{0}, {1}, {2}});
}

// Tests that MutateSameSize will eventually produce
// all possible mutants of size 1 and 2.
// Also tests some of the 3-byte mutants.
TEST(ByteArrayMutator, MutateSameSize) {
  ByteArrayMutator mutator(1);
  for (size_t size = 1; size <= 2; size++) {
    ByteArray data(size);
    absl::flat_hash_set<ByteArray> set;
    size_t expected_set_size = 1 << (8 * size);
    for (size_t iter = 0; iter < 2000000ULL; iter++) {
      mutator.MutateSameSize(data);
      EXPECT_EQ(data.size(), size);
      set.insert(data);
      if (set.size() == expected_set_size) break;
    }
    EXPECT_EQ(expected_set_size, set.size());
  }

  // One step of MutateSameSize may generate any mutant
  // that can be generated by one step of its submutants.
  // No mutant of other length may appear.
  std::vector<ByteArray> unexpected_mutants = {{1, 2}, {1, 2, 3, 4}};
  TestMutatorFn(&ByteArrayMutator::MutateSameSize, {1, 2, 3},
                {{1, 2, 4}, {42, 2, 3}, {1, 66, 3}},  // expected_mutants
                unexpected_mutants);
  TestMutatorFn(&ByteArrayMutator::MutateSameSize, {0, 7, 10},
                {{1, 7, 10}, {0, 6, 10}, {0, 7, 11}},  // expected_mutants
                unexpected_mutants);
  TestMutatorFn(&ByteArrayMutator::MutateSameSize, {0, 1, 2},
                {{0, 2, 1}, {1, 0, 2}, {2, 1, 0}},  // expected_mutants
                unexpected_mutants);
}

TEST(ByteArrayMutator, Mutate) {
  TestMutatorFn(&ByteArrayMutator::Mutate, {1, 2, 3},
                // expected_mutants
                {{1, 2, 4}, {1, 2}, {1, 2, 3, 4}},
                // unexpected_mutants
                {{/*empty array*/}});
}

TEST(ByteArrayMutator, OverwriteFromDictionary) {
  TestMutatorFn(
      &ByteArrayMutator::OverwriteFromDictionary, {1, 2, 3, 4, 5},
      // expected_mutants
      {{1, 2, 7, 8, 9},
       {1, 7, 8, 9, 5},
       {7, 8, 9, 4, 5},
       {1, 2, 3, 0, 6},
       {1, 2, 0, 6, 5},
       {1, 0, 6, 4, 5},
       {0, 6, 3, 4, 5}},
      // unexpected_mutants
      {{1, 2, 3, 7, 8}, {8, 9, 3, 4, 5}, {6, 2, 3, 4, 5}, {1, 2, 3, 4, 0}},
      // dictionary
      {{7, 8, 9}, {0, 6}});
}

TEST(ByteArrayMutator, InsertFromDictionary) {
  TestMutatorFn(&ByteArrayMutator::InsertFromDictionary, {1, 2, 3},
                // expected_mutants
                {{1, 2, 3, 4, 5},
                 {1, 2, 4, 5, 3},
                 {1, 4, 5, 2, 3},
                 {4, 5, 1, 2, 3},
                 {1, 2, 3, 6, 7, 8},
                 {1, 2, 6, 7, 8, 3},
                 {1, 6, 7, 8, 2, 3},
                 {6, 7, 8, 1, 2, 3}},
                // unexpected_mutants
                {{1, 2, 3, 7, 8}, {7, 8, 1, 2, 3}},
                // dictionary
                {{4, 5}, {6, 7, 8}});
}

// Tests CrossOver* mutations.
// With CrossOver, no random values are involved, only random offsets,
// and so we can test for all possible expected mutants.
void TestCrossOver(void (ByteArrayMutator::*fn)(ByteArray &, const ByteArray &),
                   const ByteArray &seed, const ByteArray &other,
                   const std::vector<ByteArray> &all_possible_mutants) {
  ByteArrayMutator mutator(1);
  absl::flat_hash_set<ByteArray> expected(all_possible_mutants.begin(),
                                          all_possible_mutants.end());
  absl::flat_hash_set<ByteArray> found;
  const int kNumIter = 10000;
  // Run for some number of iterations, make sure we saw all expected mutations
  // and nothing else.
  for (int i = 0; i < kNumIter; i++) {
    ByteArray mutant = seed;
    (mutator.*fn)(mutant, other);
    EXPECT_EQ(expected.count(mutant), 1);
    found.insert(mutant);
  }
  EXPECT_EQ(expected, found);
}

TEST(ByteArrayMutator, CrossOverInsert) {
  TestCrossOver(&ByteArrayMutator::CrossOverInsert, {1}, {2},
                {
                    {1, 2},
                    {2, 1},
                });
  TestCrossOver(&ByteArrayMutator::CrossOverInsert, {1, 2}, {3},
                {
                    {1, 2, 3},
                    {1, 3, 2},
                    {3, 1, 2},
                });
  TestCrossOver(&ByteArrayMutator::CrossOverInsert, {1}, {2, 3},
                {
                    {1, 2, 3},
                    {2, 3, 1},
                    {2, 1},
                    {1, 2},
                    {3, 1},
                    {1, 3},
                });
  TestCrossOver(&ByteArrayMutator::CrossOverInsert, {1, 2}, {3, 4},
                {
                    {1, 2, 3, 4},
                    {1, 3, 4, 2},
                    {3, 4, 1, 2},
                    {1, 2, 3},
                    {1, 3, 2},
                    {3, 1, 2},
                    {1, 2, 4},
                    {1, 4, 2},
                    {4, 1, 2},
                });
}

TEST(ByteArrayMutator, CrossOverOverwrite) {
  TestCrossOver(&ByteArrayMutator::CrossOverOverwrite, {1}, {2},
                {
                    {2},
                });
  TestCrossOver(&ByteArrayMutator::CrossOverOverwrite, {1, 2}, {3},
                {
                    {1, 3},
                    {3, 2},
                });
  TestCrossOver(&ByteArrayMutator::CrossOverOverwrite, {1}, {2, 3},
                {
                    {2},
                    {3},
                });
  TestCrossOver(&ByteArrayMutator::CrossOverOverwrite, {1, 2}, {3, 4},
                {
                    {1, 3},
                    {3, 2},
                    {1, 4},
                    {4, 2},
                });
  TestCrossOver(&ByteArrayMutator::CrossOverOverwrite, {1, 2, 3, 4, 5, 6},
                {7, 8},
                {
                    // overwrite with {7}
                    {7, 2, 3, 4, 5, 6},
                    {1, 7, 3, 4, 5, 6},
                    {1, 2, 7, 4, 5, 6},
                    {1, 2, 3, 7, 5, 6},
                    {1, 2, 3, 4, 7, 6},
                    {1, 2, 3, 4, 5, 7},
                    // overwrite with {8}
                    {8, 2, 3, 4, 5, 6},
                    {1, 8, 3, 4, 5, 6},
                    {1, 2, 8, 4, 5, 6},
                    {1, 2, 3, 8, 5, 6},
                    {1, 2, 3, 4, 8, 6},
                    {1, 2, 3, 4, 5, 8},
                    // overwrite with {7, 8}
                    {7, 8, 3, 4, 5, 6},
                    {1, 7, 8, 4, 5, 6},
                    {1, 2, 7, 8, 5, 6},
                    {1, 2, 3, 7, 8, 6},
                    {1, 2, 3, 4, 7, 8},
                });
}

TEST(ByteArrayMutator, CrossOver) {
  // Most of CrossOver is tested above in CrossOverOverwrite/CrossOverInsert.
  // Here just test one set of inputs to ensure CrossOver calls the other
  // two functions correctly.
  TestCrossOver(&ByteArrayMutator::CrossOver, {1, 2}, {3, 4},
                {
                    // CrossOverInsert
                    {1, 2, 3, 4},
                    {1, 3, 4, 2},
                    {3, 4, 1, 2},
                    {1, 2, 3},
                    {1, 3, 2},
                    {3, 1, 2},
                    {1, 2, 4},
                    {1, 4, 2},
                    {4, 1, 2},
                    // CrossOverOverwrite
                    {1, 3},
                    {3, 2},
                    {1, 4},
                    {4, 2},
                });
}

TEST(ByteArrayMutator, FailedMutations) {
  const int kNumIter = 1000000;
  ByteArray data = {1, 2, 3, 4, 5};
  ByteArrayMutator mutator(1);
  size_t num_failed_erase = 0;
  size_t num_failed_generic = 0;
  for (int i = 0; i < kNumIter; i++) {
    num_failed_erase += !mutator.EraseBytes(data);
    num_failed_generic += !mutator.Mutate(data);
  }
  // EraseBytes() will fail sometimes, but should not fail too often.
  EXPECT_GT(num_failed_erase, 0);
  EXPECT_LT(num_failed_erase, kNumIter / 2);
  // The generic Mutate() should fail very infrequently.
  EXPECT_LT(num_failed_generic, kNumIter / 1000);
}

}  // namespace

}  // namespace centipede
