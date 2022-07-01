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

#ifndef THIRD_PARTY_CENTIPEDE_CORPUS_H_
#define THIRD_PARTY_CENTIPEDE_CORPUS_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "./coverage.h"
#include "./defs.h"
#include "./feature.h"
#include "./util.h"

namespace centipede {

// Set of features with their frequencies.
// Features that have a frequency >= frequency_threshold
// are considered too frequent and thus less interesting for further fuzzing.
// FeatureSet is "a bit lossy", i.e. it may fail to distinguish some
// different features as such. But in practice such collisions should be rare.
class FeatureSet {
 public:
  FeatureSet(uint8_t frequency_threshold)
      : frequency_threshold_(frequency_threshold), frequencies_(kSize) {}

  // Returns the number of features in `features` not present in `this`.
  // Removes all features from `features` that are too frequent.
  size_t CountUnseenAndPruneFrequentFeatures(FeatureVec &features) const;

  // For every feature in `features` increment its frequency.
  // If a feature wasn't seen before, it is added to `this`.
  void IncrementFrequencies(const FeatureVec &features);

  // How many different features are in the set.
  size_t size() const { return num_features_; }

  // Returns features that originate from CFG counters, converted to PCIndexVec.
  Coverage::PCIndexVec ToCoveragePCs() const;

  // Returns the number of features in `this` from the given feature domain.
  size_t CountFeatures(FeatureDomains::Domain domain);

  // Returns the frequency associated with `feature`.
  size_t Frequency(feature_t feature) const {
    return frequencies_[Feature2Idx(feature)];
  }

  // Computes combined weight of `features`.
  // The less frequent the feature is, the bigger its weight.
  // The weight of a FeatureVec is a sum of individual feature weights.
  uint32_t ComputeWeight(const FeatureVec &features) const;

 private:
  const uint8_t frequency_threshold_;

  // Size of frequencies_. The bigger this is, the fewer collisions there are.
  static const size_t kSize = 1ULL << 28;

  // Maps feature into an index in frequencies_.
  size_t Feature2Idx(feature_t feature) const {
    return __builtin_ia32_crc32di(feature,
                                  __builtin_ia32_crc32di(feature >> 32, 0)) %
           kSize;
  }

  // Maps features to their frequencies.
  // The index into this array is Feature2Idx(feature), and this is
  // where collisions are possible.
  std::vector<uint8_t> frequencies_;

  // Counts all unique features added to this.
  size_t num_features_ = 0;

  // Counts features in each domain.
  size_t features_per_domain_[FeatureDomains::Domain::kLastDomain + 1] = {};

  // Maintains the set of PC indices that correspond to added features.
  absl::flat_hash_set<Coverage::PCIndex> pc_index_set_;
};

// WeightedDistribution maintains an array of integer weights.
// It allows to compute a random number in range [0,size()) such that
// the probability of each number is proportional to its weight.
class WeightedDistribution {
 public:
  // Adds one more weight.
  void AddWeight(uint32_t weight);
  // Removes the last weight and returns it.
  // Precondition: size() > 0.
  uint32_t PopBack();
  // Changes the existing idx-th weight to new_weight.
  void ChangeWeight(size_t idx, uint32_t new_weight);
  // Returns a random number in [0,size()), using a random number `random`.
  // For proper randomness, `random` should come from a 64-bit RNG.
  // RandomIndex() must not be called after ChangeWeight() without first
  // calling RecomputeInternalState().
  size_t RandomIndex(size_t random) const;
  // Returns the number of weights.
  size_t size() const { return weights_.size(); }
  // Removes all weights.
  void clear() {
    weights_.clear();
    cumulative_weights_.clear();
  }
  // Fixes the internal state that could become stale
  // after call(s) to ChangeWeight().
  void RecomputeInternalState();

 private:
  // The array of weights. The probability of choosing the index Idx
  // is weights_[Idx] / SumOfAllWeights.
  std::vector<uint32_t> weights_;
  // i-th element is the sum of the first i elements of weights_.
  std::vector<uint32_t> cumulative_weights_;
  // If false, cumulative_weights_ needs to be recomputed.
  bool cumulative_weights_valid_ = true;
};

// Maintains the corpus of inputs.
// Allows to prune (forget) inputs that become uninteresting.
class Corpus {
 public:
  // Adds a corpus element, consisting of 'data' (the input bytes)
  // and 'fv' (the features associated with this input).
  // `fs` is used to compute weights of `fv`.
  void Add(const ByteArray &data, const FeatureVec &fv, const FeatureSet &fs);
  // Returns the total number of inputs added.
  size_t NumTotal() const { return num_pruned_ + NumActive(); }
  // Return the number of currently active inputs, i.e. inputs that we want to
  // keep mutating.
  size_t NumActive() const { return records_.size(); }
  // Returns the max and avg sizes of the inputs.
  std::pair<size_t, size_t> MaxAndAvgSize() const {
    if (records_.empty()) return {0, 0};
    size_t max = 0, tot = 0;
    for (auto &cr : records_) {
      max = std::max(max, cr.data.size());
      tot += cr.data.size();
    }
    return {max, tot / records_.size()};
  }
  // Returns a random active corpus element using weighted distribution.
  // See WeightedDistribution.
  const ByteArray &WeightedRandom(size_t random) const;
  // Returns a random active corpus element using uniform distribution.
  const ByteArray &UniformRandom(size_t random) const;
  // Returns the element with index 'idx', where `idx` < NumActive().
  const ByteArray &Get(size_t idx) const { return records_[idx].data; }
  // Removes elements that contain only frequent features, according to 'fs'.
  // Returns the number of removed elements.
  size_t Prune(const FeatureSet &fs);
  // Prints corpus stats in JSON format to `out` using `fs` for frequencies.
  void PrintStats(std::ostream &out, const FeatureSet &fs);

  // Returns a string used for logging the corpus memory usage.
  std::string MemoryUsageString() const;

 private:
  std::vector<CorpusRecord> records_;
  // Maintains weights for elements of records_.
  WeightedDistribution weighted_distribution_;
  size_t num_pruned_ = 0;
};

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_CORPUS_H_
