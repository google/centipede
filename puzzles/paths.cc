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

// Centipede puzzle: almost no control flow, needs paths to find the goal.
// We disable use_dataflow_features because on this puzzle
// it is also effective.

// RUN: Run --use_dataflow_features=0 --path_level=10
// RUN: ExpectInLog "input bytes: .x1.x2.x3"

// This puzzle aborts on input "\x1\x2\x3"
// The code here has very little control flow, but an exponential number of
// call paths. The input above triggers a call sequence F1->F2->F3.
// The value `sink` generated by these functions represents the call sequence.
// The goal of this test is to verify that "bounded-path" features work.
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

static volatile uint64_t sink;

using Func = void (*)();

template <size_t kIdx>
static void F() {
  sink = sink * 16 + kIdx;
}

void FN() {}

// All functions in an array.
static Func table[256] = {
    F<0>, F<1>, F<2>,  F<3>,  F<4>,  F<5>,  F<6>,  F<7>,
    F<8>, F<9>, F<10>, F<11>, F<12>, F<13>, F<14>, F<15>,
    // The rest are set to FN below.
};

// Set table[16:256] to FN.
struct TableCtor {
  TableCtor() {
    for (size_t i = 16; i < 256; ++i) table[i] = FN;
  }
};

static TableCtor table_ctor;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size != 3) return -1;
  sink = 0;
  table[data[0]]();
  table[data[1]]();
  table[data[2]]();
  sink /= 0x123 - sink;  // Generates a crash (div by zero) w/o a control flow.
  return 0;
}
