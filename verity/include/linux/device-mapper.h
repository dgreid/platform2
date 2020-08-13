/* Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by the GPL v2 license that can
 * be found in the LICENSE file.
 *
 * Parts of this file are derived from the Linux kernel from the file with
 * the same name and path under include/.
 */
#ifndef VERITY_INCLUDE_LINUX_DEVICE_MAPPER_H_
#define VERITY_INCLUDE_LINUX_DEVICE_MAPPER_H_
#include <stdio.h>

#ifndef NDEBUG
#define CONFIG_DM_DEBUG 1
#endif

#define DMLOG(fmt, args...) \
  fprintf(stderr, "dm:" DM_MSG_PREFIX fmt "\n", ##args)
#ifdef CONFIG_DM_DEBUG
#define DMDEBUG(fmt, args...) DMLOG("[DEBUG] " fmt, ##args)
#else
#define DMDEBUG(fmt, args...) \
  {}
#endif
#define DMINFO(fmt, args...) DMLOG("[INFO] " fmt, ##args)
#define DMERR(fmt, args...) DMLOG("[ERR] " fmt, ##args)
/* TODO(wad) remap to google-glog to get easy logging support */
#define DMERR_LIMIT(fmt, args...) DMERR(fmt, ##args)
#define DMCRIT(fmt, args...) DMLOG("[CRIT] " fmt, ##args)

#define SECTOR_SHIFT 9
#define to_sector(x) ((x) >> SECTOR_SHIFT)
#define verity_to_bytes(x) ((x) << SECTOR_SHIFT)

#endif /* VERITY_INCLUDE_LINUX_DEVICE_MAPPER_H_ */
