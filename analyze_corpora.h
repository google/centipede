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

#ifndef THIRD_PARTY_CENTIPEDE_ANALYZE_CORPORA_H
#define THIRD_PARTY_CENTIPEDE_ANALYZE_CORPORA_H

#include "./binary_info.h"
#include "./corpus.h"

namespace centipede {

// Analyzes two corpora, `a` and `b`, reports the differences.
void AnalyzeCorpora(const BinaryInfo &binary_info,
                    const std::vector<CorpusRecord> &a,
                    const std::vector<CorpusRecord> &b);

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_ANALYZE_CORPORA_H
