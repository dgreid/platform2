// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBFS_INODE_MAP_H_
#define SMBFS_INODE_MAP_H_

#include <stdint.h>
#include <sys/types.h>

#include <memory>
#include <string>
#include <unordered_map>

#include <base/files/file_path.h>
#include <base/macros.h>

namespace smbfs {

// Class that synthesizes inode numbers for file paths, and keeps a reference
// count for each inode. Inode numbers are never re-used by new paths.
class InodeMap {
 public:
  explicit InodeMap(ino_t root_inode);
  ~InodeMap();

  // Increment the inode refcount for |path| by 1 and return the inode number.
  // If |path| does not have a corresponding inode, create a new one with a
  // refcount of 1. |path| must be an absolute path, and not contain any
  // relative components (i.e. '.' and '..').
  ino_t IncInodeRef(const base::FilePath& path);

  // Return the path corresponding to the file |inode|. If the inode does not
  // exist, return the empty path.
  base::FilePath GetPath(ino_t inode) const;

  // Forget |forget_count| reference to |inode|. If the refcount falls to 0,
  // remove the inode. |forget_count| cannot be greater than the current
  // refcount of |inode|. Returns true if the inode was removed.
  bool Forget(ino_t inode, uint64_t forget_count);

 private:
  struct Entry;

  const ino_t root_inode_;
  ino_t seq_num_;
  std::unordered_map<ino_t, std::unique_ptr<Entry>> inodes_;
  std::unordered_map<std::string, Entry*> files_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(InodeMap);
};

}  // namespace smbfs

#endif  // SMBFS_INODE_MAP_H_
