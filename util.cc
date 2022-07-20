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

#include "./util.h"

#include <unistd.h>

#include <cctype>
#include <charconv>
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
#include "absl/strings/str_split.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/synchronization/mutex.h"
#include "absl/types/span.h"
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
      out << "\\x" << std::uppercase << std::hex << static_cast<uint32_t>(ch);
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
  CHECK(f) << "Failed to read from local file: " << file_path;
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

void WriteToLocalFile(std::string_view file_path,
                      absl::Span<const uint8_t> data) {
  std::ofstream f(std::string{file_path.data()});
  CHECK(f) << "Failed to open local file: " << file_path;
  f.write(reinterpret_cast<const char *>(data.data()), data.size());
  CHECK(f) << "Failed to write to local file: " << file_path;
  f.close();
}

void WriteToLocalFile(std::string_view file_path, std::string_view data) {
  static_assert(sizeof(decltype(data)::value_type) == sizeof(uint8_t));
  WriteToLocalFile(file_path, absl::Span<const uint8_t>(
      reinterpret_cast<const uint8_t *>(data.data()), data.size()));
}

void WriteToLocalFile(std::string_view file_path, const FeatureVec &data) {
  WriteToLocalFile(
      file_path,
      absl::Span<const uint8_t>(reinterpret_cast<const uint8_t *>(data.data()),
                                sizeof(data[0]) * data.size()));
}

void WriteToLocalHashedFileInDir(std::string_view dir_path,
                                 absl::Span<const uint8_t> data) {
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
  // Read VmRSS from statm. Not the most accurate, but (probably?) good enough.
  std::ifstream f(std::string{"/proc/self/statm"});
  size_t value;
  f >> value;  // skip the first value.
  f >> value;
  return value * getpagesize();  // value is in pages.
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

void ExtractCorpusRecords(const std::vector<ByteArray> &corpus_blobs,
                          const std::vector<ByteArray> &features_blobs,
                          std::vector<CorpusRecord> &result) {
  absl::flat_hash_map<std::string, FeatureVec> hash_to_features;
  for (const auto &hash_and_features : features_blobs) {
    CHECK_GE(hash_and_features.size(), kHashLen);
    std::string hash;
    hash.insert(hash.end(), hash_and_features.end() - kHashLen,
                hash_and_features.end());
    size_t num_feature_bytes = hash_and_features.size() - kHashLen;
    if (num_feature_bytes == 0) {
      hash_to_features[hash] = {FeatureDomains::kNoFeature};
      continue;
    }
    FeatureVec features(num_feature_bytes / sizeof(feature_t));
    memcpy(features.data(), hash_and_features.data(),
           features.size() * sizeof(feature_t));
    hash_to_features[hash] = features;
  }
  for (const auto &input : corpus_blobs) {
    auto &features = hash_to_features[Hash(input)];
    result.push_back({input, features});
  }
}

// Returns a vector of string pairs that are used to replace special characters
// and hex values in ParseAFLDictionary.
static std::vector<std::pair<std::string, std::string>>
AFLDictionaryStringReplacements() {
  std::vector<std::pair<std::string, std::string>> replacements;
  replacements.push_back({"\\\\", "\\"});
  replacements.push_back({"\\r", "\r"});
  replacements.push_back({"\\n", "\n"});
  replacements.push_back({"\\t", "\t"});
  replacements.push_back({"\\\"", "\""});
  // Hex string replacements, lower and upper case.
  for (int i = 0; i < 256; i++) {
    replacements.push_back({absl::StrFormat("\\x%02x", i), std::string(1, i)});
    replacements.push_back({absl::StrFormat("\\x%02X", i), std::string(1, i)});
  }
  return replacements;
}

bool ParseAFLDictionary(std::string_view dictionary_text,
                        std::vector<ByteArray> &dictionary_entries) {
  auto replacements = AFLDictionaryStringReplacements();
  dictionary_entries.clear();
  // Check if the contents is ASCII.
  for (char ch : dictionary_text) {
    if (!std::isprint(ch) && !std::isspace(ch)) return false;
  }
  // Iterate over all lines.
  for (auto line : absl::StrSplit(dictionary_text, '\n')) {
    // [start, stop) are the offsets of the dictionary entry.
    size_t start = 0;
    // Skip leading spaces.
    while (start < line.size() && isspace(line[start])) ++start;
    // Skip empty line.
    if (start == line.size()) continue;
    // Skip comment line.
    if (line[start] == '#') continue;
    // Find the first "
    while (start < line.size() && line[start] != '"') ++start;
    if (start == line.size()) return false;  // no opening "
    ++start;                                 // skip the first "
    size_t stop = line.size() - 1;
    // Find the last "
    while (stop > start && line[stop] != '"') --stop;
    if (stop == start) return false;  // no closing "
    // Replace special characters and hex values.
    std::string replaced = absl::StrReplaceAll(
        std::string_view(line.begin() + start, stop - start), replacements);
    dictionary_entries.emplace_back(replaced.begin(), replaced.end());
  }
  return true;
}

static std::atomic<int> requested_exit_code(EXIT_SUCCESS);

void RequestEarlyExit(int exit_code) {
  CHECK_NE(exit_code, EXIT_SUCCESS);
  requested_exit_code = exit_code;
}
bool EarlyExitRequested() { return requested_exit_code != EXIT_SUCCESS; }
int ExitCode() { return requested_exit_code; }

}  // namespace centipede
