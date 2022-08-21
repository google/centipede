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

#ifndef THIRD_PARTY_CENTIPEDE_DEFS_H_
#define THIRD_PARTY_CENTIPEDE_DEFS_H_

// Only simple definitions here. Minimal code, no dependencies.

#include <cassert>
#include <cstdint>
#include <random>
#include <vector>

namespace centipede {

// Just a good random number generator.
using Rng = std::mt19937_64;

// A sequence of unsigned 8-bit chars.
class ByteArray : public std::vector<uint8_t> {
  using Base = std::vector<uint8_t>;

 public:
  using Base::Base;

  // This ctor is to allow usage such as `ByteArray({'\xAB'})`: C++'s character
  // literals have type `char`, so '\xAB' evaluates to -85 which cannot be
  // implicitly narrowed to `uint8_t`, failing compilation on some toolchains.
  ByteArray(std::initializer_list<int> init) {
    reserve(init.size());
    for (auto i : init) {
      // The combined allowed range: max possible [-128; 127] for 'a'- or
      // '\xAB'-style initializers + enforced [0; 255] for 123- or 0xAB-style
      // initializers.
      assert(-128 <= i && i <= 255);
      push_back(static_cast<uint8_t>(i));
    }
  }
};

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_DEFS_H_
