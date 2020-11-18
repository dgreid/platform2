// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/cryptohome.h>
#include <brillo/data_encoding.h>
#include <dbus/bus.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "arc/data-snapshotd/dbus_adaptor.h"
#include "arc/data-snapshotd/file_utils.h"
#include "bootlockbox-client/bootlockbox/boot_lockbox_client.h"
// Note that boot_lockbox_rpc.pb.h have to be included before
// dbus_adaptors/org.chromium.BootLockboxInterface.h because it is used in
// there.
#include "bootlockbox/proto_bindings/boot_lockbox_rpc.pb.h"

#include "bootlockbox-client/bootlockbox/dbus-proxies.h"

using testing::_;
using testing::Eq;
using testing::Invoke;
using testing::Return;

namespace arc {
namespace data_snapshotd {

namespace {

constexpr char kRandomDir[] = "data";
constexpr char kRandomFile[] = "random file";
constexpr char kContent[] = "content";
constexpr char kFakeLastSnapshotPublicKey[] = "fake_public_key";
constexpr char kFakeAccountID[] = "fake_account_id";
constexpr char kFakeAccountID2[] = "fake_aacount_id_2";

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
    brillo::cryptohome::home::SetSystemSalt(&salt_);
  }

  void SetUp() override {
    EXPECT_TRUE(root_tempdir_.CreateUniqueTempDir());
    user_directory_ = root_tempdir_.GetPath().Append(hash(kFakeAccountID));
    EXPECT_TRUE(base::CreateDirectory(user_directory_));
    auto boot_lockbox_client =
        std::make_unique<testing::StrictMock<MockBootLockboxClient>>(bus_);
    boot_lockbox_client_ = boot_lockbox_client.get();
    dbus_adaptor_ = DBusAdaptor::CreateForTesting(
        root_tempdir_.GetPath(), root_tempdir_.GetPath(),
        std::move(boot_lockbox_client));
  }

  void TearDown() override {
    dbus_adaptor_.reset();
    EXPECT_TRUE(
        base::DeleteFile(root_tempdir_.GetPath(), true /* recursive */));
  }

  DBusAdaptor* dbus_adaptor() { return dbus_adaptor_.get(); }
  const base::FilePath& last_snapshot_dir() const {
    return dbus_adaptor_->get_last_snapshot_directory();
  }
  const base::FilePath& previous_snapshot_dir() const {
    return dbus_adaptor_->get_previous_snapshot_directory();
  }
  base::FilePath android_data_dir() const {
    return user_directory_.Append(kAndroidDataDirectory);
  }
  base::FilePath random_dir() const {
    return root_tempdir_.GetPath().Append(kRandomDir);
  }
  std::string hash(const std::string& account_id) const {
    return brillo::cryptohome::home::SanitizeUserName(account_id);
  }
  base::FilePath user_directory() const { return user_directory_; }

  // Creates |dir| and fills in with random content.
  void CreateDir(const base::FilePath& dir) {
    EXPECT_TRUE(base::CreateDirectory(dir));
    EXPECT_TRUE(base::CreateDirectory(dir.Append(kRandomDir)));
    EXPECT_TRUE(
        base::WriteFile(dir.Append(kRandomFile), kContent, strlen(kContent)));
  }

  MockBootLockboxClient* boot_lockbox_client() { return boot_lockbox_client_; }

 private:
  std::string salt_ = "salt";
  scoped_refptr<dbus::Bus> bus_;
  MockBootLockboxClient* boot_lockbox_client_;
  std::unique_ptr<DBusAdaptor> dbus_adaptor_;
  base::ScopedTempDir root_tempdir_;
  base::FilePath user_directory_;
};

TEST_F(DBusAdaptorTest, ClearSnapshotBasic) {
  CreateDir(last_snapshot_dir());
  EXPECT_TRUE(base::DirectoryExists(last_snapshot_dir()));

  CreateDir(previous_snapshot_dir());
  EXPECT_TRUE(base::DirectoryExists(previous_snapshot_dir()));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(false /* last */));
  EXPECT_FALSE(base::DirectoryExists(previous_snapshot_dir()));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(false /* last */));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(true /* last */));
  EXPECT_FALSE(base::DirectoryExists(last_snapshot_dir()));

  EXPECT_TRUE(dbus_adaptor()->ClearSnapshot(true /* last */));
}

// Test successful basic flow with no pre-existing snapshots.
TEST_F(DBusAdaptorTest, GenerateKeyPairBasic) {
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Return(true));
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());
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

  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());
  {
    SnapshotDirectory previous_dir;
    EXPECT_TRUE(ReadSnapshotDirectory(previous_snapshot_dir(), &previous_dir));
    std::vector<uint8_t> previous_hash =
        CalculateDirectoryCryptographicHash(previous_dir);
    EXPECT_FALSE(previous_hash.empty());
    // Check that the last snapshot has been moved to previous snapshot dir.
    EXPECT_EQ(previous_hash, last_hash);
  }
}

// Test successful flow with last snapshot key reading failure.
TEST_F(DBusAdaptorTest, GenerateKeyPairReadFailure) {
  CreateDir(last_snapshot_dir());

  // Attempt failure: last snapshot dir => previous snapshot dir.
  EXPECT_CALL(*boot_lockbox_client(), Read(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Return(false));
  EXPECT_CALL(*boot_lockbox_client(),
              Store(Eq(kLastSnapshotPublicKey), nEq("")))
      .WillOnce(Return(true));

  // Generating key pair should be still successful.
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());
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

  // Generating key pair should be still successful.
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());
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

  // Generating key pair should be still successful, because the last snapshot
  // will be re-generated anyway.
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());
}

// Test failure flow when storing freshly generated public key is failed.
TEST_F(DBusAdaptorTest, GenerateKeyPairStoreFailure) {
  // Fail once freshly generated public key storage is attempted.
  EXPECT_CALL(*boot_lockbox_client(),
              Store(Eq(kLastSnapshotPublicKey), nEq("")))
      .WillOnce(Return(false));

  EXPECT_FALSE(dbus_adaptor()->GenerateKeyPair());
}

// Test failure flow when the keys were not generated.
TEST_F(DBusAdaptorTest, TakeSnapshotNoPrivateKeyFailure) {
  EXPECT_FALSE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));
}

// Test failure flow when the last snapshot directory already exists.
TEST_F(DBusAdaptorTest, TakeSnapshotLastSnapshotExistFailure) {
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Return(true));
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());

  CreateDir(last_snapshot_dir());
  EXPECT_TRUE(base::DirectoryExists(last_snapshot_dir()));
  EXPECT_FALSE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));
}

// Test failure flow when android-data directory does not exist.
TEST_F(DBusAdaptorTest, TakeSnapshotAndroidDataDirNotExist) {
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Return(true));
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());
  EXPECT_FALSE(base::DirectoryExists(android_data_dir()));

  EXPECT_FALSE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));
}

// Test failure flow when android-data is file.
TEST_F(DBusAdaptorTest, TakeSnapshotAndroidDataNotDirFile) {
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Return(true));
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());
  // Create a file instead of android-data directory.
  EXPECT_TRUE(base::WriteFile(android_data_dir(), kContent, strlen(kContent)));
  EXPECT_TRUE(base::PathExists(android_data_dir()));
  EXPECT_FALSE(base::DirectoryExists(android_data_dir()));

  EXPECT_FALSE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));
}

// Test failure flow when android-data is a sym link.
TEST_F(DBusAdaptorTest, TakeSnapshotAndroidDataSymLink) {
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Return(true));
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());
  // Create a symlink.
  CreateDir(random_dir());
  EXPECT_TRUE(base::CreateSymbolicLink(random_dir(), android_data_dir()));
  EXPECT_TRUE(base::IsLink(android_data_dir()));
  EXPECT_TRUE(base::DirectoryExists(android_data_dir()));

  EXPECT_FALSE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));
}

// Test failure flow when android-data is a fifo.
TEST_F(DBusAdaptorTest, TakeSnapshotAndroidDataFiFo) {
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Return(true));
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());
  // Create a fifo android-data.
  mkfifo(android_data_dir().value().c_str(), 0666);
  EXPECT_TRUE(base::PathExists(android_data_dir()));
  EXPECT_FALSE(base::DirectoryExists(android_data_dir()));

  EXPECT_FALSE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));
}

// TODO(crbug.com/1149744) Enable test once bug is fixed.
// Test basic TakeSnapshot success flow.
TEST_F(DBusAdaptorTest, DISABLED_TakeSnapshotSuccess) {
  // In this test the copied snapshot directory is verified against the origin
  // android data directory. Inodes verification must be disabled, because the
  // inode values are changed after copying.
  // In production, it is not the case, because the directorys' integrity is
  // verified against itself and inode values should persist.
  dbus_adaptor()->set_inode_verification_enabled_for_testing(
      false /* enabled */);
  std::string expected_public_key_digest;
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Invoke([&expected_public_key_digest](
                           const std::string& key, const std::string& digest) {
        expected_public_key_digest = digest;
        return true;
      }));
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());

  CreateDir(android_data_dir());
  EXPECT_TRUE(base::DirectoryExists(android_data_dir()));
  // Store userhash to ensure that userhash stays the same.
  EXPECT_TRUE(StoreUserhash(android_data_dir(), hash(kFakeAccountID)));
  SnapshotDirectory android_dir;
  EXPECT_TRUE(ReadSnapshotDirectory(android_data_dir(), &android_dir,
                                    false /* inode_verification_enabled */));
  std::vector<uint8_t> android_data_hash =
      CalculateDirectoryCryptographicHash(android_dir);
  EXPECT_FALSE(android_data_hash.empty());

  EXPECT_TRUE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));
  EXPECT_TRUE(base::DirectoryExists(last_snapshot_dir()));
  SnapshotDirectory last_dir;
  EXPECT_TRUE(ReadSnapshotDirectory(last_snapshot_dir(), &last_dir,
                                    false /* inode_verification_enabled */));
  std::vector<uint8_t> last_snapshot_hash =
      CalculateDirectoryCryptographicHash(last_dir);
  EXPECT_FALSE(last_snapshot_hash.empty());
  EXPECT_EQ(android_data_hash, last_snapshot_hash);

  // Verification for another account ID should fail.
  EXPECT_FALSE(VerifyHash(last_snapshot_dir(), hash(kFakeAccountID2),
                          expected_public_key_digest,
                          false /* inode_verification_enabled */));
  EXPECT_TRUE(VerifyHash(last_snapshot_dir(), hash(kFakeAccountID),
                         expected_public_key_digest,
                         false /* inode_verification_enabled */));
  dbus_adaptor()->set_inode_verification_enabled_for_testing(
      true /* enabled */);
}

// Test failure flow if TakeSnapshot is invoked twice.
TEST_F(DBusAdaptorTest, TakeSnapshotDouble) {
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Return(true));
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());

  CreateDir(android_data_dir());
  EXPECT_TRUE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));

  CreateDir(android_data_dir());
  EXPECT_FALSE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));
}

// Test failure flow when user directory does not exist.
TEST_F(DBusAdaptorTest, LoadSnapshotNoAndroidDataDir) {
  CreateDir(last_snapshot_dir());
  CreateDir(previous_snapshot_dir());
  EXPECT_TRUE(base::DeleteFile(user_directory(), true /* recursive */));
  EXPECT_FALSE(base::DirectoryExists(user_directory()));

  bool last, success;
  dbus_adaptor()->LoadSnapshot(kFakeAccountID, &last, &success);
  EXPECT_FALSE(success);
}

// Test failure when snapshot directory does not exist.
TEST_F(DBusAdaptorTest, LoadSnapshotNoSnapshot) {
  CreateDir(user_directory());
  EXPECT_TRUE(base::DirectoryExists(user_directory()));
  EXPECT_FALSE(base::DirectoryExists(last_snapshot_dir()));
  EXPECT_FALSE(base::DirectoryExists(previous_snapshot_dir()));
  bool last, success;
  dbus_adaptor()->LoadSnapshot(kFakeAccountID, &last, &success);
  EXPECT_FALSE(success);
}

// Test failure when public key is not stored in BootLockbox.
TEST_F(DBusAdaptorTest, LoadSnapshotNoPublicKey) {
  CreateDir(user_directory());
  EXPECT_TRUE(base::DirectoryExists(user_directory()));

  CreateDir(last_snapshot_dir());
  EXPECT_TRUE(base::DirectoryExists(last_snapshot_dir()));
  EXPECT_FALSE(base::DirectoryExists(previous_snapshot_dir()));

  EXPECT_CALL(*boot_lockbox_client(), Read(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Return(false));

  bool last, success;
  dbus_adaptor()->LoadSnapshot(kFakeAccountID, &last, &success);
  EXPECT_FALSE(success);
}

// Test failure when empty public key is stored in BootLockbox.
TEST_F(DBusAdaptorTest, LoadSnapshotEmptyPublicKey) {
  CreateDir(user_directory());
  EXPECT_TRUE(base::DirectoryExists(user_directory()));

  CreateDir(last_snapshot_dir());
  EXPECT_TRUE(base::DirectoryExists(last_snapshot_dir()));
  EXPECT_FALSE(base::DirectoryExists(previous_snapshot_dir()));

  EXPECT_CALL(*boot_lockbox_client(), Read(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Invoke([](const std::string& key, std::string* value) {
        *value = "";
        return true;
      }));
  bool last, success;
  dbus_adaptor()->LoadSnapshot(kFakeAccountID, &last, &success);
  EXPECT_FALSE(success);
}

// Test failure when snapshot verification fails.
TEST_F(DBusAdaptorTest, LoadSnapshotVerificationFailure) {
  CreateDir(user_directory());
  EXPECT_TRUE(base::DirectoryExists(user_directory()));

  CreateDir(last_snapshot_dir());
  EXPECT_TRUE(base::DirectoryExists(last_snapshot_dir()));
  EXPECT_FALSE(base::DirectoryExists(previous_snapshot_dir()));

  EXPECT_CALL(*boot_lockbox_client(), Read(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Invoke([](const std::string& key, std::string* value) {
        *value = kFakeLastSnapshotPublicKey;
        return true;
      }));
  bool last, success;
  dbus_adaptor()->LoadSnapshot(kFakeAccountID, &last, &success);
  EXPECT_FALSE(success);
}

// Test failure when snapshot is loaded for unknown user.
TEST_F(DBusAdaptorTest, LoadSnapshotUnknownUser) {
  // In this test the copied snapshot directory is verified against the origin
  // snapshot directory. Inodes verification must be disabled, because the
  // inode values are changed after copying.
  // In production, it is not the case, because the directorys' integrity is
  // verified against itself and inode values should persist.
  dbus_adaptor()->set_inode_verification_enabled_for_testing(
      false /* enabled */);
  std::string expected_public_key_digest;
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Invoke([&expected_public_key_digest](
                           const std::string& key, const std::string& digest) {
        expected_public_key_digest = digest;
        return true;
      }));
  // Generate key pair.
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());

  // Create android-data directory.
  CreateDir(android_data_dir());
  EXPECT_TRUE(base::DirectoryExists(android_data_dir()));

  // Take a snapshot.
  EXPECT_TRUE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));
  EXPECT_TRUE(base::DirectoryExists(last_snapshot_dir()));
  // Verify taken snapshot with disabled inode verification.
  EXPECT_TRUE(VerifyHash(last_snapshot_dir(), hash(kFakeAccountID),
                         expected_public_key_digest,
                         false /* inode_verification_enabled */));

  // Load a snapshot directory to android-data for unknown user.
  bool last, success;
  dbus_adaptor()->LoadSnapshot(kFakeAccountID2, &last, &success);
  EXPECT_FALSE(success);

  dbus_adaptor()->set_inode_verification_enabled_for_testing(
      true /* enabled */);
}

// Test basic success flow.
TEST_F(DBusAdaptorTest, LoadSnapshotSuccess) {
  // In this test the copied snapshot directory is verified against the origin
  // snapshot directory. Inodes verification must be disabled, because the
  // inode values are changed after copying.
  // In production, it is not the case, because the directorys' integrity is
  // verified against itself and inode values should persist.
  dbus_adaptor()->set_inode_verification_enabled_for_testing(
      false /* enabled */);
  std::string expected_public_key_digest;
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Invoke([&expected_public_key_digest](
                           const std::string& key, const std::string& digest) {
        expected_public_key_digest = digest;
        return true;
      }));
  // Generate key pair.
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());

  // Create android-data directory.
  CreateDir(android_data_dir());
  EXPECT_TRUE(base::DirectoryExists(android_data_dir()));

  // Take a snapshot.
  EXPECT_TRUE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));
  EXPECT_TRUE(base::DirectoryExists(last_snapshot_dir()));
  // Verify taken snapshot with disabled inode verification.
  EXPECT_TRUE(VerifyHash(last_snapshot_dir(), hash(kFakeAccountID),
                         expected_public_key_digest,
                         false /* inode_verification_enabled */));

  // Remove android-data directory to be able to load a snapshot.
  EXPECT_TRUE(base::DeleteFile(android_data_dir(), true /* recursive */));
  EXPECT_FALSE(base::DirectoryExists(android_data_dir()));

  EXPECT_CALL(*boot_lockbox_client(), Read(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Invoke([expected_public_key_digest](const std::string& key,
                                                    std::string* value) {
        *value = expected_public_key_digest;
        return true;
      }));
  // Load a snapshot directory to android-data.
  bool last, success;
  dbus_adaptor()->LoadSnapshot(kFakeAccountID, &last, &success);
  EXPECT_TRUE(success);

  // Verify the integrity of the last snapshot with disabld inode verification.
  EXPECT_TRUE(last);
  EXPECT_TRUE(VerifyHash(android_data_dir(), hash(kFakeAccountID),
                         expected_public_key_digest,
                         false /* inode_verification_enabled */));
  dbus_adaptor()->set_inode_verification_enabled_for_testing(
      true /* enabled */);
}

// Test success flow when loading of last snapshot fails, but loading of
// previous snapshot succeeds.
TEST_F(DBusAdaptorTest, LoadSnapshotPreviousSuccess) {
  // In this test the copied snapshot directory is verified against the origin
  // snapshot directory. Inodes verification must be disabled, because the
  // inode values are changed after copying.
  // In production, it is not the case, because the directorys' integrity is
  // verified against itself and inode values should persist.
  dbus_adaptor()->set_inode_verification_enabled_for_testing(
      false /* enabled */);
  std::string expected_public_key_digest = "";
  EXPECT_CALL(*boot_lockbox_client(),
              Store(Eq(kLastSnapshotPublicKey), nEq("")))
      .Times(2)
      .WillRepeatedly(
          Invoke([&expected_public_key_digest](const std::string& key,
                                               const std::string& digest) {
            if (expected_public_key_digest.empty()) {
              expected_public_key_digest = digest;
            }
            return true;
          }));
  // First time snapshot generating flow.
  // Generate a key pair.
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());

  CreateDir(android_data_dir());
  EXPECT_TRUE(base::DirectoryExists(android_data_dir()));

  // Take android-data snapshot and name it as a last snapshot.
  EXPECT_TRUE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));
  EXPECT_TRUE(base::DirectoryExists(last_snapshot_dir()));
  EXPECT_TRUE(VerifyHash(last_snapshot_dir(), hash(kFakeAccountID),
                         expected_public_key_digest,
                         false /* inode_verification_enabled */));

  EXPECT_CALL(*boot_lockbox_client(), Read(Eq(kLastSnapshotPublicKey), _))
      .WillOnce(Invoke([expected_public_key_digest](const std::string& key,
                                                    std::string* value) {
        *value = expected_public_key_digest;
        return true;
      }));
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kPreviousSnapshotPublicKey), _))
      .WillOnce(Return(true));
  EXPECT_CALL(*boot_lockbox_client(), Store(Eq(kLastSnapshotPublicKey), Eq("")))
      .WillOnce(Return(true));
  // Second time snapshot generating flow.
  // Previous snapshot has been generated during the first flow.
  EXPECT_TRUE(dbus_adaptor()->GenerateKeyPair());
  EXPECT_TRUE(base::DirectoryExists(previous_snapshot_dir()));

  // Take a snapshot.
  EXPECT_TRUE(base::DirectoryExists(android_data_dir()));
  EXPECT_TRUE(dbus_adaptor()->TakeSnapshot(kFakeAccountID));

  EXPECT_CALL(*boot_lockbox_client(), Read(Eq(kPreviousSnapshotPublicKey), _))
      .WillOnce(Invoke([expected_public_key_digest](const std::string& key,
                                                    std::string* value) {
        *value = expected_public_key_digest;
        return true;
      }));
  // Invalidate the last snapshot.
  EXPECT_TRUE(base::DeleteFile(last_snapshot_dir(), true /* recursive */));
  // Remove android-data directory to be able to load a snapshot.
  EXPECT_TRUE(base::DeleteFile(android_data_dir(), true /* recursive */));
  // Load the previous snapshot, because the last one is invalid.
  bool last, success;
  dbus_adaptor()->LoadSnapshot(kFakeAccountID, &last, &success);
  EXPECT_TRUE(success);
  EXPECT_FALSE(last);
  EXPECT_TRUE(VerifyHash(android_data_dir(), hash(kFakeAccountID),
                         expected_public_key_digest,
                         false /* inode_verification_enabled */));
  dbus_adaptor()->set_inode_verification_enabled_for_testing(
      true /* enabled */);
}

}  // namespace data_snapshotd
}  // namespace arc
