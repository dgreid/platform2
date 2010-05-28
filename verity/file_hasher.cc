// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Implementation of FileHasher

#define __STDC_LIMIT_MACROS 1
#define __STDC_FORMAT_MACROS 1
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>

#include <asm/page.h>
#include <linux/device-mapper.h>
#include <linux/kernel.h>
#include "verity/file_hasher.h"
#include "verity/logging.h"

namespace verity {

// Simple helper for Initialize.
template<typename T>
static inline bool power_of_two(T num) {
  if (num == 0) return false;
  if (!(num & (num - 1))) return true;
  return false;
}

int FileHasher::WriteCallback(void *file,
                              sector_t start,
                              u8 *dst,
                              sector_t count,
                              struct dm_bht_entry *entry) {
  simple_file::File *f = reinterpret_cast<simple_file::File *>(file);
  int offset = static_cast<int>(to_bytes(start));
  LOG_IF(FATAL, (static_cast<sector_t>(offset) != to_bytes(start)))
    << "cast to bytes truncated value";
  if (!f->WriteAt(to_bytes(count), dst, offset)) {
    dm_bht_write_completed(entry, -EIO);
    return -1;
  }
  dm_bht_write_completed(entry, 0);
  return 0;
}
 
bool FileHasher::Initialize(simple_file::File *source,
                            simple_file::File *destination,
                            unsigned int depth,
                            unsigned int blocks,
                            const char *alg) {
  if (depth == 0 || !alg || !source || !destination) {
     LOG(ERROR) << "Invalid arguments supplied to Initialize";
     LOG(INFO) << "depth: " << depth;
     LOG(INFO) << "s: " << source << " d: " << destination;
     return false;
  }
  if (source_ || destination_) {
    LOG(ERROR) << "Initialize called more than once";
    return false;
  }
  if (blocks > source->Size() / PAGE_SIZE) {
    LOG(ERROR) << blocks << " blocks exceeds image size of " << source->Size();
    return false;
  } else if (blocks == 0) {
    blocks = source->Size() / PAGE_SIZE;
    if (source->Size() % PAGE_SIZE) {
      LOG(ERROR) << "The source file size must be divisible by the block size";
      LOG(ERROR) << "Size: " << source->Size();
      LOG(INFO) << "Suggested size: " << ALIGN(source->Size(),PAGE_SIZE);
      return false;
    }
  }
  alg_ = alg;
  source_ = source;
  destination_ = destination;
  depth_ = depth;
  block_limit_ = blocks;

  // Now we initialize the tree
  if (dm_bht_create(&tree_, depth_, block_limit_, alg_)) {
    LOG(ERROR) << "Could not create the BH tree";
    return false;
  }
  dm_bht_set_write_cb(&tree_, FileHasher::WriteCallback);
  // No reading is needed.
  dm_bht_set_read_cb(&tree_, dm_bht_zeroread_callback);

  return true;
}

bool FileHasher::Store() {
        return !dm_bht_sync(&tree_, reinterpret_cast<void *>(destination_));
}

bool FileHasher::Hash() {
  // TODO(wad) abstract size when dm-bht needs to do break from PAGE_SIZE
  u8 block_data[PAGE_SIZE];
  uint32_t block = 0;

  while (block < block_limit_) {
    if (!source_->Read(PAGE_SIZE, block_data)) {
      LOG(ERROR) << "Failed to read for block: " << block;
      return false;
    }
    if (dm_bht_store_block(&tree_, block, block_data)) {
      LOG(ERROR) << "Failed to store block " << block;
      return false;
    }
    ++block;
  }
  return !dm_bht_compute(&tree_, reinterpret_cast<void *>(destination_));
}

void FileHasher::PrintTable(bool colocated) {
  // Grab the digest (up to 1kbit supported)
  uint8_t digest[128];
  dm_bht_root_hexdigest(&tree_, digest, sizeof(digest));

  // TODO(wad) later support sizes that need 64-bit sectors.
  unsigned int hash_start = 0;
  unsigned int root_end = to_sector(block_limit_ << PAGE_SHIFT);
  if (colocated) hash_start = root_end;
  printf("0 %u verity ROOT_DEV HASH_DEV %u %u %s %s\n",
         root_end, hash_start, depth_, alg_, digest);
}

}  // namespace verity


