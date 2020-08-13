// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Wrapper for File's access to the OS.
// TODO(wad) coalesce all the OS wrappers into one common OSEnv class.
#ifndef VERITY_SIMPLE_FILE_ENV_H__
#define VERITY_SIMPLE_FILE_ENV_H__ 1

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "verity/logging.h"

namespace simple_file {

class Env {
 public:
  Env();
  virtual ~Env() {}
  // Wraps open(2). Use umask(2) (Env::Umask) to set the mode for file creation.
  virtual int Open(const char* pathname, int flags) const {
    return open(pathname, flags);
  }
  virtual int Create(const char* pathname, int flags, mode_t mode) const {
    return open(pathname, flags, mode);
  }
  virtual mode_t Umask(mode_t mask) const { return umask(mask); }
  virtual int Close(int fd) const { return close(fd); }
  virtual int Fstat(int fd, struct stat* buf) const { return fstat(fd, buf); }
  virtual off_t Lseek(int fd, off_t offset, int whence) const {
    return lseek(fd, offset, whence);
  }
  virtual ssize_t Read(int fd, void* buf, size_t count) const {
    return read(fd, buf, count);
  }
  virtual ssize_t Pread(int fd, void* buf, size_t count, off_t offset) const {
    return pread(fd, buf, count, offset);
  }
  virtual ssize_t Pwrite(int fd,
                         const void* buf,
                         size_t count,
                         off_t offset) const {
    return pwrite(fd, buf, count, offset);
  }
  virtual ssize_t Write(int fd, const void* buf, size_t count) const {
    return write(fd, buf, count);
  }

  // Wrap less defined behavior.
  virtual int BlockDevSize(int fd, int64_t* size) const {
    return ioctl(fd, BLKGETSIZE64, size);
  }
};

}  // namespace simple_file
#endif  // VERITY_SIMPLE_FILE_ENV_H__
