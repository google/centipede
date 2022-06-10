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

#include "./blob_file.h"

#include <cstdint>
#include <string_view>

#include "absl/status/status.h"
#include "absl/types/span.h"
#include "./defs.h"
#include "./logging.h"
#include "./remote_file.h"
#include "./util.h"

namespace centipede {

// Simple implementation of BlobFileReader/BlobFileAppender based on
// PackBytesForAppendFile() / UnpackBytesFromAppendFile().
// We expect to eventually replace this code with something more robust,
// and efficient, e.g. possibly https://github.com/google/riegeli.
// But the current implementation is fully functional.
class SimpleBlobFileReader : public BlobFileReader {
 public:
  ~SimpleBlobFileReader() {
    if (file_ && !closed_) CHECK_EQ(Close(), absl::OkStatus());
  }

  absl::Status Open(std::string_view path) override {
    if (closed_) return absl::FailedPreconditionError("already closed");
    if (file_) return absl::FailedPreconditionError("already open");
    file_ = RemoteFileOpen(path, "r");
    if (file_ == nullptr) return absl::UnknownError("can't open file");
    // Read the entire file at once.
    // It may be useful to read the file in chunks, but if we are going
    // to migrate to something else, it's not important here.
    ByteArray raw_bytes;
    RemoteFileRead(file_, raw_bytes);
    RemoteFileClose(file_);  // close the file here, we won't need it.
    UnpackBytesFromAppendFile(raw_bytes, &unpacked_blobs_);
    return absl::OkStatus();
  }

  virtual absl::Status Read(absl::Span<uint8_t> &blob) override {
    if (closed_) return absl::FailedPreconditionError("already closed");
    if (!file_) return absl::FailedPreconditionError("was not open");
    if (next_to_read_blob_index_ == unpacked_blobs_.size())
      return absl::OutOfRangeError("no more blobs");
    blob = absl::Span<uint8_t>(unpacked_blobs_[next_to_read_blob_index_]);
    ++next_to_read_blob_index_;
    return absl::OkStatus();
  }

  // Closes the file (it must be open).
  virtual absl::Status Close() override {
    if (closed_) return absl::FailedPreconditionError("already closed");
    if (!file_) return absl::FailedPreconditionError("was not open");
    closed_ = true;
    // Nothing to do here, we've already closed the file.
    return absl::OkStatus();
  }

 private:
  RemoteFile *file_ = nullptr;
  bool closed_ = false;
  std::vector<ByteArray> unpacked_blobs_;
  size_t next_to_read_blob_index_ = 0;
};

// See SimpleBlobFileReader.
class SimpleBlobFileAppender : public BlobFileAppender {
 public:
  ~SimpleBlobFileAppender() override {
    if (file_ && !closed_) CHECK_EQ(Close(), absl::OkStatus());
  }

  absl::Status Open(std::string_view path) override {
    if (closed_) return absl::FailedPreconditionError("already closed");
    if (file_) return absl::FailedPreconditionError("already open");
    file_ = RemoteFileOpen(path, "a");
    if (file_ == nullptr) return absl::UnknownError("can't open file");
    return absl::OkStatus();
  }

  absl::Status Append(absl::Span<const uint8_t> blob) override {
    if (closed_) return absl::FailedPreconditionError("already closed");
    if (!file_) return absl::FailedPreconditionError("was not open");
    ByteArray bytes(blob.size());
    // This copy from a span to vector is clumsy.
    // TODO(kcc): [as-needed] change RemoteFileAppend to accept a span.
    std::copy(blob.begin(), blob.end(), bytes.begin());
    ByteArray packed = PackBytesForAppendFile(bytes);
    RemoteFileAppend(file_, packed);

    return absl::OkStatus();
  }

  virtual absl::Status Close() override {
    if (closed_) return absl::FailedPreconditionError("already closed");
    if (!file_) return absl::FailedPreconditionError("was not open");
    closed_ = true;
    RemoteFileClose(file_);
    return absl::OkStatus();
  }

 private:
  RemoteFile *file_ = nullptr;
  bool closed_ = false;
};

std::unique_ptr<BlobFileReader> DefaultBlobFileReaderFactory() {
  return std::make_unique<SimpleBlobFileReader>();
}

std::unique_ptr<BlobFileAppender> DefaultBlobFileAppenderFactory() {
  return std::make_unique<SimpleBlobFileAppender>();
}

}  // namespace centipede
