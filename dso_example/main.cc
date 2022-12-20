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

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

void FuzzMe(const uint8_t* data, size_t size);  // From a separate DSO

// Reads input files (argv[1:]) one by one and calls FuzzMe() on each file's
// contents.
// When using Centipede with '--binary="./main_executable @@"' only one argument
// will be passed.
// TODO(kcc): if we need to pass more than one input to the main_executable
// while fuzzing, more work needs to be done on the Centipede side.
int main(int argc, char* argv[]) {
  for (int i = 1; i < argc; ++i) {
    std::ifstream file(argv[i], std::ios::in | std::ios::binary);
    std::vector<uint8_t> bytes{std::istream_iterator<uint8_t>(file),
                               std::istream_iterator<uint8_t>()};

    std::cout << bytes.size() << " bytes read from " << argv[i] << "\n";
    // This is where we call into the instrumented DSO.
    FuzzMe(bytes.data(), bytes.size());
  }
}
