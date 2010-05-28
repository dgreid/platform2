/*
 * Copyright (C) 2010 The Chromium OS Authors <chromium-os-dev@chromium.org>
 *
 * Device-Mapper block hash tree interface.
 * See Documentation/device-mapper/dm-bht.txt for details.
 *
 * This file is released under the GPLv2.
 */
#ifndef __LINUX_DM_BHT_H
#define __LINUX_DM_BHT_H

#include <linux/compiler.h>
#include <linux/crypto.h>
#include <linux/mempool.h>
#include <linux/types.h>

/* To avoid allocating memory for digest tests, we just setup a
 * max to use for now */
#define DM_BHT_MAX_DIGEST_SIZE 128  /* 1k hashes are unlikely for now */

/* UNALLOCATED, PENDING, READY, and VERIFIED are valid states. All other
 * values are entry-related return codes. */
#define DM_BHT_ENTRY_VERIFIED 8  /* All children match their hashes,
				  * but not recursively. */
#define DM_BHT_ENTRY_READY 4  /* data is loaded and available */
#define DM_BHT_ENTRY_PENDING 2  /* data is being loaded */
#define DM_BHT_ENTRY_REQUESTED 1  /* non-state response indicating entry is
				   * pending because of the current call */
#define DM_BHT_ENTRY_UNALLOCATED 0 /* untouched */
#define DM_BHT_ENTRY_ERROR -1 /* entry is unsuitable for use */
#define DM_BHT_ENTRY_ERROR_IO -2 /* I/O error on load */

/* Additional possible return codes */
#define DM_BHT_ENTRY_ERROR_MISMATCH -3 /* Digest mismatch */

/* dm_bht_entry
 * Contains dm_bht->node_count tree nodes at a given tree depth.
 * (where node_count is as many digests as can fit in a page.)
 * state is used to transactionally assure that data is paged in
 * from disk.  Unless dm_bht kept running crypto contexts for each
 * level, we need to load in the data for on-demand verification. */
struct dm_bht_entry {
	atomic_t state; /* see defines */
	/* Keeping an extra pointer per entry wastes up to ~33k of
	 * memory if a 1m blocks are used (or 66 on 64-bit arch) */
	void *io_context;  /* Reserve a pointer for use during io */
	/* data should only be non-NULL if fully populated. */
	u8 *nodes;  /* The hash data used to verify the children */
};

/* dm_bht_level
 * Contains an array of entries that represents a given depth of
 * the tree. */
struct dm_bht_level {
	struct dm_bht_entry *entries;  /* array of entries of tree nodes */
	u32 count;  /* number of entries at this level */
	sector_t sector;  /* starting sector for this level */
};

/* opaque context, start, databuf, sector_count */
typedef int(*dm_bht_callback)(void *,  /* external context */
			      sector_t,  /* start sector */
			      u8 *,	/* destination */
			      sector_t, /* destination size in sectors */
			      struct dm_bht_entry *);
/* dm_bht - Device mapper block hash tree
 * dm_bht provides a fixed interface for comparing data blocks
 * against a cryptographic hashes stored in a hash tree. It
 * optimizes the tree structure for storage on disk.
 * 
 * The tree is built from the bottom up.  A collection of data,
 * external to the tree, is hashed and these hashes are stored
 * as the blocks in the tree.  For some number of these hashes,
 * a parent node is created by hashing them.  These steps are
 * repeated.
 *
 * All hash storage memory is pre-allocated and freed once an
 * entire branch has been verified.
 * TODO(wad) support on-demand LRU of entry and entry nodes.
 */
struct dm_bht {
	/* Configured values */
	/* ENFORCE: depth must be >= 2. */
	unsigned int depth;  /* Depth of the tree including the root */
        u32 block_count;  /* Number of blocks hashed */
	char hash_alg[CRYPTO_MAX_ALG_NAME];

	/* Computed values */
	unsigned int node_count;  /* Data size (in hashes) for each entry */
	unsigned int node_count_shift;  /* first bit set - 1 */
	/* There is one hash_desc per CPU to allow hashing functions to
	 * be called in parallel when on different CPUs */
	struct hash_desc *hash_desc;  /* Container for the hash alg */
	unsigned int digest_size;
	sector_t sectors;  /* Number of disk sectors used */

	/* bool verified;  Full tree is verified */
	u8 *root_digest;  /* hash_alg(levels[0].entries[*].nodes) */
	bool root_verified;
	struct dm_bht_level *levels;  /* in reverse order */
	mempool_t *entry_pool;
	/* Callbacks for reading and/or writing to the hash device */
	dm_bht_callback read_cb;
	dm_bht_callback write_cb;
};

/* Constructor for struct dm_bht instances. */
int dm_bht_create(struct dm_bht *bht, 
		  unsigned int depth,
		  u32 block_count,
		  const char *alg_name);
/* Destructor for struct dm_bht instances.  Does not free @bht */
int dm_bht_destroy(struct dm_bht *bht);

/* Basic accessors for struct dm_bht */
sector_t dm_bht_sectors(const struct dm_bht *bht);
void dm_bht_set_read_cb(struct dm_bht *bht, dm_bht_callback read_cb);
void dm_bht_set_write_cb(struct dm_bht *bht, dm_bht_callback write_cb);
int dm_bht_set_root_hexdigest(struct dm_bht *bht, const u8 *hexdigest);
int dm_bht_root_hexdigest(struct dm_bht *bht, u8 *hexdigest, int available);

/* Functions for loading in data from disk for verification */
int dm_bht_populate(struct dm_bht *bht, void *read_cb_ctx, u32 block_index);
int dm_bht_verify_block(struct dm_bht *bht, u32 block_index, u8 *digest,
			unsigned int digest_len);

/* Functions for creating struct dm_bhts on disk.  A newly created dm_bht
 * should not be directly used for verification. (It should be repopulated.)
 * In addition, these functions aren't meant to be called in parallel. */
int dm_bht_compute(struct dm_bht *bht, void *read_cb_ctx);
int dm_bht_sync(struct dm_bht *bht, void *write_cb_ctx);
int dm_bht_store_block(struct dm_bht *bht, u32 block_index, u8 *block_data);
int dm_bht_zeroread_callback(void *ctx, sector_t start, u8 *dst, sector_t count,
			     struct dm_bht_entry *entry);
void dm_bht_read_completed(struct dm_bht_entry *entry, int status);
void dm_bht_write_completed(struct dm_bht_entry *entry, int status);
#endif  /* __LINUX_DM_BHT_H */
