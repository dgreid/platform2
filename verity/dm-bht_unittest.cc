// Copyright (C) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by the GPL v2 license that can
// be found in the LICENSE file.
//
// Basic unittesting of dm-bht using google-gtest.

#include <base/basictypes.h>
#include <base/logging.h>
#include <base/memory/scoped_ptr.h>
#include <gtest/gtest.h>
#include <stdlib.h>

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

void *my_memalign(size_t boundary, size_t size) {
  void * memptr;
  if (posix_memalign(&memptr, boundary, size))
    return NULL;
  return memptr;
}

TEST(DmBht, CreateFailOnOverflow) {
  struct dm_bht bht;
  // This should fail.
  EXPECT_EQ(-EINVAL, dm_bht_create(&bht, UINT_MAX, "sha1"));
}

// Simple test to help valgrind/tcmalloc catch bad mem management
TEST(DmBht, CreateZeroPopulateDestroy) {
  struct dm_bht bht;
  // This should fail.
  unsigned int blocks = 16384;
  u8 *data = (u8 *)my_memalign(PAGE_SIZE, PAGE_SIZE);

  // Store all the block hashes of blocks of 0.
  memset(reinterpret_cast<void *>(data), 0, sizeof(data));
  EXPECT_EQ(0, dm_bht_create(&bht, blocks, "sha256"));
  dm_bht_set_read_cb(&bht, dm_bht_zeroread_callback);
  do {
    EXPECT_EQ(dm_bht_store_block(&bht, blocks - 1, data), 0);
  } while (--blocks > 0);
  EXPECT_EQ(0, dm_bht_compute(&bht, NULL));
  EXPECT_EQ(0, dm_bht_destroy(&bht));
  free(data);
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
  void NewBht(const unsigned int total_blocks,
              const char *digest_algorithm) {
    bht_.reset(new dm_bht());
    EXPECT_EQ(0, dm_bht_create(bht_.get(), total_blocks,
                               digest_algorithm));
    if (hash_data_.get() == NULL) {
      sectors_ = dm_bht_sectors(bht_.get());
      hash_data_.reset(new u8[to_bytes(sectors_)]);
    }
    dm_bht_set_write_cb(bht_.get(), MemoryBhtTest::WriteCallback);
    dm_bht_set_read_cb(bht_.get(), MemoryBhtTest::ReadCallback);
  }
  void SetupBht(const unsigned int total_blocks,
                const char *digest_algorithm) {
    NewBht(total_blocks, digest_algorithm);

    u8 *data = (u8 *)my_memalign(PAGE_SIZE, PAGE_SIZE);

    memset(data, 0, PAGE_SIZE);

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

    NewBht(total_blocks, digest_algorithm);

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
    free(data);
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
  // A page of all zeros
  u8 *zero_page = (u8 *)my_memalign(PAGE_SIZE, PAGE_SIZE);

  memset(zero_page, 0, PAGE_SIZE);

  SetupBht(total_blocks, "sha256");
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  for (unsigned int blocks = 0; blocks < total_blocks; ++blocks) {
    DLOG(INFO) << "verifying block: " << blocks;
    EXPECT_EQ(0, dm_bht_verify_block(bht_.get(), blocks,
                                     virt_to_page(zero_page), 0));
  }

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
  free(zero_page);
}

TEST_F(MemoryBhtTest, CreateThenVerifySingleLevel) {
  static const unsigned int total_blocks = 32;
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "2d3a43008286f56536fa24dcdbf14d342f0548827e374210415c7be0b610d2ba";
  // A page of all zeros
  u8 *zero_page = (u8 *)my_memalign(PAGE_SIZE, PAGE_SIZE);

  memset(zero_page, 0, PAGE_SIZE);

  SetupBht(total_blocks, "sha256");
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  for (unsigned int blocks = 0; blocks < total_blocks; ++blocks) {
    DLOG(INFO) << "verifying block: " << blocks;
    EXPECT_EQ(0, dm_bht_verify_block(bht_.get(), blocks,
                                     virt_to_page(zero_page), 0));
  }

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
  free(zero_page);
}

TEST_F(MemoryBhtTest, CreateThenVerifyRealParameters) {
  static const unsigned int total_blocks = 217600;
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "15d5a180b5080a1d43e3fbd1f2cd021d0fc3ea91a8e330bad468b980c2fd4d8b";
  // A page of all zeros
  u8 *zero_page = (u8 *)my_memalign(PAGE_SIZE, PAGE_SIZE);

  memset(zero_page, 0, PAGE_SIZE);

  SetupBht(total_blocks, "sha256");
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  for (unsigned int blocks = 0; blocks < total_blocks; ++blocks) {
    DLOG(INFO) << "verifying block: " << blocks;
    EXPECT_EQ(0, dm_bht_verify_block(bht_.get(), blocks,
                                     virt_to_page(zero_page), 0));
  }

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
  free(zero_page);
}

TEST_F(MemoryBhtTest, CreateThenVerifyOddLeafCount) {
  static const unsigned int total_blocks = 16383;
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "dc8cec4220d388b05ba75c853f858bb8cc25edfb1d5d2f3be6bdf9edfa66dc6a";
  // A page of all zeros
  u8 *zero_page = (u8 *)my_memalign(PAGE_SIZE, PAGE_SIZE);

  memset(zero_page, 0, PAGE_SIZE);

  SetupBht(total_blocks, "sha256");
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  for (unsigned int blocks = 0; blocks < total_blocks; ++blocks) {
    DLOG(INFO) << "verifying block: " << blocks;
    EXPECT_EQ(0, dm_bht_verify_block(bht_.get(), blocks,
                                     virt_to_page(zero_page), 0));
  }

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
  free(zero_page);
}

TEST_F(MemoryBhtTest, CreateThenVerifyOddNodeCount) {
  static const unsigned int total_blocks = 16000;
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "10832dd62c427bcf68c56c8de0d1f9c32b61d9e5ddf43c77c56a97b372ad4b07";
  // A page of all zeros
  u8 *zero_page = (u8 *)my_memalign(PAGE_SIZE, PAGE_SIZE);

  memset(zero_page, 0, PAGE_SIZE);

  SetupBht(total_blocks, "sha256");
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  for (unsigned int blocks = 0; blocks < total_blocks; ++blocks) {
    DLOG(INFO) << "verifying block: " << blocks;
    EXPECT_EQ(0, dm_bht_verify_block(bht_.get(), blocks,
                                     virt_to_page(zero_page), 0));
  }

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
  free(zero_page);
}

TEST_F(MemoryBhtTest, CreateThenVerifyBadHashBlock) {
  static const unsigned int total_blocks = 16384;
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "45d65d6f9e5a962f4d80b5f1bd7a918152251c27bdad8c5f52b590c129833372";
  // A page of all zeros
  u8 *zero_page = (u8 *)my_memalign(PAGE_SIZE, PAGE_SIZE);

  memset(zero_page, 0, PAGE_SIZE);

  SetupBht(total_blocks, "sha256");

  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));

  // TODO(wad) add tests for partial tree validity/verification

  // Corrupt one has hblock
  static const unsigned int kBadBlock = 256;
  u8 *bad_hash_block= (u8 *)my_memalign(PAGE_SIZE, PAGE_SIZE);
  memset(bad_hash_block, 'A', PAGE_SIZE);
  EXPECT_EQ(dm_bht_store_block(bht_.get(), kBadBlock, bad_hash_block), 0);

  // Attempt to verify both the bad block and all the neighbors.
  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock + 1,
                                virt_to_page(zero_page), 0), 0);

  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock + 2,
                                virt_to_page(zero_page), 0), 0);

  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock + (bht_->node_count / 2),
                                virt_to_page(zero_page), 0), 0);

  EXPECT_LT(dm_bht_verify_block(bht_.get(), kBadBlock,
                                virt_to_page(zero_page), 0), 0);

  // Verify that the prior entry is untouched and still safe
  EXPECT_EQ(dm_bht_verify_block(bht_.get(), kBadBlock - 1,
                                virt_to_page(zero_page), 0), 0);

  // Same for the next entry
  EXPECT_EQ(dm_bht_verify_block(bht_.get(), kBadBlock + bht_->node_count,
                                virt_to_page(zero_page), 0), 0);

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
  free(bad_hash_block);
  free(zero_page);
}

TEST_F(MemoryBhtTest, CreateThenVerifyBadDataBlock) {
  static const unsigned int total_blocks = 384;
  SetupBht(total_blocks, "sha256");
  // Set the root hash for a 0-filled image
  static const char kRootDigest[] =
    "45d65d6f9e5a962f4d80b5f1bd7a918152251c27bdad8c5f52b590c129833372";
  dm_bht_set_root_hexdigest(bht_.get(),
                            reinterpret_cast<const u8 *>(kRootDigest));
  // A corrupt page
  u8 *bad_page = (u8 *)my_memalign(PAGE_SIZE, PAGE_SIZE);

  memset(bad_page, 'A', PAGE_SIZE);


  EXPECT_LT(dm_bht_verify_block(bht_.get(), 0, virt_to_page(bad_page), 0), 0);
  EXPECT_LT(dm_bht_verify_block(bht_.get(), 127, virt_to_page(bad_page), 0), 0);
  EXPECT_LT(dm_bht_verify_block(bht_.get(), 128, virt_to_page(bad_page), 0), 0);
  EXPECT_LT(dm_bht_verify_block(bht_.get(), 255, virt_to_page(bad_page), 0), 0);
  EXPECT_LT(dm_bht_verify_block(bht_.get(), 256, virt_to_page(bad_page), 0), 0);
  EXPECT_LT(dm_bht_verify_block(bht_.get(), 383, virt_to_page(bad_page), 0), 0);

  EXPECT_EQ(0, dm_bht_destroy(bht_.get()));
  free(bad_page);
}
