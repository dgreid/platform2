// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/data-snapshotd/dbus_adaptor.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <crypto/scoped_openssl_types.h>
#include <crypto/rsa_private_key.h>
#include <openssl/sha.h>

#include "arc/data-snapshotd/file_utils.h"
#include "bootlockbox-client/bootlockbox/boot_lockbox_client.h"

namespace arc {
namespace data_snapshotd {

namespace {

// Snapshot paths:
constexpr char kCommonSnapshotPath[] =
    "/mnt/stateful_partition/unencrypted/arc-data-snapshot/";
constexpr char kLastSnapshotPath[] = "last";
constexpr char kPreviousSnapshotPath[] = "previous";

}  // namespace

// BootLockbox snapshot keys:
const char kLastSnapshotPublicKey[] = "snapshot_public_key_last";
const char kPreviousSnapshotPublicKey[] = "snapshot_public_key_previous";

DBusAdaptor::DBusAdaptor()
    : DBusAdaptor(base::FilePath(kCommonSnapshotPath),
                  cryptohome::BootLockboxClient::CreateBootLockboxClient()) {}

DBusAdaptor::~DBusAdaptor() = default;

// static
std::unique_ptr<DBusAdaptor> DBusAdaptor::CreateForTesting(
    const base::FilePath& snapshot_directory,
    std::unique_ptr<cryptohome::BootLockboxClient> boot_lockbox_client) {
  return base::WrapUnique(
      new DBusAdaptor(snapshot_directory, std::move(boot_lockbox_client)));
}

void DBusAdaptor::RegisterAsync(
    const scoped_refptr<dbus::Bus>& bus,
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      nullptr /* object_manager */, bus, GetObjectPath()),
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(sequencer->GetHandler(
      "Failed to register D-Bus object" /* descriptive_message */,
      true /* failure_is_fatal */));
}

bool DBusAdaptor::GenerateKeyPair(brillo::ErrorPtr* error) {
  // TODO(b/160387490): Implement showing a spinner screen.
  std::string last_public_key_digest;
  // Try to move last snapshot to previous for consistency.
  if (base::PathExists(last_snapshot_directory_) &&
      boot_lockbox_client_->Read(kLastSnapshotPublicKey,
                                 &last_public_key_digest) &&
      !last_public_key_digest.empty()) {
    if (boot_lockbox_client_->Store(kPreviousSnapshotPublicKey,
                                    last_public_key_digest) &&
        ClearSnapshot(error, false /* last */) &&
        base::Move(last_snapshot_directory_, previous_snapshot_directory_)) {
      boot_lockbox_client_->Store(kLastSnapshotPublicKey, "");
    } else {
      LOG(ERROR) << "Failed to move last to previous snapshot.";
    }
  }
  // Clear last snapshot - a new one will be created soon.
  ClearSnapshot(error, true /* last */);

  // Generate a key pair.
  public_key_info_.clear();
  std::unique_ptr<crypto::RSAPrivateKey> generated_private_key(
      crypto::RSAPrivateKey::Create(1024));
  if (!generated_private_key) {
    LOG(ERROR) << "Failed to generate a key pair.";
    return false;
  }
  if (!generated_private_key->ExportPublicKey(&public_key_info_)) {
    LOG(ERROR) << "Failed to export public key";
    return false;
  }

  // Store a new public key digest.
  std::vector<uint8_t> digest;
  digest.resize(SHA256_DIGEST_LENGTH);
  if (!SHA256((const unsigned char*)public_key_info_.data(),
              public_key_info_.size(), digest.data()) ||
      digest.empty()) {
    LOG(ERROR) << "Failed to calculate digest of public key.";
    return false;
  }
  if (!boot_lockbox_client_->Store(kLastSnapshotPublicKey,
                                   std::string(digest.begin(), digest.end()))) {
    LOG(ERROR) << "Failed to store a public key in BootLockbox.";
    return false;
  }
  if (!boot_lockbox_client_->Finalize()) {
    LOG(ERROR) << "Failed to finalize a BootLockbox.";
    return false;
  }
  // Save private key for later usage.
  private_key_ = std::move(generated_private_key);
  return true;
}

bool DBusAdaptor::ClearSnapshot(brillo::ErrorPtr* error, bool last) {
  base::FilePath dir(last ? last_snapshot_directory_
                          : previous_snapshot_directory_);
  if (!base::DirectoryExists(dir)) {
    LOG(WARNING) << "Snapshot directory is already empty: " << dir.value();
    return true;
  }
  if (!base::DeleteFile(dir, true /* recursive */)) {
    LOG(ERROR) << "Failed to delete snapshot directory: " << dir.value();
    return false;
  }
  return true;
}

DBusAdaptor::DBusAdaptor(
    const base::FilePath& snapshot_directory,
    std::unique_ptr<cryptohome::BootLockboxClient> boot_lockbox_client)
    : org::chromium::ArcDataSnapshotdAdaptor(this),
      last_snapshot_directory_(snapshot_directory.Append(kLastSnapshotPath)),
      previous_snapshot_directory_(
          snapshot_directory.Append(kPreviousSnapshotPath)),
      boot_lockbox_client_(std::move(boot_lockbox_client)) {
  DCHECK(boot_lockbox_client_);
}

}  // namespace data_snapshotd
}  // namespace arc
