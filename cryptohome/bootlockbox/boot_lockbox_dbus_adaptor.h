// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_DBUS_ADAPTOR_H_
#define CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_DBUS_ADAPTOR_H_

#include <memory>
#include <vector>

#include "bootlockbox/proto_bindings/boot_lockbox_rpc.pb.h"
#include "cryptohome/bootlockbox/nvram_boot_lockbox.h"

#include "dbus_adaptors/org.chromium.BootLockboxInterface.h"

namespace cryptohome {
// Implements DBus BootLockboxInterface.
class BootLockboxDBusAdaptor
    : public org::chromium::BootLockboxInterfaceInterface,
      public org::chromium::BootLockboxInterfaceAdaptor {
 public:
  // Don't own boot_lockbox, it is managed by BootLockboxService.
  explicit BootLockboxDBusAdaptor(scoped_refptr<dbus::Bus> bus,
                                  NVRamBootLockbox* boot_lockbox);
  BootLockboxDBusAdaptor(const BootLockboxDBusAdaptor&) = delete;
  BootLockboxDBusAdaptor& operator=(const BootLockboxDBusAdaptor&) = delete;

  // Registers dbus objects.
  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb);

  // Stores a digest in bootlockbox.
  void StoreBootLockbox(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::StoreBootLockboxReply>> response,
      const cryptohome::StoreBootLockboxRequest& in_request) override;

  // Reads a digest from bootlockbox.
  void ReadBootLockbox(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::ReadBootLockboxReply>> response,
      const cryptohome::ReadBootLockboxRequest& in_request) override;

  // Finalizes the BootLockbox and locks the signing key. |response| is of type
  // BaseReply.
  void FinalizeBootLockbox(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
          cryptohome::FinalizeBootLockboxReply>> response,
      const cryptohome::FinalizeNVRamBootLockboxRequest& in_request) override;

 private:
  NVRamBootLockbox* boot_lockbox_;
  brillo::dbus_utils::DBusObject dbus_object_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_DBUS_ADAPTOR_H_
