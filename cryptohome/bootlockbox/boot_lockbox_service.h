// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_SERVICE_H_
#define CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_SERVICE_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>

#include "cryptohome/bootlockbox/boot_lockbox_dbus_adaptor.h"
#include "cryptohome/bootlockbox/nvram_boot_lockbox.h"

namespace cryptohome {

// BootLockboxService that implements the top level setups of bootlockboxd.
class BootLockboxService : public brillo::DBusServiceDaemon {
 public:
  BootLockboxService();
  BootLockboxService(const BootLockboxService&) = delete;
  BootLockboxService& operator=(const BootLockboxService&) = delete;

  ~BootLockboxService();

 protected:
  int OnInit() override;
  void OnShutdown(int* exit_code) override;
  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override;

 private:
  std::unique_ptr<TPMNVSpaceUtilityInterface> nvspace_utility_;
  std::unique_ptr<NVRamBootLockbox> boot_lockbox_;
  std::unique_ptr<BootLockboxDBusAdaptor> boot_lockbox_dbus_adaptor_;
  base::WeakPtrFactory<BootLockboxService> weak_factory_{this};
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_SERVICE_H_
