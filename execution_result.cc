// Copyright 2022 Google LLC.
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

#include "./execution_result.h"

#include "./logging.h"
#include "./shared_memory_blob_sequence.h"

namespace centipede {

bool BatchResult::WriteOneFeatureVec(const feature_t *vec, size_t size,
                                     SharedMemoryBlobSequence &blobseq) {
  return blobseq.Write({1 /*unused tag*/, size * sizeof(vec[0]),
                        reinterpret_cast<const uint8_t *>(vec)});
}

bool BatchResult::Read(SharedMemoryBlobSequence &blobseq) {
  num_outputs_read_ = 0;
  while (true) {
    auto blob = blobseq.Read();
    if (blob.size == 0) break;
    if (num_outputs_read_ >= results().size()) return false;
    auto features_beg = reinterpret_cast<const feature_t *>(blob.data);
    size_t features_size = blob.size / sizeof(features_beg[0]);
    FeatureVec &features = results()[num_outputs_read_].mutable_features();
    // if featurres.capacity() >= features_size, this will not cause malloc.
    features.resize(0);
    features.insert(features.begin(), features_beg,
                    features_beg + features_size);
    num_outputs_read_++;
  }
  // Missing outputs should have their features empty.
  for (size_t i = num_outputs_read_; i < results().size(); i++) {
    CHECK(results()[i].features().empty());
  }
  return true;
}

}  // namespace centipede
