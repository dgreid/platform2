// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/cellular/pending_activation_store.h"

#include <utility>

#include <base/files/scoped_temp_dir.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shill/mock_store.h"
#include "shill/store_interface.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::Mock;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace shill {

class PendingActivationStoreTest : public ::testing::Test {
 public:
  PendingActivationStoreTest() : mock_store_(new MockStore()) {}

 protected:
  void SetMockStore() { store_.storage_ = std::move(mock_store_); }

  std::unique_ptr<MockStore> mock_store_;
  PendingActivationStore store_;
};

TEST_F(PendingActivationStoreTest, FileInteractions) {
  const char kEntry1[] = "1234";
  const char kEntry2[] = "4321";

  base::ScopedTempDir temp_dir;
  EXPECT_TRUE(temp_dir.CreateUniqueTempDir());

  EXPECT_TRUE(store_.InitStorage(temp_dir.GetPath()));

  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry1));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry2));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry1));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry2));

  EXPECT_TRUE(store_.SetActivationState(
      PendingActivationStore::kIdentifierICCID, kEntry1,
      PendingActivationStore::kStatePending));
  EXPECT_TRUE(store_.SetActivationState(
      PendingActivationStore::kIdentifierICCID, kEntry2,
      PendingActivationStore::kStateActivated));

  EXPECT_EQ(PendingActivationStore::kStatePending,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry1));
  EXPECT_EQ(PendingActivationStore::kStateActivated,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry2));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry1));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry2));

  EXPECT_TRUE(store_.SetActivationState(
      PendingActivationStore::kIdentifierMEID, kEntry1,
      PendingActivationStore::kStateActivated));

  EXPECT_EQ(PendingActivationStore::kStatePending,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry1));
  EXPECT_EQ(PendingActivationStore::kStateActivated,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry2));
  EXPECT_EQ(PendingActivationStore::kStateActivated,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry1));

  EXPECT_TRUE(store_.SetActivationState(
      PendingActivationStore::kIdentifierICCID, kEntry1,
      PendingActivationStore::kStateActivated));
  EXPECT_TRUE(store_.SetActivationState(
      PendingActivationStore::kIdentifierICCID, kEntry2,
      PendingActivationStore::kStatePending));

  EXPECT_EQ(PendingActivationStore::kStateActivated,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry1));
  EXPECT_EQ(PendingActivationStore::kStatePending,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry2));

  // Close and reopen the file to verify that the entries persisted.
  EXPECT_TRUE(store_.InitStorage(temp_dir.GetPath()));

  EXPECT_EQ(PendingActivationStore::kStateActivated,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry1));
  EXPECT_EQ(PendingActivationStore::kStatePending,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry2));
  EXPECT_EQ(PendingActivationStore::kStateActivated,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry1));

  EXPECT_TRUE(
      store_.RemoveEntry(PendingActivationStore::kIdentifierMEID, kEntry1));
  EXPECT_TRUE(
      store_.RemoveEntry(PendingActivationStore::kIdentifierICCID, kEntry2));

  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry1));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry2));
  EXPECT_EQ(PendingActivationStore::kStateActivated,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry1));

  EXPECT_TRUE(
      store_.RemoveEntry(PendingActivationStore::kIdentifierICCID, kEntry1));
  EXPECT_FALSE(
      store_.RemoveEntry(PendingActivationStore::kIdentifierMEID, kEntry2));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry1));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry2));

  EXPECT_TRUE(store_.InitStorage(temp_dir.GetPath()));

  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry1));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry2));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry1));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry2));
}

TEST_F(PendingActivationStoreTest, GetActivationState) {
  MockStore* mock_store = mock_store_.get();
  SetMockStore();

  const char kEntry[] = "12345689";

  // Value not found
  EXPECT_CALL(*mock_store,
              GetInt(PendingActivationStore::kIccidGroupId, kEntry, _))
      .WillOnce(Return(false));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry));

  // File contains invalid entry
  EXPECT_CALL(*mock_store,
              GetInt(PendingActivationStore::kMeidGroupId, kEntry, _))
      .WillOnce(DoAll(
          SetArgPointee<2>(static_cast<int>(PendingActivationStore::kStateMax)),
          Return(true)));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry));
  EXPECT_CALL(*mock_store,
              GetInt(PendingActivationStore::kMeidGroupId, kEntry, _))
      .WillOnce(DoAll(SetArgPointee<2>(0), Return(true)));
  EXPECT_EQ(PendingActivationStore::kStateUnknown,
            store_.GetActivationState(PendingActivationStore::kIdentifierMEID,
                                      kEntry));
  Mock::VerifyAndClearExpectations(mock_store);

  // All enum values
  EXPECT_CALL(*mock_store,
              GetInt(PendingActivationStore::kIccidGroupId, kEntry, _))
      .WillOnce(DoAll(SetArgPointee<2>(1), Return(true)));
  EXPECT_EQ(PendingActivationStore::kStatePending,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry));
  EXPECT_CALL(*mock_store,
              GetInt(PendingActivationStore::kIccidGroupId, kEntry, _))
      .WillOnce(DoAll(SetArgPointee<2>(2), Return(true)));
  EXPECT_EQ(PendingActivationStore::kStateActivated,
            store_.GetActivationState(PendingActivationStore::kIdentifierICCID,
                                      kEntry));
  Mock::VerifyAndClearExpectations(mock_store);
}

TEST_F(PendingActivationStoreTest, SetActivationState) {
  MockStore* mock_store = mock_store_.get();
  SetMockStore();

  const char kEntry[] = "12345689";

  EXPECT_CALL(*mock_store, Flush()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_store,
              SetInt(PendingActivationStore::kIccidGroupId, kEntry, _))
      .WillOnce(Return(false));
  EXPECT_FALSE(
      store_.SetActivationState(PendingActivationStore::kIdentifierICCID,
                                kEntry, PendingActivationStore::kStateUnknown));
  EXPECT_FALSE(
      store_.SetActivationState(PendingActivationStore::kIdentifierICCID,
                                kEntry, PendingActivationStore::kStateUnknown));
  EXPECT_FALSE(
      store_.SetActivationState(PendingActivationStore::kIdentifierICCID,
                                kEntry, PendingActivationStore::kStatePending));

  EXPECT_CALL(*mock_store,
              SetInt(PendingActivationStore::kIccidGroupId, kEntry, _))
      .WillRepeatedly(Return(true));
  EXPECT_FALSE(store_.SetActivationState(
      PendingActivationStore::kIdentifierICCID, kEntry,
      static_cast<PendingActivationStore::State>(-1)));
  EXPECT_FALSE(
      store_.SetActivationState(PendingActivationStore::kIdentifierICCID,
                                kEntry, PendingActivationStore::kStateMax));
  EXPECT_FALSE(
      store_.SetActivationState(PendingActivationStore::kIdentifierICCID,
                                kEntry, PendingActivationStore::kStateUnknown));
  EXPECT_TRUE(
      store_.SetActivationState(PendingActivationStore::kIdentifierICCID,
                                kEntry, PendingActivationStore::kStatePending));
  EXPECT_TRUE(store_.SetActivationState(
      PendingActivationStore::kIdentifierICCID, kEntry,
      PendingActivationStore::kStateActivated));
}

TEST_F(PendingActivationStoreTest, RemoveEntry) {
  MockStore* mock_store = mock_store_.get();
  SetMockStore();

  const char kEntry[] = "12345689";

  EXPECT_CALL(*mock_store, Flush()).WillRepeatedly(Return(true));
  EXPECT_CALL(*mock_store,
              DeleteKey(PendingActivationStore::kIccidGroupId, kEntry))
      .WillOnce(Return(false));
  EXPECT_FALSE(
      store_.RemoveEntry(PendingActivationStore::kIdentifierICCID, kEntry));
  EXPECT_CALL(*mock_store,
              DeleteKey(PendingActivationStore::kIccidGroupId, kEntry))
      .WillOnce(Return(true));
  EXPECT_TRUE(
      store_.RemoveEntry(PendingActivationStore::kIdentifierICCID, kEntry));
}

}  // namespace shill
