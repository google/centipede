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

#include "./feature.h"

#include <cstdint>

namespace centipede {
namespace feature_domains {
namespace {

struct DomainImportance {
  Domain domain;
  uint32_t importance;
};

// The mapping between the domain and its importance.
// We don't embedd it into the domain definition because we may eventually
// need a more dynamic mapping (computed on the flight, coming from flags, etc).
const DomainImportance domain_importance[] = {
    {kUnknown, 1}, {k8bitCounters, 100}, {kDataFlow, 10},
    {kCMP, 10},    {kBoundedPath, 1},    {kPCPair, 1},
};

}  // namespace

uint32_t Importance(feature_t feature) {
  for (const auto &di : domain_importance) {
    if (di.domain.Contains(feature)) return di.importance;
  }
  return 1;  // not one of the known ranges.
}

}  // namespace feature_domains
}  // namespace centipede
