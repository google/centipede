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

#include "./corpus.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "./coverage.h"
#include "./defs.h"
#include "./feature.h"
#include "./logging.h"
#include "./util.h"

namespace centipede {

// TODO(kcc): [impl] add tests.
Coverage::PCIndexVec FeatureSet::ToCoveragePCs() const {
  absl::flat_hash_set<Coverage::PCIndex> pc_index_set;
  auto feature_domain = FeatureDomains::k8bitCounters;
  for (auto f : all_features_) {
    if (!feature_domain.Contains(f)) continue;
    pc_index_set.insert(Convert8bitCounterFeatureToPcIndex(f));
  }
  return Coverage::PCIndexVec(pc_index_set.begin(), pc_index_set.end());
}

size_t FeatureSet::CountFeatures(FeatureDomains::Domain domain) {
  size_t res = 0;
  for (auto f : all_features_) {
    if (domain.Contains(f)) {
      res++;
    }
  }
  return res;
}

__attribute__((noinline))  // to see it in profile.
size_t
FeatureSet::CountUnseenAndPruneFrequentFeatures(FeatureVec &features) const {
  size_t number_of_unseen_features = 0;
  size_t num_kept = 0;
  for (size_t i = 0, n = features.size(); i < n; i++) {
    auto feature = features[i];
    auto freq = frequencies_[Feature2Idx(feature)];
    if (freq == 0) {
      number_of_unseen_features++;
      features[num_kept++] = feature;
    } else {
      if (freq < frequency_threshold_) {
        features[num_kept++] = feature;
      }
    }
  }
  features.resize(num_kept);
  return number_of_unseen_features;
}

void FeatureSet::IncrementFrequencies(const FeatureVec &features) {
  for (auto f : features) {
    auto &freq = frequencies_[Feature2Idx(f)];
    if (freq == 0) all_features_.push_back(f);
    if (freq < frequency_threshold_) freq++;
  }
}

__attribute__((noinline))  // to see it in profile.
uint32_t
FeatureSet::ComputeWeight(const FeatureVec &features) const {
  uint32_t weight = 0;
  for (auto feature : features) {
    // The less frequent is the feature, the more valuable it is.
    // (frequency == 1) => (weight == 256)
    // (frequency == 2) => (weight == 128)
    // and so on.
    weight += 256 / frequencies_[Feature2Idx(feature)];
  }
  return weight;
}

//================= Corpus

size_t Corpus::Prune(const FeatureSet &fs) {
  if (records_.size() < 2UL) return 0;
  size_t num_pruned_now = 0;
  for (size_t i = 0; i < records_.size();) {
    fs.CountUnseenAndPruneFrequentFeatures(records_[i].features);
    auto new_weight = fs.ComputeWeight(records_[i].features);
    weighted_distribution_.ChangeWeight(i, new_weight);
    if (new_weight == 0) {
      // Swap last element of records_ with i-th, pop-back.
      std::swap(records_[i], records_[records_.size() - 1]);
      records_.pop_back();
      // Same for weighted_distribution_.
      auto weight = weighted_distribution_.PopBack();
      weighted_distribution_.ChangeWeight(i, weight);
      // Update prune counters.
      num_pruned_++;
      num_pruned_now++;
      continue;
    }
    i++;
  }
  weighted_distribution_.RecomputeInternalState();
  CHECK(!records_.empty());
  return num_pruned_now;
}

void Corpus::Add(const ByteArray &data, const FeatureVec &fv,
                 const FeatureSet &fs) {
  CHECK_EQ(records_.size(), weighted_distribution_.size());
  records_.push_back({data, fv});
  weighted_distribution_.AddWeight(fs.ComputeWeight(fv));
}

const ByteArray &Corpus::WeightedRandom(size_t random) const {
  return records_[weighted_distribution_.RandomIndex(random)].data;
}

const ByteArray &Corpus::UniformRandom(size_t random) const {
  return records_[random % records_.size()].data;
}

void Corpus::PrintStats(std::ostream &out, const FeatureSet &fs) {
  out << "{ \"corpus_stats\": [\n";
  std::string before_record = "";
  for (auto &record : records_) {
    out << before_record;
    before_record = ",\n";
    out << "  {";
    out << "\"size\": " << record.data.size() << ", ";
    {
      out << "\"frequencies\": [";
      std::string before_feature = "";
      for (auto feature : record.features) {
        out << before_feature;
        before_feature = ", ";
        out << fs.Frequency(feature);
      }
      out << "]";
    }
    out << "}";
  }
  out << "]}\n";
}

//================= WeightedDistribution
void WeightedDistribution::AddWeight(uint32_t weight) {
  CHECK_EQ(weights_.size(), cumulative_weights_.size());
  weights_.push_back(weight);
  if (cumulative_weights_.empty()) {
    cumulative_weights_.push_back(weight);
  } else {
    cumulative_weights_.push_back(cumulative_weights_.back() + weight);
  }
}

void WeightedDistribution::ChangeWeight(size_t idx, uint32_t new_weight) {
  CHECK_LT(idx, size());
  weights_[idx] = new_weight;
  cumulative_weights_valid_ = false;
}

__attribute__((noinline))  // to see it in profile.
void WeightedDistribution::RecomputeInternalState() {
  uint32_t partial_sum = 0;
  for (size_t i = 0, n = size(); i < n; i++) {
    partial_sum += weights_[i];
    cumulative_weights_[i] = partial_sum;
  }
  cumulative_weights_valid_ = true;
}

__attribute__((noinline))  // to see it in profile.
size_t
WeightedDistribution::RandomIndex(size_t random) const {
  CHECK(!weights_.empty());
  CHECK(cumulative_weights_valid_);
  uint32_t sum_of_all_weights = cumulative_weights_.back();
  if (sum_of_all_weights == 0)
    return random % size();  // can't do much else here.
  random = random % sum_of_all_weights;
  auto it = std::upper_bound(cumulative_weights_.begin(),
                             cumulative_weights_.end(), random);
  CHECK(it != cumulative_weights_.end());
  return it - cumulative_weights_.begin();
}

uint32_t WeightedDistribution::PopBack() {
  uint32_t result = weights_.back();
  weights_.pop_back();
  cumulative_weights_.pop_back();
  return result;
}

}  // namespace centipede
