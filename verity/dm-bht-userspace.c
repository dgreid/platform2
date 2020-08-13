/*
 * Copyright (C) 2011 The Chromium OS Authors <chromium-os-dev@chromium.org>
 *
 * Device-Mapper block hash tree interface.
 *
 * This file is released under the GPL.
 */

#include <errno.h>
#include <string.h>

#include <asm/page.h>
#include <linux/device-mapper.h>

#include "verity/dm-bht.h"

#define DM_MSG_PREFIX "dm bht"

/**
 * dm_bht_compute_hash: hashes a page of data
 */
static int dm_bht_compute_hash(struct dm_bht* bht,
                               const u8* buffer,
                               u8* digest) {
  struct hash_desc* hash_desc = &bht->hash_desc[0];

  /* Note, this is synchronous. */
  if (crypto_hash_init(hash_desc)) {
    DMCRIT("failed to reinitialize crypto hash");
    return -EINVAL;
  }
  if (crypto_hash_update(hash_desc, buffer, PAGE_SIZE)) {
    DMCRIT("crypto_hash_update failed");
    return -EINVAL;
  }
  if (bht->have_salt) {
    if (crypto_hash_update(hash_desc, bht->salt, sizeof(bht->salt))) {
      DMCRIT("crypto_hash_update failed");
      return -EINVAL;
    }
  }
  if (crypto_hash_final(hash_desc, digest)) {
    DMCRIT("crypto_hash_final failed");
    return -EINVAL;
  }

  return 0;
}

void dm_bht_set_buffer(struct dm_bht* bht, void* buffer) {
  int depth;

  for (depth = 0; depth < bht->depth; ++depth) {
    struct dm_bht_level* level = dm_bht_get_level(bht, depth);
    struct dm_bht_entry* entry_end = level->entries + level->count;
    struct dm_bht_entry* entry;

    for (entry = level->entries; entry < entry_end; ++entry) {
      entry->nodes = buffer;
      memset(buffer, 0, PAGE_SIZE);
      buffer += PAGE_SIZE;
    }
  }
}

/**
 * dm_bht_compute - computes and updates all non-block-level hashes in a tree
 * @bht:	pointer to a dm_bht_create()d bht
 *
 * Returns 0 on success, >0 when data is pending, and <0 when a IO or other
 * error has occurred.
 *
 * Walks the tree and computes the hashes at each level from the
 * hashes below.
 */
int dm_bht_compute(struct dm_bht* bht) {
  int depth, r = 0;

  for (depth = bht->depth - 2; depth >= 0; depth--) {
    struct dm_bht_level* level = dm_bht_get_level(bht, depth);
    struct dm_bht_level* child_level = level + 1;
    struct dm_bht_entry* entry = level->entries;
    struct dm_bht_entry* child = child_level->entries;
    unsigned int i, j;

    for (i = 0; i < level->count; i++, entry++) {
      unsigned int count = bht->node_count;

      memset(entry->nodes, 0, PAGE_SIZE);
      entry->state = DM_BHT_ENTRY_READY;

      if (i == (level->count - 1))
        count = child_level->count % bht->node_count;
      if (count == 0)
        count = bht->node_count;
      for (j = 0; j < count; j++, child++) {
        u8* digest = dm_bht_node(bht, entry, j);

        r = dm_bht_compute_hash(bht, child->nodes, digest);
        if (r) {
          DMERR("Failed to update (d=%d,i=%u)", depth, i);
          goto out;
        }
      }
    }
  }
  r = dm_bht_compute_hash(bht, bht->levels[0].entries->nodes, bht->root_digest);
  if (r)
    DMERR("Failed to update root hash");

out:
  return r;
}

/**
 * dm_bht_store_block - sets a given block's hash in the tree
 * @bht:	pointer to a dm_bht_create()d bht
 * @block:	numeric index of the block in the tree
 * @block_data:	array of u8s containing the block of data to hash
 *
 * Returns 0 on success.
 *
 * If the containing entry in the tree is unallocated, it will allocate memory
 * and mark the entry as ready.  All other block entries will be 0s.
 *
 * It is up to the users of the update interface to ensure the entry data is
 * fully populated prior to use. The number of updated entries is NOT tracked.
 */
int dm_bht_store_block(struct dm_bht* bht, unsigned int block, u8* block_data) {
  int depth = bht->depth;
  struct dm_bht_entry* entry = dm_bht_get_entry(bht, depth - 1, block);
  u8* node = dm_bht_get_node(bht, entry, depth, block);

  return dm_bht_compute_hash(bht, block_data, node);
}
