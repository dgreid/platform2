 /*
 * Copyright (C) 2011 The Chromium OS Authors <chromium-os-dev@chromium.org>
 *
 * Device-Mapper block hash tree interface.
 *
 * This file is released under the GPL.
 */

#include <errno.h>
#include <string.h>

#include <asm/atomic.h>
#include <asm/page.h>
#include <linux/device-mapper.h>
#include <linux/dm-bht.h>
#include <linux/gfp.h>
#include <linux/scatterlist.h>

#define DM_MSG_PREFIX "dm bht"

/**
 * dm_bht_compute_hash: hashes a page of data
 */
static int dm_bht_compute_hash(struct dm_bht *bht, struct page *pg,
			       unsigned int offset, u8 *digest)
{
	struct hash_desc *hash_desc = &bht->hash_desc[0];
	struct scatterlist sg;

	sg_init_table(&sg, 1);
	sg_set_page(&sg, pg, PAGE_SIZE, offset);
	/* Note, this is synchronous. */
	if (crypto_hash_init(hash_desc)) {
	  DMCRIT("failed to reinitialize crypto hash");
		return -EINVAL;
	}
	if (crypto_hash_update(hash_desc, &sg, PAGE_SIZE)) {
		DMCRIT("crypto_hash_update failed");
		return -EINVAL;
	}
	if (bht->have_salt) {
		sg_set_buf(&sg, bht->salt, sizeof(bht->salt));
		if (crypto_hash_update(hash_desc, &sg, sizeof(bht->salt))) {
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

/**
 * dm_bht_compute - computes and updates all non-block-level hashes in a tree
 * @bht:	pointer to a dm_bht_create()d bht
 * @read_cb_ctx:opaque read_cb context for all I/O on this call
 *
 * Returns 0 on success, >0 when data is pending, and <0 when a IO or other
 * error has occurred.
 *
 * Walks the tree and computes the hashes at each level from the
 * hashes below. This can only be called once per tree creation
 * since it will mark entries verified. Expects dm_bht_populate() to
 * correctly populate the tree from the read_callback_stub.
 *
 * This function should not be used when verifying the same tree and
 * should not be used with multiple simultaneous operators on @bht.
 */
int dm_bht_compute(struct dm_bht *bht, void *read_cb_ctx)
{
	int depth, r = 0;

	for (depth = bht->depth - 2; depth >= 0; depth--) {
		struct dm_bht_level *level = dm_bht_get_level(bht, depth);
		struct dm_bht_level *child_level = level + 1;
		struct dm_bht_entry *entry = level->entries;
		struct dm_bht_entry *child = child_level->entries;
		unsigned int i, j;

		for (i = 0; i < level->count; i++, entry++) {
			unsigned int count = bht->node_count;
			struct page *pg;

			pg = alloc_page(GFP_NOIO);
			if (!pg) {
				DMCRIT("an error occurred while reading entry");
				goto out;
			}

			entry->nodes = page_address(pg);
			memset(entry->nodes, 0, PAGE_SIZE);
			atomic_set(&entry->state, DM_BHT_ENTRY_READY);

			if (i == (level->count - 1))
				count = child_level->count % bht->node_count;
			if (count == 0)
				count = bht->node_count;
			for (j = 0; j < count; j++, child++) {
				struct page *pg = virt_to_page(child->nodes);
				u8 *digest = dm_bht_node(bht, entry, j);

				r = dm_bht_compute_hash(bht, pg, 0, digest);
				if (r) {
					DMERR("Failed to update (d=%d,i=%u)",
					      depth, i);
					goto out;
				}
			}
		}
	}
	r = dm_bht_compute_hash(bht,
				virt_to_page(bht->levels[0].entries->nodes),
				0, bht->root_digest);
	if (r)
		DMERR("Failed to update root hash");

out:
	return r;
}

/**
 * dm_bht_sync - writes the tree in memory to disk
 * @bht:	pointer to a dm_bht_create()d bht
 * @write_ctx:	callback context for writes issued
 *
 * Since all entry nodes are PAGE_SIZE, the data will be pre-aligned and
 * padded.
 */
int dm_bht_sync(struct dm_bht *bht, void *write_cb_ctx)
{
	int depth;
	int ret = 0;
	int state;
	sector_t sector;
	struct dm_bht_level *level;
	struct dm_bht_entry *entry;
	struct dm_bht_entry *entry_end;

	for (depth = 0; depth < bht->depth; ++depth) {
		level = dm_bht_get_level(bht, depth);
		entry_end = level->entries + level->count;
		sector = level->sector;
		for (entry = level->entries; entry < entry_end; ++entry) {
			state = atomic_read(&entry->state);
			if (state <= DM_BHT_ENTRY_PENDING) {
				DMERR("At depth %d, entry %lu is not ready",
				      depth,
				      (unsigned long)(entry - level->entries));
				return state;
			}
			ret = bht->write_cb(write_cb_ctx,
					    sector,
					    entry->nodes,
					    to_sector(PAGE_SIZE),
					    entry);
			if (ret) {
				DMCRIT("an error occurred writing entry %lu",
				      (unsigned long)(entry - level->entries));
				return ret;
			}
			sector += to_sector(PAGE_SIZE);
		}
	}

	return 0;
}

/**
 * dm_bht_store_block - sets a given block's hash in the tree
 * @bht:	pointer to a dm_bht_create()d bht
 * @block:	numeric index of the block in the tree
 * @digest:	array of u8s containing the digest of length @bht->digest_size
 *
 * Returns 0 on success, >0 when data is pending, and <0 when a IO or other
 * error has occurred.
 *
 * If the containing entry in the tree is unallocated, it will allocate memory
 * and mark the entry as ready.  All other block entries will be 0s.  This
 * function is not safe for simultaneous use when verifying data and should not
 * be used if the @bht is being accessed by any other functions in any other
 * threads/processes.
 *
 * It is expected that virt_to_page will work on |block_data|.
 */
int dm_bht_store_block(struct dm_bht *bht, unsigned int block,
		       u8 *block_data)
{
	int depth;
	unsigned int index;
	unsigned int node_index;
	struct dm_bht_entry *entry;
	struct dm_bht_level *level;
	int state;
	struct page *node_page = NULL;

	/* Look at the last level of nodes above the leaves (data blocks) */
	depth = bht->depth - 1;

	/* Index into the level */
	level = dm_bht_get_level(bht, depth);
	index = dm_bht_index_at_level(bht, depth, block);
	/* Grab the node index into the current entry by getting the
	 * index at the leaf-level.
	 */
	node_index = dm_bht_index_at_level(bht, depth + 1, block) %
		     bht->node_count;
	entry = &level->entries[index];

	DMDEBUG("Storing block %u in d=%d,ei=%u,ni=%u,s=%d",
		block, depth, index, node_index,
		atomic_read(&entry->state));

	state = atomic_cmpxchg(&entry->state,
			       DM_BHT_ENTRY_UNALLOCATED,
			       DM_BHT_ENTRY_PENDING);
	/* !!! Note. It is up to the users of the update interface to
	 *     ensure the entry data is fully populated prior to use.
	 *     The number of updated entries is NOT tracked.
	 */
	if (state == DM_BHT_ENTRY_UNALLOCATED) {
		node_page = alloc_page(GFP_KERNEL);
		if (!node_page) {
			atomic_set(&entry->state, DM_BHT_ENTRY_ERROR);
			return -ENOMEM;
		}
		entry->nodes = page_address(node_page);
		memset(entry->nodes, 0, PAGE_SIZE);
		/* TODO(wad) could expose this to the caller to that they
		 * can transition from unallocated to ready manually.
		 */
		atomic_set(&entry->state, DM_BHT_ENTRY_READY);
	} else if (state <= DM_BHT_ENTRY_ERROR) {
		DMCRIT("leaf entry for block %u is invalid",
		      block);
		return state;
	} else if (state == DM_BHT_ENTRY_PENDING) {
		DMERR("leaf data is pending for block %u", block);
		return 1;
	}

	dm_bht_compute_hash(bht, virt_to_page(block_data), 0,
			    dm_bht_node(bht, entry, node_index));
	return 0;
}
