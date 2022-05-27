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

#include "./util.h"

#include <unistd.h>

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/flat_hash_map.h"
#include "absl/synchronization/mutex.h"
#include "./defs.h"
#include "./feature.h"
#include "./logging.h"

namespace centipede {

size_t GetRandomSeed(size_t seed) {
  if (seed != 0) return seed;
  return time(0) + getpid() +
         std::hash<std::thread::id>{}(std::this_thread::get_id());
}

std::string AsString(const ByteArray &data, size_t max_len) {
  std::ostringstream out;
  size_t len = std::min(max_len, data.size());
  for (size_t i = 0; i < len; ++i) {
    char ch = data[i];
    if (std::isprint(ch)) {
      out << ch;
    } else {
      out << std::hex << ch;
    }
  }
  return out.str();
}

template <typename Container>
void ReadFromLocalFile(std::string_view file_path, Container &data) {
  std::ifstream f(std::string{file_path});
  if (!f) return;
  f.seekg(0, std::ios_base::end);
  size_t size = f.tellg();
  f.seekg(0, std::ios_base::beg);
  CHECK_EQ(size % sizeof(data[0]), 0);
  data.resize(size / sizeof(data[0]));
  f.read(reinterpret_cast<char *>(data.data()), size);
  CHECK(f);
  f.close();
}

void ReadFromLocalFile(std::string_view file_path, std::string &data) {
  return ReadFromLocalFile<std::string>(file_path, data);
}
void ReadFromLocalFile(std::string_view file_path, ByteArray &data) {
  return ReadFromLocalFile<ByteArray>(file_path, data);
}
void ReadFromLocalFile(std::string_view file_path, FeatureVec &data) {
  return ReadFromLocalFile<FeatureVec>(file_path, data);
}
void ReadFromLocalFile(std::string_view file_path,
                       std::vector<uint32_t> &data) {
  return ReadFromLocalFile<std::vector<uint32_t> &>(file_path, data);
}

template <typename Container>
void WriteToLocalFile(std::string_view file_path, const Container &data) {
  std::ofstream f(std::string{file_path.data()});
  CHECK(f);
  f.write(reinterpret_cast<const char *>(data.data()),
          data.size() * sizeof(data[0]));
  CHECK(f);
  f.close();
}

void WriteToLocalFile(std::string_view file_path, const ByteArray &data) {
  WriteToLocalFile<ByteArray>(file_path, data);
}
void WriteToLocalFile(std::string_view file_path, std::string_view data) {
  WriteToLocalFile<std::string_view>(file_path, data);
}
void WriteToLocalFile(std::string_view file_path, const FeatureVec &data) {
  WriteToLocalFile<FeatureVec>(file_path, data);
}

void WriteToLocalHashedFileInDir(std::string_view dir_path,
                                 const ByteArray &data) {
  if (dir_path.empty()) return;
  std::string file_path = std::filesystem::path(dir_path).append(Hash(data));
  WriteToLocalFile(file_path, data);
}

std::string HashOfFileContents(std::string_view file_path) {
  ByteArray ba;
  ReadFromLocalFile(file_path, ba);
  return Hash(ba);
}

int64_t MemoryUsage() {
  // TODO(b/233909173): [impl]
  return 0;
}

std::string ProcessAndThreadUniqueID(std::string_view prefix) {
  // operator << is the only way to serialize std::this_thread::get_id().
  std::ostringstream oss;
  oss << prefix << getpid() << "-" << std::this_thread::get_id();
  return oss.str();
}

std::string TemporaryLocalDirPath() {
  const char *TMPDIR = getenv("TMPDIR");
  std::string tmp = TMPDIR ? TMPDIR : "/tmp";
  return std::filesystem::path(tmp).append(
      ProcessAndThreadUniqueID("centipede-"));
}

// We need to maintain a global set of dirs that CreateLocalDirRemovedAtExit()
// was called with, so that we can remove all these dirs at exit.
ABSL_CONST_INIT static absl::Mutex dirs_to_delete_at_exit_mutex{
    absl::kConstInit};
static std::vector<std::string> *dirs_to_delete_at_exit
    ABSL_GUARDED_BY(dirs_to_delete_at_exit_mutex);

// Atexit handler added by CreateLocalDirRemovedAtExit().
// Deletes all dirs in dirs_to_delete_at_exit.
static void RemoveDirsAtExit() {
  absl::MutexLock lock(&dirs_to_delete_at_exit_mutex);
  for (auto &dir : *dirs_to_delete_at_exit) {
    std::filesystem::remove_all(dir);
  }
}

void CreateLocalDirRemovedAtExit(std::string_view path) {
  // Safe-guard against removing dirs not created by TemporaryLocalDirPath().
  CHECK_NE(path.find("/centipede-"), std::string::npos);
  // Create the dir.
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  // Add to dirs_to_delete_at_exit.
  absl::MutexLock lock(&dirs_to_delete_at_exit_mutex);
  if (!dirs_to_delete_at_exit) {
    dirs_to_delete_at_exit = new std::vector<std::string>();
    atexit(&RemoveDirsAtExit);
  }
  dirs_to_delete_at_exit->emplace_back(path);
}

// Honor $TEST_TMPDIR first, then $TMPDIR, then fall back to /tmp.
std::string GetTestTempDir() {
  if (auto path = getenv("TEST_TMPDIR")) return path;
  if (auto path = getenv("TMPDIR")) return path;
  return "/tmp";
}

static const size_t kMagicLen = 11;
static const uint8_t kPackBegMagic[] = "-Centipede-";
static const uint8_t kPackEndMagic[] = "-edepitneC-";
static_assert(sizeof(kPackBegMagic) == kMagicLen + 1);
static_assert(sizeof(kPackEndMagic) == kMagicLen + 1);

// Pack 'data' such that it can be appended to a file and later extracted:
//   * kPackBegMagic
//   * hash(data)
//   * data.size() (8 bytes)
//   * data itself
//   * kPackEndMagic
// Storing the magics and the hash is a precaution against partial writes.
// UnpackBytesFromAppendFile looks for the kPackBegMagic and so
// it will ignore any partially-written data.
//
// This is simple and efficient, but I wonder if there is a ready-to-use
// standard open-source alternative. Or should we just use tar?
ByteArray PackBytesForAppendFile(const ByteArray &data) {
  ByteArray res;
  auto hash = Hash(data);
  CHECK_EQ(hash.size(), kHashLen);
  size_t size = data.size();
  uint8_t size_bytes[sizeof(size)];
  memcpy(size_bytes, &size, sizeof(size));
  res.insert(res.end(), &kPackBegMagic[0], &kPackBegMagic[kMagicLen]);
  res.insert(res.end(), hash.begin(), hash.end());
  res.insert(res.end(), &size_bytes[0], &size_bytes[sizeof(size_bytes)]);
  res.insert(res.end(), data.begin(), data.end());
  res.insert(res.end(), &kPackEndMagic[0], &kPackEndMagic[kMagicLen]);
  return res;
}

// Reverse to a sequence of PackBytesForAppendFile() appended to each other.
void UnpackBytesFromAppendFile(const ByteArray &packed_data,
                               std::vector<ByteArray> *unpacked,
                               std::vector<std::string> *hashes) {
  ByteArray::const_iterator pos = packed_data.begin();
  while (true) {
    pos = std::search(pos, packed_data.end(), &kPackBegMagic[0],
                      &kPackBegMagic[kMagicLen]);
    if (pos == packed_data.end()) return;
    pos += kMagicLen;
    if (packed_data.end() - pos < kHashLen) return;
    std::string hash(pos, pos + kHashLen);
    pos += kHashLen;
    size_t size;
    if (packed_data.end() - pos < sizeof(size)) return;
    memcpy(&size, &*pos, sizeof(size));
    pos += sizeof(size);
    if (packed_data.end() - pos < size) return;
    ByteArray ba(pos, pos + size);
    pos += size;
    if (packed_data.end() - pos < kMagicLen) return;
    if (memcmp(&*pos, kPackEndMagic, kMagicLen)) continue;
    pos += kMagicLen;
    if (hash != Hash(ba)) continue;
    if (unpacked) unpacked->push_back(std::move(ba));
    if (hashes) hashes->push_back(std::move(hash));
  }
}

void AppendHashToArray(ByteArray &ba, std::string_view hash) {
  CHECK_EQ(hash.size(), kHashLen);
  ba.insert(ba.end(), hash.begin(), hash.end());
}

std::string ExtractHashFromArray(ByteArray &ba) {
  CHECK_GE(ba.size(), kHashLen);
  std::string res;
  res.insert(res.end(), ba.end() - kHashLen, ba.end());
  ba.resize(ba.size() - kHashLen);
  return res;
}

ByteArray PackFeaturesAndHash(const ByteArray &data,
                              const FeatureVec &features) {
  size_t features_len_in_bytes = features.size() * sizeof(feature_t);
  ByteArray feature_bytes_with_hash(features_len_in_bytes + kHashLen);
  memcpy(feature_bytes_with_hash.data(), features.data(),
         features_len_in_bytes);
  auto hash = Hash(data);
  CHECK_EQ(hash.size(), kHashLen);
  memcpy(feature_bytes_with_hash.data() + features_len_in_bytes, hash.data(),
         kHashLen);
  return feature_bytes_with_hash;
}

void ExtractCorpusRecords(const ByteArray &corpus_bytes,
                          const ByteArray &features_bytes,
                          std::vector<CorpusRecord> &result) {
  std::vector<ByteArray> corpus;
  std::vector<ByteArray> features_with_hashes;
  UnpackBytesFromAppendFile(corpus_bytes, &corpus);
  UnpackBytesFromAppendFile(features_bytes, &features_with_hashes);
  absl::flat_hash_map<std::string, FeatureVec> hash_to_features;
  for (auto &fh : features_with_hashes) {
    auto hash = ExtractHashFromArray(fh);
    FeatureVec features(fh.size() / sizeof(feature_t));
    memcpy(features.data(), fh.data(), features.size() * sizeof(feature_t));
    hash_to_features[hash] = features;
  }
  for (auto &input : corpus) {
    auto &features = hash_to_features[Hash(input)];
    result.push_back({input, features});
  }
}

static std::atomic<int> requested_exit_code(EXIT_SUCCESS);

void RequestEarlyExit(int exit_code) {
  CHECK_NE(exit_code, EXIT_SUCCESS);
  requested_exit_code = exit_code;
}
bool EarlyExitRequested() { return requested_exit_code != EXIT_SUCCESS; }
int ExitCode() { return requested_exit_code; }

}  // namespace centipede
