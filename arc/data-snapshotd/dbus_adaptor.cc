// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/data-snapshotd/dbus_adaptor.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>

namespace arc {
namespace data_snapshotd {

namespace {

// Snapshot paths:
constexpr char kCommonSnapshotPath[] =
    "/mnt/stateful_partition/unencrypted/arc-data-snapshot/";
constexpr char kLastSnapshotPath[] = "last";
constexpr char kPreviousSnapshotPath[] = "previous";

}  // namespace

DBusAdaptor::DBusAdaptor() : DBusAdaptor(base::FilePath(kCommonSnapshotPath)) {}

DBusAdaptor::~DBusAdaptor() = default;

// static
std::unique_ptr<DBusAdaptor> DBusAdaptor::CreateForTesting(
    const base::FilePath& snapshot_directory) {
  return base::WrapUnique(new DBusAdaptor(snapshot_directory));
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
  LOG(WARNING) << "Unimplimented";
  // TODO(b/160387490): Implement method:
  // * Generate a key pair.
  // * Store public key ib BootlockBox.
  // * Show a spinner screen.
  return false;
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

DBusAdaptor::DBusAdaptor(const base::FilePath& snapshot_directory)
    : org::chromium::ArcDataSnapshotdAdaptor(this),
      last_snapshot_directory_(snapshot_directory.Append(kLastSnapshotPath)),
      previous_snapshot_directory_(
          snapshot_directory.Append(kPreviousSnapshotPath)) {}

}  // namespace data_snapshotd
}  // namespace arc
