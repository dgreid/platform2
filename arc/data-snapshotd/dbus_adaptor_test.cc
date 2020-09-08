// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <dbus/bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arc/data-snapshotd/dbus_adaptor.h"
#include "arc/data-snapshotd/file_utils.h"
#include "cryptohome/bootlockbox/boot_lockbox_client.h"
// Note that boot_lockbox_rpc.pb.h have to be included before
// dbus_adaptors/org.chromium.BootLockboxInterface.h because it is used in
// there.
#include "cryptohome/bootlockbox/boot_lockbox_rpc.pb.h"

#include "bootlockbox/dbus-proxies.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::Return;

namespace arc {
namespace data_snapshotd {

namespace {

constexpr char kRandomDir[] = "data";
constexpr char kRandomFile[] = "hash";
constexpr char kContent[] = "content";
constexpr char kFakeLastSnapshotPublicKey[] = "fake_public_key";

MATCHER_P(nEq, expected, "") {
  return expected != arg;
}

}  // namespace

class MockBootLockboxClient : public cryptohome::BootLockboxClient {
 public:
  explicit MockBootLockboxClient(scoped_refptr<dbus::Bus> bus)
      : BootLockboxClient(
            std::make_unique<org::chromium::BootLockboxInterfaceProxy>(bus),
            bus) {}
  ~MockBootLockboxClient() override = default;

  MOCK_METHOD(bool,
              Store,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(bool, Read, (const std::string&, std::string*), (override));
  MOCK_METHOD(bool, Finalize, (), (override));
};

class DBusAdaptorTest : public testing::Test {
 public:
  DBusAdaptorTest() : bus_(new dbus::Bus{dbus::Bus::Options{}}) {
    EXPECT_TRUE(root_tempdir_.CreateUniqueTempDir());
    auto boot_lockbox_client =
        std::make_unique<testing::StrictMock<MockBootLockboxClient>>(bus_);
    boot_lockbox_client_ = boot_lockbox_client.get();
    dbus_adaptor_ = DBusAdaptor::CreateForTesting(
        root_tempdir_.GetPath(), std::move(boot_lockbox_client));
  }
  DBusAdaptor* dbus_adaptor() { return dbus_adaptor_.get(); }
  const base::FilePath& last_snapshot_dir() {
    return dbus_adaptor_->get_last_snapshot_directory();
  }
  const base::FilePath& previous_snapshot_dir() {
    return dbus_adaptor_->get_previous_snapshot_directory();
  }
  // Creates |dir| and fills in with random content.
  void CreateDir(const base::FilePath& dir) {
    EXPECT_TRUE(base::CreateDirectory(dir));
    EXPECT_TRUE(base::CreateDirectory(dir.Append(kRandomDir)));
    EXPECT_TRUE(
        base::WriteFile(dir.Append(kRandomFile), kContent, strlen(kContent)));
  }

  MockBootLockboxClient* boot_lockbox_client() { return boot_lockbox_client_; }

 private:
  scoped_refptr<dbus::Bus> bus_;
  MockBootLockboxClient* boot_lockbox_client_;
  std::unique_ptr<DBusAdaptor> dbus_adaptor_;
  base::ScopedTempDir root_tempdir_;
};

TEST_F(DBusAdaptorTest, ClearSnapshotBasic) {
  CreateDir(last_snapshot_dir());
  EXPECT_TRUE(base::DirectoryExists(last_snapshot_dir()));

  CreateDir(previous_snapshot_dir());
  EXPECT_TRUE(base::DirectoryExists(previous_snapshot_dir()));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(nullptr, false /* last */));
  EXPECT_FALSE(base::DirectoryExists(previous_snapshot_dir()));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(nullptr, false /* last */));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(nullptr, true /* last */));
  EXPECT_FALSE(base::DirectoryExists(last_snapshot_dir()));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(nullptr, true /* last */));
}

// Test successful basic flow with no pre-existing snapshots.
TEST_F(DBusAdaptorTest, GenerateKeyPairBasic) {
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*boot_lockbox_client(), Finalize()).WillOnce(Return(true));
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair(nullptr));
}

// Test successful basic flow with pre-existing snapshots.
TEST_F(DBusAdaptorTest, GenerateKeyPairExisting) {
  CreateDir(last_snapshot_dir());
  CreateDir(previous_snapshot_dir());

  SnapshotDirectory last_dir;
  EXPECT_TRUE(ReadSnapshotDirectory(last_snapshot_dir(), &last_dir));
  std::vector<uint8_t> last_hash =
      CalculateDirectoryCryptographicHash(last_dir);
  EXPECT_FALSE(last_hash.empty());

  {
    SnapshotDirectory previous_dir;
    EXPECT_TRUE(ReadSnapshotDirectory(previous_snapshot_dir(), &previous_dir));
    std::vector<uint8_t> previous_hash =
        CalculateDirectoryCryptographicHash(previous_dir);
    EXPECT_FALSE(previous_hash.empty());

    EXPECT_NE(previous_dir.DebugString(), last_dir.DebugString());
    EXPECT_NE(last_hash, previous_hash);
  }

  // Last snapshot dir => previous snapshot dir.
  EXPECT_CALL(*boot_lockbox_client(), Read(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Invoke([](const std::string& key, std::string* value) {
        *value = kFakeLastSnapshotPublicKey;
        return true;
      }));
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kPreviousSnapshotPublicKey),
                                            Eq(kFakeLastSnapshotPublicKey)))
      .WillOnce(Return(true));
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), Eq("")))
      .WillOnce(Return(true));
  EXPECT_CALL(*boot_lockbox_client(),
              Store(Eq(kLastSnapshotPublicKey), nEq("")))
      .WillOnce(Return(true));
  EXPECT_CALL(*boot_lockbox_client(), Finalize()).WillOnce(Return(true));

  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair(nullptr));
  {
    SnapshotDirectory previous_dir;
    EXPECT_TRUE(ReadSnapshotDirectory(previous_snapshot_dir(), &previous_dir));
    std::vector<uint8_t> previous_hash =
        CalculateDirectoryCryptographicHash(previous_dir);
    EXPECT_FALSE(previous_hash.empty());
    // Check that the last snapshot has been moved to previous snapshot dir.
    EXPECT_EQ(previous_hash, last_hash);
    EXPECT_EQ(previous_dir.DebugString(), last_dir.DebugString());
  }
}

// Test successful flow with last snapshot key reading failure.
TEST_F(DBusAdaptorTest, GenerateKeyPairReadFailure) {
  CreateDir(last_snapshot_dir());

  // Attempt failure: last snapshot dir => previous snapshot dir.
  EXPECT_CALL(*boot_lockbox_client(), Read(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Invoke(
          [](const std::string& key, std::string* value) { return false; }));
  EXPECT_CALL(*boot_lockbox_client(),
              Store(Eq(kLastSnapshotPublicKey), nEq("")))
      .WillOnce(Return(true));
  EXPECT_CALL(*boot_lockbox_client(), Finalize()).WillOnce(Return(true));

  // Generating key pair should be still successful.
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair(nullptr));
}

// Test successful flow with pre-existing last snapshot empty key.
TEST_F(DBusAdaptorTest, GenerateKeyPairReadEmpty) {
  CreateDir(last_snapshot_dir());

  // Attempt failure: last snapshot dir => previous snapshot dir.
  EXPECT_CALL(*boot_lockbox_client(), Read(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Invoke([](const std::string& key, std::string* value) {
        *value = "";
        return true;
      }));
  EXPECT_CALL(*boot_lockbox_client(),
              Store(Eq(kLastSnapshotPublicKey), nEq("")))
      .WillOnce(Return(true));
  EXPECT_CALL(*boot_lockbox_client(), Finalize()).WillOnce(Return(true));

  // Generating key pair should be still successful.
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair(nullptr));
}

// Test success flow with pre-existing snapshots and moving error.
TEST_F(DBusAdaptorTest, GenerateKeyPairMoveError) {
  CreateDir(last_snapshot_dir());

  // Last snapshot dir => previous snapshot dir.
  EXPECT_CALL(*boot_lockbox_client(), Read(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Invoke([](const std::string& key, std::string* value) {
        *value = kFakeLastSnapshotPublicKey;
        return true;
      }));
  // Fail to move last snapshot public key to previous.
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kPreviousSnapshotPublicKey),
                                            Eq(kFakeLastSnapshotPublicKey)))
      .WillOnce(Return(false));
  EXPECT_CALL(*boot_lockbox_client(),
              Store(Eq(kLastSnapshotPublicKey), nEq("")))
      .WillOnce(Return(true));
  EXPECT_CALL(*boot_lockbox_client(), Finalize()).WillOnce(Return(true));

  // Generating key pair should be still successful, because the last snapshot
  // will be re-generated anyway.
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair(nullptr));
}

// Test failure flow when storing freshly generated public key is failed.
TEST_F(DBusAdaptorTest, GenerateKeyPairStoreFailure) {
  // Fail once freshly generated public key storage is attempted.
  EXPECT_CALL(*boot_lockbox_client(),
              Store(Eq(kLastSnapshotPublicKey), nEq("")))
      .WillOnce(Return(false));

  EXPECT_FALSE(dbus_adaptor()->GenerateKeyPair(nullptr));
}

// Test failure flow when finalizing BootLockbox is attempted.
TEST_F(DBusAdaptorTest, GenerateKeyPairFinalizeFailure) {
  EXPECT_CALL(*boot_lockbox_client(),
              Store(Eq(kLastSnapshotPublicKey), nEq("")))
      .WillOnce(Return(true));
  // Fail once the finalization of BootLockbox is attempted.
  EXPECT_CALL(*boot_lockbox_client(), Finalize()).WillOnce(Return(false));

  EXPECT_FALSE(dbus_adaptor()->GenerateKeyPair(nullptr));
}

}  // namespace data_snapshotd
}  // namespace arc
