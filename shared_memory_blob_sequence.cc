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

#include "./shared_memory_blob_sequence.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>

#include "absl/log/check.h"

namespace centipede {

SharedMemoryBlobSequence::SharedMemoryBlobSequence(const char *name,
                                                   size_t size)
    : size_(size) {
  PCHECK(size >= sizeof(Blob::size)) << "Size too small";
  fd_ = shm_open(name, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
  name_to_unlink_ = strdup(name);  // Using raw C strings to avoid dependencies.
  PCHECK(!(fd_ < 0)) << "shm_open() failed";
  PCHECK(!(ftruncate(fd_, size_))) << "ftruncate() failed)";
  MmapData();
}

SharedMemoryBlobSequence::SharedMemoryBlobSequence(const char *name) {
  fd_ = shm_open(name, O_RDWR, 0);
  PCHECK(fd_ >=  0) << "shm_open() failed";
  struct stat statbuf;
  PCHECK(!(fstat(fd_, &statbuf))) << "fstat() failed";
  size_ = statbuf.st_size;
  MmapData();
}

void SharedMemoryBlobSequence::MmapData() {
  data_ =
      (uint8_t *)mmap(NULL, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
  PCHECK(!(data_ == MAP_FAILED)) << "mmap() failed";
}

SharedMemoryBlobSequence::~SharedMemoryBlobSequence() {
  PCHECK(munmap(data_, size_) == 0) << "munmap() failed";
  if (name_to_unlink_) {
    PCHECK(shm_unlink(name_to_unlink_) >= 0) << "shm_unlink() failed";
    free(name_to_unlink_);
  }
  PCHECK(close(fd_) == 0) << "close() failed";
}

void SharedMemoryBlobSequence::Reset() {
  offset_ = 0;
  had_reads_after_reset_ = false;
  had_writes_after_reset_ = false;
}

bool SharedMemoryBlobSequence::Write(Blob blob) {
  PCHECK(blob.IsValid()) << "blob.tag must not be zero";
  PCHECK(!had_reads_after_reset_) << "Had reads after reset";
  had_writes_after_reset_ = true;
  if (offset_ + sizeof(blob.size) + sizeof(blob.tag) + blob.size > size_)
    return false;
  // Write tag.
  memcpy(data_ + offset_, &blob.tag, sizeof(blob.tag));
  offset_ += sizeof(blob.tag);

  // Write size.
  memcpy(data_ + offset_, &blob.size, sizeof(blob.size));
  offset_ += sizeof(blob.size);
  // Write data.
  memcpy(data_ + offset_, blob.data, blob.size);
  offset_ += blob.size;
  if (offset_ + sizeof(blob.size) + sizeof(blob.tag) <= size_) {
    // Write zero tag/size to data_+offset_ but don't change the offset.
    // This is required to overwrite any stale bits in data_.
    Blob invalid_blob;  // invalid.
    memcpy(data_ + offset_, &invalid_blob.tag, sizeof(invalid_blob.tag));
    memcpy(data_ + offset_ + sizeof(invalid_blob.tag), &invalid_blob.size,
           sizeof(invalid_blob.size));
  }
  return true;
}

SharedMemoryBlobSequence::Blob SharedMemoryBlobSequence::Read() {
  PCHECK(!had_writes_after_reset_) << "Had writes after reset";
  had_reads_after_reset_ = true;
  if (offset_ + sizeof(Blob::size) + sizeof(Blob::tag) >= size_) return {};
  // Read blob_tag.
  Blob::size_and_tag_type blob_tag = 0;
  memcpy(&blob_tag, data_ + offset_, sizeof(blob_tag));
  offset_ += sizeof(blob_tag);
  // Read blob_size.
  Blob::size_and_tag_type blob_size = 0;
  memcpy(&blob_size, data_ + offset_, sizeof(Blob::size));
  offset_ += sizeof(Blob::size);
  // Read blob_data.
  PCHECK(offset_ + blob_size <= size_) << "Not enough bytes";
  if (blob_tag == 0 && blob_size == 0) return {};
  PCHECK(blob_tag != 0) << "blob.tag must not be zero";
  Blob result{blob_tag, blob_size, data_ + offset_};
  offset_ += result.size;
  return result;
}

}  // namespace centipede
