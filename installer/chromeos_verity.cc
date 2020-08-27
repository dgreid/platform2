/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "installer/chromeos_verity.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <mtd/ubi-user.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <verity/dm-bht.h>
#include <verity/dm-bht-userspace.h>

namespace {

#define IO_BUF_SIZE (1UL * 1024 * 1024)

// 512 bytes in a sector.
#define SECTOR_SHIFT (9ULL)

// Obtain LEB size of a UBI volume. Return -1 if |dev| is not a UBI volume.
int64_t GetUbiLebSize(const std::string& dev) {
  struct stat stat_buf;
  if (stat(dev.c_str(), &stat_buf)) {
    PLOG(WARNING) << "Cannot stat " << dev;
    return -1;
  }

  if (!S_ISCHR(stat_buf.st_mode)) {
    // Not a character device, not an UBI device.
    return -1;
  }

  // Make sure this is UBI.
  int dev_major = major(stat_buf.st_rdev);
  int dev_minor = minor(stat_buf.st_rdev);
  const base::FilePath sys_dev("/sys/dev/char/" + std::to_string(dev_major) +
                               ":" + std::to_string(dev_minor));

  const base::FilePath subsystem = sys_dev.Append("subsystem");
  base::FilePath target;
  if (!base::ReadSymbolicLink(subsystem, &target)) {
    PLOG(WARNING) << "Cannot tell where " << subsystem.value() << " links to";
    return -1;
  }
  if (target.BaseName().value() != "ubi") {
    // Not an UBI device, so silently ignore it.
    return -1;
  }

  // Only a volume has update marker.
  const base::FilePath upd_marker = sys_dev.Append("upd_marker");
  if (!base::PathExists(upd_marker)) {
    return -1;
  }

  const base::FilePath usable_eb_size = sys_dev.Append("usable_eb_size");
  std::string data;
  if (!base::ReadFileToString(usable_eb_size, &data)) {
    PLOG(WARNING) << "Cannot read " << usable_eb_size.value();
    return -1;
  }
  int64_t eb_size;
  if (!base::StringToInt64(data, &eb_size)) {
    PLOG(WARNING) << "Cannot convert data: " << data;
    return -1;
  }

  return eb_size;
}

// Align |value| up to the nearest greater multiple of |block|. DO NOT assume
// that |block| is a power of 2.
constexpr off64_t AlignUp(off64_t value, off64_t block) {
  off64_t t = value + block - 1;
  return t - (t % block);
}

ssize_t PwriteToUbi(int fd,
                    const uint8_t* src,
                    size_t size,
                    off64_t offset,
                    ssize_t eraseblock_size) {
  struct ubi_set_vol_prop_req prop = {
      .property = UBI_VOL_PROP_DIRECT_WRITE,
      .value = 1,
  };
  if (ioctl(fd, UBI_IOCSETVOLPROP, &prop)) {
    PLOG(WARNING) << "Failed to enable direct write";
    return -1;
  }

  // Save the cursor.
  off64_t cur_pos = lseek64(fd, 0, SEEK_CUR);
  if (cur_pos < 0) {
    return -1;
  }

  // Align up to next LEB.
  offset = AlignUp(offset, eraseblock_size);
  if (lseek64(fd, offset, SEEK_SET) < 0) {
    return -1;
  }

  // TODO(ahassani): Convert this to a std::vector.
  uint8_t* buf = static_cast<uint8_t*>(malloc(eraseblock_size));
  if (!buf) {
    return -1;
  }

  // Start writing in blocks of LEB size.
  size_t nr_written = 0;
  while (nr_written < size) {
    size_t to_write = size - nr_written;
    if (to_write > eraseblock_size) {
      to_write = eraseblock_size;
    } else {
      // UBI layer requires 0xFF padding the erase block.
      memset(buf + to_write, 0xFF, eraseblock_size - to_write);
    }
    memcpy(buf, src + nr_written, to_write);
    int32_t leb_no = (offset + nr_written) / eraseblock_size;
    if (ioctl(fd, UBI_IOCEBUNMAP, &leb_no) < 0) {
      PLOG(WARNING) << "Cannot unmap LEB " << leb_no;
      free(buf);
      return -1;
    }
    ssize_t chunk_written = write(fd, buf, eraseblock_size);
    if (chunk_written != eraseblock_size) {
      PLOG(WARNING) << "Failed to write to LEB " << leb_no;
      free(buf);
      return -1;
    }
    nr_written += chunk_written;
  }
  free(buf);

  // Reset the cursor.
  if (lseek64(fd, cur_pos, SEEK_SET) < 0) {
    return -1;
  }

  return nr_written;
}

ssize_t WriteHash(const std::string& dev,
                  const uint8_t* buf,
                  size_t size,
                  off64_t offset) {
  int64_t eraseblock_size = GetUbiLebSize(dev);
  base::ScopedFD fd(open(dev.c_str(), O_WRONLY | O_CLOEXEC));
  if (!fd.is_valid()) {
    PLOG(WARNING) << "Cannot open " << dev << " for writing";
    return -1;
  }
  if (eraseblock_size <= 0) {
    return pwrite(fd.get(), buf, size, offset);
  } else {
    return PwriteToUbi(fd.get(), buf, size, offset, eraseblock_size);
  }
}

}  // namespace

int chromeos_verity(const std::string& alg,
                    const std::string& device,
                    unsigned blocksize,
                    uint64_t fs_blocks,
                    const std::string& salt,
                    const std::string& expected,
                    bool enforce_rootfs_verification) {
  // Blocksize better be a power of two and fit into 1 MiB.
  if (IO_BUF_SIZE % blocksize != 0) {
    LOG(WARNING) << "blocksize % " << IO_BUF_SIZE << " != 0";
    return -EINVAL;
  }

  // we don't need to call dm_bht_destroy after this because we're supplying
  // our own buffer -- in fact calling it will trigger a bogus assert.
  int ret;
  struct dm_bht bht;
  if ((ret = dm_bht_create(&bht, fs_blocks, alg.c_str()))) {
    LOG(WARNING) << "dm_bht_create failed: " << ret;
    return ret;
  }

  uint8_t* io_buffer;
  ret = posix_memalign(reinterpret_cast<void**>(&io_buffer), blocksize,
                       IO_BUF_SIZE);
  if (ret != 0) {
    LOG(WARNING) << "posix_memalign io_buffer failed: " << ret;
    return ret;
  }

  // We aren't going to do any automatic reading.
  dm_bht_set_read_cb(&bht, dm_bht_zeroread_callback);
  dm_bht_set_salt(&bht, salt.c_str());
  size_t hash_size = dm_bht_sectors(&bht) << SECTOR_SHIFT;

  uint8_t* hash_buffer;
  ret = posix_memalign(reinterpret_cast<void**>(&hash_buffer), blocksize,
                       hash_size);
  if (ret != 0) {
    LOG(WARNING) << "posix_memalign hash_buffer failed: " << ret;
    free(io_buffer);
    return ret;
  }
  memset(hash_buffer, 0, hash_size);
  dm_bht_set_buffer(&bht, hash_buffer);

  base::ScopedFD fd(open(device.c_str(), O_RDONLY | O_CLOEXEC));
  if (!fd.is_valid()) {
    PLOG(WARNING) << "error opening " << device;
    free(io_buffer);
    free(hash_buffer);
    return errno;
  }

  uint64_t cur_block = 0;
  while (cur_block < fs_blocks) {
    unsigned int i;
    ssize_t readb;
    size_t count = (fs_blocks - cur_block) * blocksize;

    if (count > IO_BUF_SIZE)
      count = IO_BUF_SIZE;

    // TODO(ahassani): Make this robust against partial reads.
    readb = pread(fd.get(), io_buffer, count, cur_block * blocksize);
    if (readb < 0) {
      PLOG(WARNING) << "read returned error";
      free(io_buffer);
      free(hash_buffer);
      return errno;
    }

    for (i = 0; i < (count / blocksize); i++) {
      ret = dm_bht_store_block(&bht, cur_block, io_buffer + (i * blocksize));
      if (ret) {
        LOG(WARNING) << "dm_bht_store_block returned error: " << ret;
        free(io_buffer);
        free(hash_buffer);
        return ret;
      }
      cur_block++;
    }
  }
  free(io_buffer);
  fd.reset();

  ret = dm_bht_compute(&bht);
  if (ret) {
    LOG(WARNING) << "dm_bht_compute returned error: " << ret;
    free(hash_buffer);
    return ret;
  }

  uint8_t digest[DM_BHT_MAX_DIGEST_SIZE];
  dm_bht_root_hexdigest(&bht, digest, DM_BHT_MAX_DIGEST_SIZE);

  if (memcmp(digest, expected.c_str(), bht.digest_size)) {
    LOG(ERROR) << "Filesystem hash verification failed";
    LOG(ERROR) << "Expected " << expected << " != actual "
               << reinterpret_cast<char*>(digest);
    if (enforce_rootfs_verification) {
      free(hash_buffer);
      return -1;
    } else {
      LOG(INFO) << "Verified Boot not enabled; ignoring";
    }
  }

  ssize_t written =
      WriteHash(device, hash_buffer, hash_size, cur_block * blocksize);
  if (written < static_cast<ssize_t>(hash_size)) {
    PLOG(ERROR) << "Writing out hash failed: written" << written
                << ", expected %d" << hash_size;
    free(hash_buffer);
    return errno;
  }
  free(hash_buffer);

  return 0;
}
