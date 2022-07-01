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

#include <array>
#include <cstdint>

namespace centipede {
namespace FeatureDomains {

uint32_t Importance(feature_t feature) {
  // The mapping between the domain and its importance.
  // We don't embedd it into the domain definition because we may eventually
  // need a more dynamic mapping (computed on the flight, coming from flags,
  // etc).
  static auto importance_by_domain = [&]() {
    std::array<uint32_t, Domain::kLastDomain + 1> res;
    res[Domain::kUnknown] = 1;
    res[Domain::k8bitCounters] = 100;
    res[Domain::kDataFlow] = 10;
    res[Domain::kCMP] = 10;
    res[Domain::kBoundedPath] = 1;
    res[Domain::kPCPair] = 1;
    res[Domain::kLastDomain] = 1;
    return res;
  }();
  return importance_by_domain[Domain::FeatureToDomainId(feature)];
}

}  // namespace FeatureDomains
}  // namespace centipede
