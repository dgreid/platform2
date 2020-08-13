/*
 * Copyright (C) 2011 The Chromium OS Authors <chromium-os-dev@chromium.org>
 *
 * Device-Mapper block hash tree interface.
 *
 * This file is released under the GPLv2.
 */
#ifndef __LINUX_DM_BHT_USERSPACE_H
#define __LINUX_DM_BHT_USERSPACE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Functions for creating struct dm_bhts on disk.  A newly created dm_bht
 * should not be directly used for verification. (It should be repopulated.)
 * In addition, these functions aren't meant to be called in parallel.
 */
int dm_bht_compute(struct dm_bht* bht);
void dm_bht_set_buffer(struct dm_bht* bht, void* buffer);
int dm_bht_store_block(struct dm_bht* bht, unsigned int block, u8* block_data);

#ifdef __cplusplus
}
#endif

#endif /* __LINUX_DM_BHT_USERSPACE_H */
