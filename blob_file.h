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

#ifndef THIRD_PARTY_CENTIPEDE_BLOB_FILE_H_
#define THIRD_PARTY_CENTIPEDE_BLOB_FILE_H_

#include <cstdint>
#include <string_view>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "./defs.h"

namespace centipede {

// Blob is a sequence of bytes, a BlobFile is a sequence of blobs.
// BlobFileReader reads blobs from a BlobFile.
// BlobFileAppender appends blobs to a BlobFile.
// Only one active BlobFileAppender is allowed for a given file.
// Multiple BlobFileReader objects can open the same file, concurrently
// with up to one BlobFileAppender.
// BlobFileReader/BlobFileAppender should have some level of protection against
// failed appends (may vary depending on the implementation).
//
// BlobFileReader reads blobs from a file.
// All methods return OkStatus() on success and an approriate error status
// on failure.
//
// Different implementations of BlobFileReader/BlobFileAppender don't have to
// be file-format compatible.
class BlobFileReader {
 public:
  // Not copyable or movable.
  BlobFileReader(const BlobFileReader &) = delete;
  BlobFileReader &operator=(const BlobFileReader &) = delete;

  // Opens the file `path`.
  // Open() can be called only once.
  virtual absl::Status Open(std::string_view path) = 0;

  // Reads one `blob` from an open file.
  // The blob is valid until the next Read() or Close().
  // Returns absl::OutOfRangeError when there are no more blobs to read.
  virtual absl::Status Read(absl::Span<uint8_t> &blob) = 0;

  // Closes the file, which was previosly open and never closed.
  virtual absl::Status Close() = 0;

  // If the file was opened but not closed,
  // calls CHECK_EQ(Close(), absl::OkStatus()).
  virtual ~BlobFileReader() {}

 protected:
  BlobFileReader() {}
};

// Appends blobs to a BlobFile.
// See also comments for BlobFileReader.
class BlobFileAppender {
 public:
  // Not copyable or movable.
  BlobFileAppender(const BlobFileAppender &) = delete;
  BlobFileAppender &operator=(const BlobFileAppender &) = delete;

  // Opens the file `path`.
  // Open() can be called only once.
  virtual absl::Status Open(std::string_view path) = 0;

  // Appends one `blob` to an open file.
  // Returns OkStatus on success.
  virtual absl::Status Append(absl::Span<const uint8_t> blob) = 0;

  // Same as above, but for ByteArray.
  absl::Status Append(const ByteArray &bytes) {
    return Append(absl::Span<const uint8_t>{bytes});
  }

  // Closes the file, which was previosly open and never closed.
  virtual absl::Status Close() = 0;

  // If the file was opened but not closed,
  // calls CHECK_EQ(Close(), absl::OkStatus()).
  virtual ~BlobFileAppender() {}

 protected:
  BlobFileAppender() {}
};

// Creates a new object of a default implementation of BlobFileReader.
std::unique_ptr<BlobFileReader> DefaultBlobFileReaderFactory();

// Creates a new object of a default implementation of BlobFileAppender.
std::unique_ptr<BlobFileAppender> DefaultBlobFileAppenderFactory();

}  // namespace centipede

#endif  // THIRD_PARTY_CENTIPEDE_BLOB_FILE_H_
