// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_DATA_SNAPSHOTD_DBUS_ADAPTOR_H_
#define ARC_DATA_SNAPSHOTD_DBUS_ADAPTOR_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/scoped_refptr.h>
#include <brillo/dbus/async_event_sequencer.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/bus.h>

#include "dbus_adaptors/org.chromium.ArcDataSnapshotd.h"

namespace crypto {

class RSAPrivateKey;

}  // namespace crypto

namespace cryptohome {

class BootLockboxClient;

}  // namespace cryptohome

namespace arc {
namespace data_snapshotd {

// BootLockbox snapshot keys:
extern const char kLastSnapshotPublicKey[];
extern const char kPreviousSnapshotPublicKey[];
// Android data directory name:
extern const char kAndroidDataDirectory[];

// Implements the "org.chromium.ArcDataSnapshotdInterface" D-Bus interface
// exposed by the arc-data-snapshotd daemon (see constants for the API methods
// at src/platform/system_api/dbus/arc-data-snapshotd/dbus-constants.h).
class DBusAdaptor final : public org::chromium::ArcDataSnapshotdAdaptor,
                          public org::chromium::ArcDataSnapshotdInterface {
 public:
  DBusAdaptor();
  DBusAdaptor(const DBusAdaptor&) = delete;
  DBusAdaptor& operator=(const DBusAdaptor&) = delete;
  ~DBusAdaptor() override;

  static std::unique_ptr<DBusAdaptor> CreateForTesting(
      const base::FilePath& snapshot_directory,
      const base::FilePath& home_root_directory,
      std::unique_ptr<cryptohome::BootLockboxClient> boot_lockbox_client);

  // Registers the D-Bus object that the arc-data-snapshotd daemon exposes and
  // ties methods exposed by this object with the actual implementation.
  void RegisterAsync(const scoped_refptr<dbus::Bus>& bus,
                     brillo::dbus_utils::AsyncEventSequencer* sequencer);

  // Implementation of the "org.chromium.ArcDataSnapshotdInterface" D-Bus
  // interface:
  bool GenerateKeyPair() override;
  bool ClearSnapshot(bool last) override;
  bool TakeSnapshot(const std::string& account_id) override;
  void LoadSnapshot(const std::string& account_id,
                    bool* last,
                    bool* success) override;

  const base::FilePath& get_last_snapshot_directory() const {
    return last_snapshot_directory_;
  }
  const base::FilePath& get_previous_snapshot_directory() const {
    return previous_snapshot_directory_;
  }

  // Use this method only for testing.
  // Inode verification of snapshot directory is enabled in production by
  // default.
  // In production the integrity of the persisting snapshot directory is
  // verified, inode values should stay the same.
  //
  // Using this method, the inode verification for snapshot directories can be
  // disabled for testing. It is needed to ensure the integrity of snapshot
  // directories after copying it (inodes change).
  void set_inode_verification_enabled_for_testing(bool enabled) {
    inode_verification_enabled_ = enabled;
  }

 private:
  // Tries to load a snapshot stored in |snapshot_dir| to |android_data_dir|
  // and verify the snapshot by the public key digest stored in BootLockbox by
  // |boot_lockbox_key|.
  // Returns false in case of any error.
  bool TryToLoadSnapshot(const std::string& userhash,
                         const base::FilePath& snapshot_dir,
                         const base::FilePath& android_data_dir,
                         const std::string& boot_lockbox_key);
  DBusAdaptor(
      const base::FilePath& snapshot_directory,
      const base::FilePath& home_root_directory,
      std::unique_ptr<cryptohome::BootLockboxClient> boot_lockbox_client);

  // Manages the D-Bus interfaces exposed by the arc-data-snapshotd daemon.
  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;

  // Snapshot directory paths:
  const base::FilePath last_snapshot_directory_;
  const base::FilePath previous_snapshot_directory_;
  // Home root directory.
  const base::FilePath home_root_directory_;

  // Manages the communication with BootLockbox.
  std::unique_ptr<cryptohome::BootLockboxClient> boot_lockbox_client_;
  // This private key is generated once GenerateKeyPair is called and used once
  // per snapshot in TakeSnapshot.
  std::unique_ptr<crypto::RSAPrivateKey> private_key_;
  // This public key info is generated along with a private key in
  // GenerateKeyPair. The key is valid only when |private_key_| is set.
  // Should be stored on disk once |private_key_| is disposed.
  std::vector<uint8_t> public_key_info_;
  // Inode verification of snapshot directories is enabled in production ny
  // default.
  bool inode_verification_enabled_ = true;
};

}  // namespace data_snapshotd
}  // namespace arc

#endif  // ARC_DATA_SNAPSHOTD_DBUS_ADAPTOR_H_
