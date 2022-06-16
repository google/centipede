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

#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>  // NOLINT(build/c++11)
#include <vector>

#include "testing/base/public/gunit.h"
#include "./defs.h"
#include "./logging.h"

namespace centipede {

static void Append(ByteArray &to, const ByteArray &from) {
  to.insert(to.end(), from.begin(), from.end());
}

TEST(AppendFile, t1) {
  ByteArray packed;
  ByteArray a{1, 2, 3};
  ByteArray b{3, 4, 5};
  ByteArray c{111, 112, 113, 114, 115};
  Append(packed, PackBytesForAppendFile(a));
  Append(packed, PackBytesForAppendFile(b));
  Append(packed, PackBytesForAppendFile(c));
  std::vector<ByteArray> unpacked;
  UnpackBytesFromAppendFile(packed, &unpacked);
  EXPECT_EQ(a, unpacked[0]);
  EXPECT_EQ(b, unpacked[1]);
  EXPECT_EQ(c, unpacked[2]);
}

TEST(Util, Hash) {
  // The current implementation of Hash() is sha1.
  // Here we test a couple of inputs against their known sha1 values
  // obtained from the sha1sum command line utility.
  EXPECT_EQ(Hash({'a', 'b', 'c'}), "a9993e364706816aba3e25717850c26c9cd0d89d");
  EXPECT_EQ(Hash({'x', 'y'}), "5f8459982f9f619f4b0d9af2542a2086e56a4bef");
}

TEST(Util, AsString) {
  EXPECT_EQ(AsString({'a', 'b', 'c'}, 3), "abc");
  EXPECT_EQ(AsString({'a', 'b', 'c'}, 4), "abc");
  EXPECT_EQ(AsString({'a', 'b', 'c'}, 2), "ab");
  EXPECT_EQ(AsString({'a', 0xab, 0xcd}, 3), "a\xAB\xCD");
  EXPECT_EQ(AsString({'a', 0xab, 0xcd}, 4), "a\xAB\xCD");
  EXPECT_EQ(AsString({'a', 0xab, 0xcd}, 2), "a\xAB");
  EXPECT_EQ(AsString({'a', 0xab, 0xcd, 'z'}, 5), "a\xAB\xCDz");
}

TEST(Centipede, ExtractHashFromArray) {
  const ByteArray a{1, 2, 3, 4};
  const ByteArray b{100, 111, 122, 133, 145};
  auto hash1 = Hash({4, 5, 6});
  auto hash2 = Hash({7, 8});

  ByteArray a1 = a;
  AppendHashToArray(a1, hash1);
  EXPECT_EQ(a1.size(), a.size() + hash1.size());

  ByteArray b2 = b;
  AppendHashToArray(b2, hash2);
  EXPECT_EQ(b2.size(), b.size() + hash2.size());

  EXPECT_EQ(ExtractHashFromArray(b2), hash2);
  EXPECT_EQ(b2, b);

  EXPECT_EQ(ExtractHashFromArray(a1), hash1);
  EXPECT_EQ(a1, a);
}

TEST(Centipede, ExtractCorpusRecords) {
  ByteArray data1 = {1, 2, 3};
  ByteArray data2 = {3, 4, 5, 6};
  ByteArray data3 = {7, 8, 9, 10, 11};
  FeatureVec fv1 = {100, 200, 300};
  FeatureVec fv2 = {300, 400, 500, 600};
  FeatureVec fv3 = {700, 800, 900, 1000, 1100};

  std::vector<ByteArray> corpus_blobs;
  corpus_blobs.push_back(data1);
  corpus_blobs.push_back(data2);
  corpus_blobs.push_back(data3);

  std::vector<ByteArray> features_blobs;
  features_blobs.push_back(PackFeaturesAndHash(data1, fv1));
  features_blobs.push_back(PackFeaturesAndHash(data2, fv2));
  features_blobs.push_back(PackFeaturesAndHash(data3, fv3));

  std::vector<CorpusRecord> res;
  ExtractCorpusRecords(corpus_blobs, features_blobs, res);
  EXPECT_EQ(res.size(), 3UL);
  EXPECT_EQ(res[0].data, data1);
  EXPECT_EQ(res[1].data, data2);
  EXPECT_EQ(res[2].data, data3);
  EXPECT_EQ(res[0].features, fv1);
  EXPECT_EQ(res[1].features, fv2);
  EXPECT_EQ(res[2].features, fv3);
}

// Tests that TemporaryLocalDirPath() returns a valid path for a temp dir.
static void Test_TemporaryLocalDirPath() {
  auto tmpdir = TemporaryLocalDirPath();
  LOG(INFO) << tmpdir;
  EXPECT_EQ(tmpdir, TemporaryLocalDirPath());  // second call returns the same.
  // Create dir, create a file there, write to file, read from it, remove dir.
  std::filesystem::create_directories(tmpdir);
  std::string temp_file_path = std::filesystem::path(tmpdir).append("blah");
  ByteArray written_data{1, 2, 3};
  WriteToLocalFile(temp_file_path, written_data);
  ByteArray read_data;
  ReadFromLocalFile(temp_file_path, read_data);
  EXPECT_EQ(read_data, written_data);
  std::filesystem::remove_all(tmpdir);
  // temp_file_path should be gone by now.
  read_data.clear();
  ReadFromLocalFile(temp_file_path, read_data);
  EXPECT_TRUE(read_data.empty());
}

// Tests TemporaryLocalDirPath from several threads.
TEST(Centipede, TemporaryLocalDirPath) {
  Test_TemporaryLocalDirPath();

  std::string temp_dir_from_other_thread;
  std::thread get_temp_dir_thread(
      [&]() { temp_dir_from_other_thread = TemporaryLocalDirPath(); });
  get_temp_dir_thread.join();
  EXPECT_NE(TemporaryLocalDirPath(), temp_dir_from_other_thread);
}

TEST(Centipede, CreateLocalDirRemovedAtExit) {
  // We need to test that dirs created via CreateLocalDirRemovedAtExit
  // are removed at exit.
  // To do that, we run death tests and check if the dirs exist afterwards.
  // The path to directory is computed in the parent test, then it is
  // passed via an env. var. to the child test so that the child test doesn't
  // recompute it to be something different.
  const char *centipede_util_test_temp_dir =
      getenv("CENTIPEDE_UTIL_TEST_TEMP_DIR");
  auto tmpdir = centipede_util_test_temp_dir ? centipede_util_test_temp_dir
                                             : TemporaryLocalDirPath();
  EXPECT_FALSE(std::filesystem::exists(tmpdir));
  CreateLocalDirRemovedAtExit(tmpdir);
  EXPECT_TRUE(std::filesystem::exists(tmpdir));
  setenv("CENTIPEDE_UTIL_TEST_TEMP_DIR", tmpdir.c_str(), 1);
  // Create two subdirs via CreateLocalDirRemovedAtExit.
  std::string subdir1 = std::filesystem::path(tmpdir).append("1");
  std::string subdir2 = std::filesystem::path(tmpdir).append("2");
  CreateLocalDirRemovedAtExit(subdir1);
  CreateLocalDirRemovedAtExit(subdir2);
  EXPECT_TRUE(std::filesystem::exists(subdir1));
  EXPECT_TRUE(std::filesystem::exists(subdir2));

  // Run a subprocess that creates the same two subdirs and ends with abort.
  // Both subdirs should still be there.
  auto create_dir_and_abort = [&]() {
    CreateLocalDirRemovedAtExit(subdir1);
    CreateLocalDirRemovedAtExit(subdir2);
    abort();  // atexit handlers are not called.
  };
  EXPECT_DEATH(create_dir_and_abort(), "");
  EXPECT_TRUE(std::filesystem::exists(subdir1));
  EXPECT_TRUE(std::filesystem::exists(subdir2));

  // Run a subprocess that creates the same two subdirs and ends with exit.
  // Both subdirs should be gone.
  auto create_dir_and_exit1 = [&]() {
    CreateLocalDirRemovedAtExit(subdir1);
    CreateLocalDirRemovedAtExit(subdir2);
    exit(1);  // atexit handlers are called.
  };
  EXPECT_DEATH(create_dir_and_exit1(), "");
  EXPECT_FALSE(std::filesystem::exists(subdir1));
  EXPECT_FALSE(std::filesystem::exists(subdir2));
}

TEST(Centipede, ParseAFLDictionary) {
  std::vector<ByteArray> dict;
  EXPECT_TRUE(ParseAFLDictionary("", dict));  // Empty text.
  EXPECT_FALSE(ParseAFLDictionary("\xAB", dict));  // Non-ascii.
  EXPECT_FALSE(ParseAFLDictionary(" l1  \n\t\t\tl2  \n", dict));  // Missing "
  EXPECT_FALSE(ParseAFLDictionary(" \"zzz", dict));  // Missing second "

  // Two entries and a comment.
  EXPECT_TRUE(
      ParseAFLDictionary("  name=\"v1\"  \n"
                         " # comment\n"
                         " \"v2\"",
                         dict));
  EXPECT_EQ(dict, std::vector<ByteArray>({{'v', '1'}, {'v', '2'}}));

  // Hex entries and a properly escaped back slash.
  EXPECT_TRUE(ParseAFLDictionary("  \"\\xBC\\\\a\\xAB\\x00\"", dict));
  EXPECT_EQ(dict, std::vector<ByteArray>({{'\xBC', '\\', 'a', '\xAB', 0}}));

  // Special characters.
  EXPECT_TRUE(ParseAFLDictionary("\"\\r\\t\\n\\\"\"", dict));
  EXPECT_EQ(dict, std::vector<ByteArray>({{'\r', '\t', '\n', '"'}}));

  // Improper use of back slash, still parses.
  EXPECT_TRUE(ParseAFLDictionary("\"\\g\\h\"", dict));
  EXPECT_EQ(dict, std::vector<ByteArray>({{'\\', 'g', '\\', 'h'}}));
}

}  // namespace centipede