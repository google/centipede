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

#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "./coverage.h"
#include "./defs.h"
#include "./feature.h"
#include "./logging.h"
#include "./util.h"

namespace centipede {

// TODO(kcc): [impl] add tests.
Coverage::PCIndexVec FeatureSet::ToCoveragePCs() const {
  return Coverage::PCIndexVec(pc_index_set_.begin(), pc_index_set_.end());
}

size_t FeatureSet::CountFeatures(FeatureDomains::Domain domain) {
  return features_per_domain_[domain.domain_id];
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
      ++number_of_unseen_features;
    }
    if (freq < FrequencyThreshold(feature)) {
      features[num_kept++] = feature;
    }
  }
  features.resize(num_kept);
  return number_of_unseen_features;
}

void FeatureSet::IncrementFrequencies(const FeatureVec &features) {
  for (auto f : features) {
    auto &freq = frequencies_[Feature2Idx(f)];
    if (freq == 0) {
      ++num_features_;
      ++features_per_domain_[FeatureDomains::Domain::FeatureToDomainId(f)];
      if (FeatureDomains::k8bitCounters.Contains(f))
        pc_index_set_.insert(Convert8bitCounterFeatureToPcIndex(f));
    }
    if (freq < FrequencyThreshold(f)) ++freq;
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
    // The less frequent is the domain, the more valuable are its features.
    auto domain_id = FeatureDomains::Domain::FeatureToDomainId(feature);
    auto features_in_domain = features_per_domain_[domain_id];
    CHECK_GT(features_in_domain, 0) << VV(feature) << VV(domain_id);
    auto domain_weight = num_features_ / features_in_domain;
    auto feature_idx = Feature2Idx(feature);
    auto feature_frequency = frequencies_[feature_idx];
    CHECK_GT(feature_frequency, 0) << VV(feature) << VV(feature_idx);
    weight += domain_weight * (256 / feature_frequency);
  }
  return weight;
}

//================= Corpus

// Returns the weight of `fv` computed using `fs` and `coverage_frontier`.
static size_t ComputeWeight(const FeatureVec &fv, const FeatureSet &fs,
                            const CoverageFrontier &coverage_frontier) {
  size_t weight = fs.ComputeWeight(fv);
  size_t num_features_in_frontier = 0;
  for (const auto feature : fv) {
    if (!FeatureDomains::k8bitCounters.Contains(feature)) continue;
    const auto pc_index = Convert8bitCounterFeatureToPcIndex(feature);
    if (coverage_frontier.PcIndexIsFrontier(pc_index)) {
      ++num_features_in_frontier;
    }
  }
  return weight * (num_features_in_frontier + 1);  // Multiply by at least 1.
}

size_t Corpus::Prune(const FeatureSet &fs,
                     const CoverageFrontier &coverage_frontier,
                     size_t max_corpus_size, Rng &rng) {
  // TODO(kcc): use coverage_frontier.
  CHECK(max_corpus_size);
  if (records_.size() < 2UL) return 0;
  // Recompute the weights.
  size_t num_zero_weights = 0;
  for (size_t i = 0, n = records_.size(); i < n; ++i) {
    fs.CountUnseenAndPruneFrequentFeatures(records_[i].features);
    auto new_weight =
        ComputeWeight(records_[i].features, fs, coverage_frontier);
    weighted_distribution_.ChangeWeight(i, new_weight);
    num_zero_weights += new_weight == 0;
  }

  // Remove zero weights and the corresponding corpus record.
  // Also remove some random elements, if the corpus is still too big.
  // The corpus must not be empty, hence target_size is at least 1.
  // It should also be <= max_corpus_size.
  size_t target_size = std::min(
      max_corpus_size, std::max(1UL, records_.size() - num_zero_weights));
  auto subset_to_remove =
      weighted_distribution_.RemoveRandomWeightedSubset(target_size, rng);
  RemoveSubset(subset_to_remove, records_);

  weighted_distribution_.RecomputeInternalState();
  CHECK(!records_.empty());

  // Features may have shrunk from CountUnseenAndPruneFrequentFeatures.
  // Call shrink_to_fit for the features that survived the pruning.
  for (auto &record : records_) {
    record.features.shrink_to_fit();
  }

  num_pruned_ += subset_to_remove.size();
  return subset_to_remove.size();
}

void Corpus::Add(const ByteArray &data, const FeatureVec &fv,
                 const FeatureSet &fs,
                 const CoverageFrontier &coverage_frontier) {
  // TODO(kcc): use coverage_frontier.
  CHECK(!data.empty());
  CHECK_EQ(records_.size(), weighted_distribution_.size());
  records_.push_back({data, fv});
  weighted_distribution_.AddWeight(ComputeWeight(fv, fs, coverage_frontier));
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

std::string Corpus::MemoryUsageString() const {
  size_t data_size = 0;
  size_t features_size = 0;
  for (const auto &record : records_) {
    data_size += record.data.capacity() * sizeof(record.data[0]);
    features_size += record.features.capacity() * sizeof(record.features[0]);
  }
  return absl::StrCat("d", data_size >> 20, "/f", features_size >> 20);
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

//================= CoverageFrontier
size_t CoverageFrontier::Compute(const Corpus &corpus) {
  std::fill(frontier_.begin(), frontier_.end(), false);

  // Use frontier_ as a scratch to record all PCs covered by corpus.
  for (const auto &record : corpus.records_) {
    for (auto feature : record.features) {
      if (!FeatureDomains::k8bitCounters.Contains(feature)) continue;
      size_t idx = Convert8bitCounterFeatureToPcIndex(feature);
      if (idx >= pc_table_.size()) continue;
      frontier_[idx] = true;
    }
  }

  // Iterate all functions, set frontier_[] depending on whether the function
  // is partially covered or not.
  num_functions_in_frontier_ = 0;
  IteratePcTableFunctions(pc_table_, [&](size_t beg, size_t end) {
    auto frontier_begin = frontier_.begin() + beg;
    auto frontier_end = frontier_.begin() + end;
    size_t cov_size_in_this_func =
        std::count(frontier_begin, frontier_end, true);
    if (cov_size_in_this_func == 0) return;  // Function not covered.
    if (cov_size_in_this_func == end - beg) {
      // function fully covered => not in the frontier.
      std::fill(frontier_begin, frontier_end, false);
      return;
    }
    // This function is in the frontier.
    std::fill(frontier_begin, frontier_end, true);
    ++num_functions_in_frontier_;
  });
  return num_functions_in_frontier_;
}

}  // namespace centipede
