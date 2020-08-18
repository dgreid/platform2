// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Implementation of simple_file::File
#include "verity/simple_file/file.h"

namespace simple_file {

const Env* File::env() const {
  if (env_) {
    return env_;
  }
  return default_env_;
}

bool File::Initialize(const char* path, int flags, const Env* new_env) {
  if (fd_ >= 0) {
    LOG(ERROR) << "Attempted to Initialize while in use";
    return false;
  }

  if (!new_env) {
    DLOG(INFO) << "Using the default Env";
  } else {
    env_ = new_env;
  }

  if (flags & O_CREAT) {
    // TODO(wad) less hacky
    fd_ = env()->Create(path, flags, S_IRUSR | S_IWUSR);
  } else {
    fd_ = env()->Open(path, flags);
  }
  if (fd_ < 0) {
    PLOG(ERROR) << "Failed to open the specified file";
    return false;
  }
  return true;
}

void File::Reset() {
  if (fd_ >= 0) {
    env()->Close(fd_);
    fd_ = -1;
  }
  env_ = NULL;
  offset_ = 0;
  delete default_env_;
  default_env_ = new Env;
}

bool File::Seek(off_t location, bool absolute) {
  int64_t size = Size();
  if (absolute) {
    if (location > size || location < 0)
      return false;
    offset_ = location;
    return true;
  }
  off_t result = location + offset_;
  if (result < 0 || result > size) {
    return false;
  }
  offset_ = result;
  return true;
}

off_t File::Whence() const {
  return offset_;
}

bool File::Read(int bytes, uint8_t* buf) {
  if (!ReadAt(bytes, buf, offset_)) {
    return false;
  }
  offset_ += bytes;
  return true;
}

bool File::ReadAt(int bytes, uint8_t* buf, off_t offset) {
  if (fd_ < 0) {
    LOG(ERROR) << "Read called with an invalid fd";
    return false;
  }
  if (bytes <= 0) {
    LOG(ERROR) << "Negative or 0 sized read requested";
    return false;
  }
  ssize_t in_bytes = env()->Pread(fd_, buf, bytes, offset);
  if (in_bytes == bytes) {
    return true;
  }
  if (in_bytes < 0) {
    // TODO(wad) Roll PLOG into the Env so that errno logging is encapsulated.
    PLOG(ERROR) << "An error occurred reading from the file";
    return false;
  }
  DLOG(INFO) << "Failed to read the total number of bytes requested.";
  return false;
}

int64_t File::Size() const {
  if (fd_ < 0) {
    LOG(ERROR) << "Size called with an invalid fd";
    return false;
  }
  struct stat buf;
  if (env()->Fstat(fd_, &buf)) {
    PLOG(ERROR) << "Failed to fstat() the file";
    return -1;
  }
  if (!S_ISBLK(buf.st_mode)) {
    return static_cast<int64_t>(buf.st_size);
  }
  // Handle use on block devices.
  int64_t size = -1;
  if (env()->BlockDevSize(fd_, &size)) {
    PLOG(ERROR) << "Failed to get the block device size";
    return -1;
  }
  return size;
}

bool File::WriteAt(int bytes, const uint8_t* buf, off_t offset) {
  if (fd_ < 0) {
    LOG(ERROR) << "Write called with an invalid fd";
    return false;
  }
  if (bytes <= 0) {
    LOG(ERROR) << "Negative or 0 sized write requested";
    return false;
  }
  ssize_t out_bytes = env()->Pwrite(fd_, buf, bytes, offset);
  if (out_bytes == bytes) {
    return true;
  }
  if (out_bytes < 0) {
    PLOG(ERROR) << "An error occurred writing to the file at " << offset;
    return false;
  }
  DLOG(INFO) << "Failed to write the total number of bytes requested.";
  return false;
}

bool File::Write(int bytes, const uint8_t* buf) {
  if (!WriteAt(bytes, buf, offset_)) {
    return false;
  }
  offset_ += bytes;
  return true;
}

}  // namespace simple_file
