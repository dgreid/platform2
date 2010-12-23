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
  unsigned int blocks = 16384;
  u8 data[PAGE_SIZE];
  // Store all the block hashes of blocks of 0.
  memset(reinterpret_cast<void *>(data), 0, sizeof(data));
  EXPECT_EQ(0, dm_bht_create(&bht, 2, blocks, "sha256"));
  dm_bht_set_read_cb(&bht, dm_bht_zeroread_callback);
  do {
    EXPECT_EQ(dm_bht_store_block(&bht, blocks - 1, data), 0);
  } while (--blocks > 0);
  EXPECT_EQ(0, dm_bht_compute(&bht, NULL));
  EXPECT_EQ(0, dm_bht_destroy(&bht));
}

class MemoryBhtTest : public ::testing::Test {
 public:
  void SetUp() {
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
  void NewBht(const unsigned int depth,
              const unsigned int total_blocks,
              const char *digest_algorithm) {
    bht_.reset(new dm_bht());
    EXPECT_EQ(0, dm_bht_create(bht_.get(),depth, total_blocks,
                               digest_algorithm));
    if (hash_data_.get() == NULL) {
      sectors_ = dm_bht_sectors(bht_.get());
      hash_data_.reset(new u8[to_bytes(sectors_)]);
    }
    dm_bht_set_write_cb(bht_.get(), MemoryBhtTest::WriteCallback);
    dm_bht_set_read_cb(bht_.get(), MemoryBhtTest::ReadCallback);
  }
  void SetupBht(const unsigned int depth,
                const unsigned int total_blocks,
                const char *digest_algorithm) {
    NewBht(depth, total_blocks, digest_algorithm);

    u8 data[PAGE_SIZE];
    // Store all the block hashes of blocks of 0.
    memset(reinterpret_cast<void *>(data), 0, sizeof(data));
    unsigned int blocks = total_blocks;
    do {
      EXPECT_EQ(dm_bht_store_block(bht_.get(), blocks - 1, data), 0);
    } while (--blocks > 0);

    dm_bht_set_read_cb(bht_.get(), dm_bht_zeroread_callback);
    EXPECT_EQ(0, dm_bht_compute(bht_.get(), NULL));
    EXPECT_EQ(0, dm_bht_sync(bht_.get(), reinterpret_cast<void *>(this)));
    u8 digest[1024];
    dm_bht_root_hexdigest(bht_.get(), digest, sizeof(digest));
    LOG(INFO) << "MemoryBhtTest root is " << digest;
    EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
    // bht is now dead and mbht_ is a prepared hash image

    NewBht(depth, total_blocks, digest_algorithm);

    // Load the tree from the pre-populated hash data
    for (blocks = 0; blocks < total_blocks; blocks += bht_->node_count) {
      EXPECT_GE(dm_bht_populate(bht_.get(),
                                reinterpret_cast<void *>(this),
                                blocks),
                DM_BHT_ENTRY_REQUESTED);
      // Since we're testing synchronously, a second run through should yield
      // READY.
      EXPECT_GE(dm_bht_populate(bht_.get(),
                                reinterpret_cast<void *>(this),
                                blocks),
                DM_BHT_ENTRY_READY);
    }
  }

  scoped_ptr<struct dm_bht> bht_;
  scoped_array<u8> hash_data_;
  sector_t sectors_;
};

TEST_F(MemoryBhtTest, CreateThenVerifyOk) {
  static const unsigned int total_blocks = 16384;
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "45d65d6f9e5a962f4d80b5f1bd7a918152251c27bdad8c5f52b590c129833372";
  // This should match what dm_bht_store_block computed earlier.
  static const char kZeroDigest[] =
    "ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7";
  u8 digest[(sizeof(kZeroDigest) - 1) >> 1];
  // TODO(wad) write a test for hex_to_bin and bin_to_hex
  unsigned int digest_size = strlen(kZeroDigest) >> 1;

  dm_bht_hex_to_bin(digest,
                    reinterpret_cast<const u8 *>(kZeroDigest),
                    digest_size);

  SetupBht(2, total_blocks, "sha256");
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  for (unsigned int blocks = 0; blocks < total_blocks; ++blocks) {
    DLOG(INFO) << "verifying block: " << blocks;
    EXPECT_EQ(0, dm_bht_verify_block(bht_.get(), blocks, digest, digest_size));
  }

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
}

TEST_F(MemoryBhtTest, CreateThenVerifyMultipleLevels) {
  static const unsigned int total_blocks = 16384;
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "c86619624d3456f711dbb94d4ad79a4b029f6fd3b5a4a90b155c856bf5b3409b";
  // This should match what dm_bht_store_block computed earlier.
  static const char kZeroDigest[] =
    "ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7";
  u8 digest[(sizeof(kZeroDigest) - 1) >> 1];
  // TODO(wad) write a test for hex_to_bin and bin_to_hex
  unsigned int digest_size = strlen(kZeroDigest) >> 1;

  dm_bht_hex_to_bin(digest,
                    reinterpret_cast<const u8 *>(kZeroDigest),
                    digest_size);

  SetupBht(4, total_blocks, "sha256");
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  for (unsigned int blocks = 0; blocks < total_blocks; ++blocks) {
    DLOG(INFO) << "verifying block: " << blocks;
    EXPECT_EQ(0, dm_bht_verify_block(bht_.get(), blocks, digest, digest_size));
  }

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
}

TEST_F(MemoryBhtTest, CreateThenVerifyOddCount) {
  static const unsigned int total_blocks = 16383;
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "c78d187c430465bd7831fe4908247b6ab5107e3a826d933b71e85aa9a932e03c";
  // This should match what dm_bht_store_block computed earlier.
  static const char kZeroDigest[] =
    "ad7facb2586fc6e966c004d7d1d16b024f5805ff7cb47c7a85dabd8b48892ca7";
  u8 digest[(sizeof(kZeroDigest) - 1) >> 1];
  // TODO(wad) write a test for hex_to_bin and bin_to_hex
  unsigned int digest_size = strlen(kZeroDigest) >> 1;

  dm_bht_hex_to_bin(digest,
                    reinterpret_cast<const u8 *>(kZeroDigest),
                    digest_size);

  SetupBht(4, total_blocks, "sha256");
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  for (unsigned int blocks = 0; blocks < total_blocks; ++blocks) {
    DLOG(INFO) << "verifying block: " << blocks;
    EXPECT_EQ(0, dm_bht_verify_block(bht_.get(), blocks, digest, digest_size));
  }

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
}

TEST_F(MemoryBhtTest, CreateThenVerifyBad) {
  static const unsigned int total_blocks = 16384;
  SetupBht(2, total_blocks, "sha256");
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "45d65d6f9e5a962f4d80b5f1bd7a918152251c27bdad8c5f52b590c129833372";
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  // TODO(wad) add tests for partial tree validity/verification

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

  // Attempt to verify both the bad block and all the neighbors.
  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock + 1, digest, digest_size),
            0);

  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock + 2, digest, digest_size),
            0);

  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock + (bht_->node_count / 2),
                                digest, digest_size),
            0);

  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock, digest, digest_size), 0);

  // Verify that the prior entry is untouched and still safe
  EXPECT_EQ(dm_bht_verify_block(bht_.get(), kBadBlock - 1, digest, digest_size),
            0);

  // Same for the next entry
  EXPECT_EQ(dm_bht_verify_block(bht_.get(), kBadBlock + bht_->node_count,
                                digest, digest_size),
            0);

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
}
