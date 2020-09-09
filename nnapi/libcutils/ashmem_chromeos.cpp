// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cutils/ashmem.h>

#include <android-base/logging.h>

#include <algorithm>
#include <string>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

// Implementation of the ashmem interface using POSIX shared memory buffers

extern "C" {

int ashmem_valid(int fd) {
  return fcntl(fd, F_GETFD) != -1 || errno != EBADF;
}

int ashmem_create_region(const char* /*name*/, size_t size) {
  char _tmpname[L_tmpnam];
  if (tmpnam_r(_tmpname) == nullptr) {
    return -1;
  }

  // tmpnam will produce a string containing with slashes, but shm_open
  // won't like that.
  std::string _name = std::string(_tmpname);
  std::replace(_name.begin(), _name.end(), '/', '-');

  int fd =
      shm_open(_name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
  if (fd == -1) {
    return -1;
  }

  // This will clean up the /dev/shm area but the fd will
  // live until it is closed.
  shm_unlink(_name.c_str());

  // Set the size of the buffer as requested
  if (ftruncate(fd, size) == -1) {
    close(fd);
    return -1;
  }

  return fd;
}

int ashmem_set_prot_region(int /*fd*/, int prot) {
  // This is difficult to do with file descriptors, and since we're only
  // implementing for libfmq it's not needed, since the only call sets
  // (PROT_READ | PROT_WRITE), which is effectively the default behaviour
  // of the POSIX shared memory system.
  if ((prot & PROT_READ) && (prot & PROT_WRITE)) {
    return 0;
  }
  LOG(FATAL) << "ashmem only supports (PROT_READ | PROT_WRITE) protection";
  return -1;
}

int ashmem_pin_region(int /*fd*/, size_t /*offset*/, size_t /*len*/) {
  // This isn't tied into any kernel memory management, so pinning
  // doesn't need support. Telling the caller their memory is safe
  // is fine.
  return 0 /*ASHMEM_NOT_PURGED*/;
}

int ashmem_unpin_region(int /*fd*/, size_t /*offset*/, size_t /*len*/) {
  // This isn't tied into any kernel memory management, so pinning
  // doesn't need support. Telling the caller that they can unpin
  // their memory is fine, even if it doesn't ever get freed.
  return 0 /*ASHMEM_IS_UNPINNED*/;
}

int ashmem_get_size_region(int fd) {
  struct stat s;
  if (fstat(fd, &s) == -1) {
    return -1;
  }
  return (s.st_size);
}
}
