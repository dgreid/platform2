// Copyright (C) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Basic unittesting of dm-bht using google-gtest.

#include <base/basictypes.h>
#include <base/logging.h>
#include <base/scoped_ptr.h>
#include <gtest/gtest.h>

// Pull in dm-bht.c so that we can access static functions.
// But disable verbose logging.
extern "C" {
#ifndef NDEBUG
#  undef NDEBUG
#  include "dm-bht.c"
#  define NDEBUG 1
#else
#  include "dm-bht.c"
#endif
}

TEST(DmBht, CreateFailOnOverflow) {
  struct dm_bht bht;
  // This should fail.
  EXPECT_EQ(-EINVAL, dm_bht_create(&bht, 32, 1, "sha256"));
}

// Simple test to help valgrind/tcmalloc catch bad mem management
TEST(DmBht, CreateZeroPopulateDestroy) {
  struct dm_bht bht;
  // This should fail.
  EXPECT_EQ(0, dm_bht_create(&bht, 2, 16384, "sha256"));
  dm_bht_set_read_cb(&bht, dm_bht_zeroread_callback);
  EXPECT_EQ(0, dm_bht_compute(&bht, NULL));
  EXPECT_EQ(0, dm_bht_destroy(&bht));
}

class MemoryBhtTest : public ::testing::Test {
 public:
  void SetUp() {
    NewBht();

    u8 data[PAGE_SIZE];
    // Store all the block hashes of blocks of 0.
    memset(reinterpret_cast<void *>(data), 0, sizeof(data));
    unsigned int blocks = kTotalBlocks;
    do {
      EXPECT_EQ(dm_bht_store_block(bht_.get(), blocks - 1, data), 0);
    } while (--blocks > 0);

    dm_bht_set_read_cb(bht_.get(), dm_bht_zeroread_callback);
    EXPECT_EQ(0, dm_bht_compute(bht_.get(), NULL));
    EXPECT_EQ(0, dm_bht_sync(bht_.get(), reinterpret_cast<void *>(this)));
    u8 digest[256];
    dm_bht_root_hexdigest(bht_.get(), digest, sizeof(digest));
    LOG(INFO) << "MemoryBhtTest root is " << digest;
    EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
    // bht is now dead and mbht_ is a prepared hash image
  }

  int Write(sector_t start, u8 *src, sector_t count) {
    EXPECT_LT(start, sectors_);
    EXPECT_EQ(to_bytes(count), PAGE_SIZE);
    u8 *dst = &hash_data_[to_bytes(start)];
    memcpy(dst, src, to_bytes(count));
    return 0;
  }

  int Read(sector_t start, u8 *dst, sector_t count) {
    EXPECT_LT(start, sectors_);
    EXPECT_EQ(to_bytes(count), PAGE_SIZE);
    u8 *src = &hash_data_[to_bytes(start)];
    memcpy(dst, src, to_bytes(count));
    return 0;
  }

  static int WriteCallback(void *mbht_instance,
                           sector_t start,
                           u8 *src,
                           sector_t count,
                           struct dm_bht_entry *entry) {
    MemoryBhtTest *mbht = reinterpret_cast<MemoryBhtTest *>(mbht_instance);
    mbht->Write(start, src, count);
    dm_bht_write_completed(entry, 0);
    return 0;
  }

  static int ReadCallback(void *mbht_instance,
                          sector_t start,
                          u8 *dst,
                          sector_t count,
                          struct dm_bht_entry *entry) {
    MemoryBhtTest *mbht = reinterpret_cast<MemoryBhtTest *>(mbht_instance);
    mbht->Read(start, dst, count);
    dm_bht_read_completed(entry, 0);
    return 0;
  }

 protected:
  // Creates a new dm_bht and sets it in the existing MemoryBht.
  void NewBht() {
    bht_.reset(new dm_bht());
    EXPECT_EQ(0, dm_bht_create(bht_.get(), kDepth, kTotalBlocks,
                               kDigestAlgorithm));
    if (hash_data_.get() == NULL) {
      sectors_ = dm_bht_sectors(bht_.get());
      hash_data_.reset(new u8[to_bytes(sectors_)]);
    }
    dm_bht_set_write_cb(bht_.get(), MemoryBhtTest::WriteCallback);
    dm_bht_set_read_cb(bht_.get(), MemoryBhtTest::ReadCallback);
  }

  static const unsigned int kDepth;
  static const unsigned int kTotalBlocks;
  static const char *kDigestAlgorithm;
  scoped_ptr<struct dm_bht> bht_;
  scoped_array<u8> hash_data_;
  sector_t sectors_;
};
const unsigned int MemoryBhtTest::kDepth = 2;
const unsigned int MemoryBhtTest::kTotalBlocks = 16384;
const char *MemoryBhtTest::kDigestAlgorithm = "sha256";

TEST_F(MemoryBhtTest, CreateThenVerifyOk) {
  NewBht();
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "45d65d6f9e5a962f4d80b5f1bd7a918152251c27bdad8c5f52b590c129833372";
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  // This should match what dm_bht_store_block computed earlier.
  static const char kZeroDigest[] =
    "ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7";
  u8 digest[(sizeof(kZeroDigest) - 1) >> 1];
  // TODO(wad) write a test for hex_to_bin and bin_to_hex
  unsigned int digest_size = strlen(kZeroDigest) >> 1;
  dm_bht_hex_to_bin(digest,
                    reinterpret_cast<const u8 *>(kZeroDigest),
                    digest_size);

  unsigned int blocks = kTotalBlocks;
  do {
    EXPECT_GE(dm_bht_populate(bht_.get(),
                              reinterpret_cast<void *>(this),
                              blocks - 1),
              DM_BHT_ENTRY_READY);
    EXPECT_EQ(0, dm_bht_verify_block(bht_.get(),
                                     blocks - 1, digest, digest_size));
  } while (--blocks > 0);

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
}

TEST_F(MemoryBhtTest, CreateThenVerifyBad) {
  NewBht();
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "45d65d6f9e5a962f4d80b5f1bd7a918152251c27bdad8c5f52b590c129833372";
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  // Load the tree from the pre-populated hash data
  unsigned int blocks = kTotalBlocks;
  do {
    EXPECT_GE(dm_bht_populate(bht_.get(),
                              reinterpret_cast<void *>(this),
                              blocks - 1),
              DM_BHT_ENTRY_READY);
    // TODO(wad) add tests for partial tree validity/verification
  } while (--blocks > 0);

  // Corrupt one block value
  static const unsigned int kBadBlock = 256;
  u8 data[PAGE_SIZE];
  memset(reinterpret_cast<void *>(data), 'A', sizeof(data));
  EXPECT_EQ(dm_bht_store_block(bht_.get(), kBadBlock, data), 0);

  // This should match what dm_bht_store_block computed earlier.
  static const char kZeroDigest[] =
    "ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7";
  u8 digest[(sizeof(kZeroDigest) - 1) >> 1];
  // TODO(wad) write a test for hex_to_bin and bin_to_hex
  unsigned int digest_size = strlen(kZeroDigest) >> 1;
  dm_bht_hex_to_bin(digest,
                    reinterpret_cast<const u8 *>(kZeroDigest),
                    digest_size);

  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock + 1, digest, digest_size),
            0);

  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock + 2, digest, digest_size),
            0);

  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock - 1, digest, digest_size),
            0);

  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock, digest, digest_size), 0);



  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
}
