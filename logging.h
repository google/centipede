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

// This file either includes "base/logging.h" (default) or defines a
// rudimentary logging interface similar to a subset of "base/logging.h"
// (when CENTIPEDE_USE_OWN_LOGGING is defined).

// TODO(b/229642878): [cleanup] Switch to //third_party/absl/log once available.

#ifndef THIRD_PARTY_CENTIPEDE_LOGGING_H_
#define THIRD_PARTY_CENTIPEDE_LOGGING_H_

#include <stdlib.h>

#include <iostream>

namespace centipede {

class TinyLogger {
 public:
  TinyLogger(std::string_view file, int line, bool fatal) : fatal_{fatal} {
    std::cerr << file << ":" << line << "] ";
  }
  ~TinyLogger() {
    std::cerr << std::endl;
    if (fatal_) abort();
  }
  template <typename Type>
  TinyLogger &operator<<(const Type &data) {
    std::cerr << data;
    return *this;
  }

 private:
  const bool fatal_;
};

}  // namespace centipede

#define LOG_IMPL(fatal) \
  TinyLogger { __FILE__, __LINE__, (fatal) }
#undef LOG
#define LOG(anything) LOG_IMPL(false)
#undef VLOG
#define VLOG(logging_level) LOG(INFO)  // ignore logging_level

#undef CHECK_COND
   // NOTE: The `if` is intentionally dangling to allow trailing `<<`s.
   // clang-format off
#define CHECK_COND(a, b, cond) \
  if (!((a)cond(b)))                                        \
    LOG_IMPL(true) << "Check failed: " << #a << #cond << #b \
                   << " [" << (a) << #cond << (b) << "] "
   // clang-format on
#undef CHECK_EQ
#define CHECK_EQ(a, b) CHECK_COND(a, b, ==)
#undef CHECK_NE
#define CHECK_NE(a, b) CHECK_COND(a, b, !=)
#undef CHECK_GT
#define CHECK_GT(a, b) CHECK_COND(a, b, >)
#undef CHECK_GE
#define CHECK_GE(a, b) CHECK_COND(a, b, >=)
#undef CHECK_LT
#define CHECK_LT(a, b) CHECK_COND(a, b, <)
#undef CHECK_LE
#define CHECK_LE(a, b) CHECK_COND(a, b, <=)
#undef CHECK
#define CHECK(condition) CHECK_NE(!!(condition), false)

// Easy variable value logging: LOG(INFO) << VV(foo) << VV(bar);
#define VV(x) #x "=" << (x) << " "

#endif  // THIRD_PARTY_CENTIPEDE_LOGGING_H_
