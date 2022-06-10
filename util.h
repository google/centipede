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

#ifndef THIRD_PARTY_CENTIPEDE_UTIL_H_
#define THIRD_PARTY_CENTIPEDE_UTIL_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "./defs.h"
#include "./feature.h"

namespace centipede {

// Input data and features that correspond to that input.
struct CorpusRecord {
  ByteArray data;
  FeatureVec features;
};

// Returns a printable hash of a byte array. Currently sha1 is used.
std::string Hash(const ByteArray &ba);
// Returns a printable hash of a string. Currently sha1 is used.
std::string Hash(std::string_view str);
// Hashes are always this many bytes.
inline constexpr size_t kHashLen = 40;
// Returns the hash of the contents of the file `file_path`.
std::string HashOfFileContents(std::string_view file_path);
// Returns a printable string representing at most `max_len` bytes of `data`.
std::string AsString(const ByteArray &data, size_t max_len = 16);
// Reads from a local file `file_path` into `data`.
// Crashes on any error.
void ReadFromLocalFile(std::string_view file_path, ByteArray &data);
// Same as above.
void ReadFromLocalFile(std::string_view file_path, std::string &data);
// Same as above but for FeatureVec.
// Crashes if the number of read bytes is not 0 mod sizeof(feature_t).
void ReadFromLocalFile(std::string_view file_path, FeatureVec &data);
// Same as above but for vector<uint32_t>.
void ReadFromLocalFile(std::string_view file_path, std::vector<uint32_t> &data);
// Writes the contents of `data` to a local file `file_path`.
// Crashes on any error.
void WriteToLocalFile(std::string_view file_path, const ByteArray &data);
// Same as above.
void WriteToLocalFile(std::string_view file_path, std::string_view data);
// Same as above but for FeatureVec.
void WriteToLocalFile(std::string_view file_path, const FeatureVec &data);
// Writes `data` to `dir_path`/Hash(`data`). Does nothing if `dir_path.empty()`.
void WriteToLocalHashedFileInDir(std::string_view dir_path,
                                 const ByteArray &data);
// Returns the current process's memory usage in bytes or -1 on error.
int64_t MemoryUsage();
// Returns a path string suitable to create a temporary local directory.
// Will return the same value every time it is called within one thread,
// but different values for different threads and difference processes.
std::string TemporaryLocalDirPath();

// Creates an empty dir `path` and schedules it for deletion at exit.
// Uses atexit(), and so the removal will not happen if exit() is bypassed.
// This function is supposed to be called only on paths created by
// TemporaryLocalDirPath().
void CreateLocalDirRemovedAtExit(std::string_view path);

// Requests that the process exits soon, with `exit_code`.
// `exit_code` must be non-zero (!= EXIT_SUCCESS).
// Async-signal-safe.
void RequestEarlyExit(int exit_code);
// Returns true iff RequestEarlyExit() was called.
bool EarlyExitRequested();
// Returns the value most recently passed to RequestEarlyExit()
// or 0 if RequestEarlyExit() was not called.
int ExitCode();

// If `seed` != 0, returns `seed`, otherwise returns a random number
// based on time, pid, tid, etc.
size_t GetRandomSeed(size_t seed);

// Returns a string that starts with `prefix` and that uniquely identifies
// the caller's process and thread.
std::string ProcessAndThreadUniqueID(std::string_view prefix);

// Test-only. Returns a temp dir for use inside tests.
std::string GetTestTempDir();

// Adds a prefix and a postfix to `data` such that the result can be
// appended to another such packed data and then the operation can be reversed.
// The purpose is to allow appending blobs of data to a (possibly remote) file
// such that when reading this file we can separate the blobs.
// TODO(kcc): [impl] is there a lightweight equivalent in the open-source world?
//  tar sounds too heavy.
// TODO(kcc): [impl] investigate https://github.com/google/riegeli.
ByteArray PackBytesForAppendFile(const ByteArray &data);
// Unpacks `packed_data` into `unpacked` and `hashes`.
// `packed_data` is multiple data packed by PackBytesForAppendFile()
// and merged together.
// `unpacked` or `hashes` can be nullptr.
void UnpackBytesFromAppendFile(const ByteArray &packed_data,
                               std::vector<ByteArray> *unpacked,
                               std::vector<std::string> *hashes = nullptr);
// Append the bytes from 'hash' to 'ba'.
void AppendHashToArray(ByteArray &ba, std::string_view hash);
// Reverse to AppendHashToArray.
std::string ExtractHashFromArray(ByteArray &ba);

// Pack {features, Hash(data)} into a byte array.
ByteArray PackFeaturesAndHash(const ByteArray &data,
                              const FeatureVec &features);

// `corpus_blobs` is a vector of inputs.
// `features_blobs` is a sequence of features/hash pairs,
// each created by PackFeaturesAndHash.
// This function unpacks these into a vector of CorpusRecords.
void ExtractCorpusRecords(const std::vector<ByteArray> &corpus_blobs,
                          const std::vector<ByteArray> &features_blobs,
                          std::vector<CorpusRecord> &result);

// Parses `dictionary_text` representing an AFL/libFuzzer dictionary.
// https://github.com/google/AFL/blob/master/dictionaries/README.dictionaries
// https://llvm.org/docs/LibFuzzer.html#dictionaries
// Fills in `dictionary_entries` with byte sequences from the dictionary.
// Returns true iff parsing completes succesfully.
bool ParseAFLDictionary(std::string_view dictionary_text,
                        std::vector<ByteArray> &dictionary_entries);

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_UTIL_H_
