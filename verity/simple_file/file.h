// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Simple wrapper for synchronous file operations
// TODO(wad) clean up the API then propose moving it to a standalone location.
#ifndef VERITY_SIMPLE_FILE_FILE_H_
#define VERITY_SIMPLE_FILE_FILE_H_

#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "verity/logging.h"
#include "verity/simple_file/env.h"

namespace simple_file {

// File wraps normal file interactions to allow for easy mocking. In addition,
// the underlying OS calls can also be mocked using the Env class above.
// This class may not be used by multiple threads at once.
class File {
 public:
  File() : default_env_(new Env), env_(NULL), fd_(-1), offset_(0) {}
  virtual ~File() {
    if (fd_ >= 0) {
      env()->Close(fd_);
    }
    delete default_env_;
  }
  virtual const Env* env() const;

  // Specify the file and the open(2) flags for using it
  virtual bool Initialize(const char* path, int flags, const Env* env);
  // Read |bytes| into |buf|. |buf| may be altered even on failure.
  virtual bool Read(int bytes, uint8_t* buf);
  virtual bool ReadAt(int bytes, uint8_t* buf, off_t at);
  // Write |bytes| from |buf|
  virtual bool Write(int bytes, const uint8_t* buf);
  // WriteAt |bytes| from |buf|
  virtual bool WriteAt(int bytes, const uint8_t* buf, off_t at);

  // Size returns the total File size.
  virtual int64_t Size() const;
  virtual bool Seek(off_t location, bool absolute);
  virtual off_t Whence() const;
  // Reset returns the object to state immediately after construction.
  virtual void Reset();

 private:
  Env* default_env_;
  const Env* env_;
  int fd_;
  off_t offset_;
};

}  // namespace simple_file
#endif  // VERITY_SIMPLE_FILE_FILE_H_
