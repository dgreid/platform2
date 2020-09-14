// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/bootlockbox/boot_lockbox_service.h"

#include <sysexits.h>

#include <base/logging.h>
#include <dbus/dbus-protocol.h>

#include "cryptohome/bootlockbox/tpm2_nvspace_utility.h"
#include "cryptohome/bootlockbox/tpm_nvspace_interface.h"
#include "cryptohome/crypto.h"
#include "cryptohome/cryptolib.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"

namespace cryptohome {

int BootLockboxService::OnInit() {
  nvspace_utility_ = std::make_unique<TPM2NVSpaceUtility>();
  if (!nvspace_utility_->Initialize()) {
    LOG(ERROR) << "Failed to initialize nvspace utility";
    return EX_UNAVAILABLE;
  }
  boot_lockbox_.reset(new NVRamBootLockbox(nvspace_utility_.get()));

  if (!boot_lockbox_->Load() &&
      boot_lockbox_->GetState() == NVSpaceState::kNVSpaceUndefined) {
    LOG(INFO) << "NVSpace is not defined, define it now";
    if (!boot_lockbox_->DefineSpace()) {
      // TPM define nvspace failed but continue to run the service so
      // bootlockbox client can still communicated with bootlockbox. The client
      // need this to differentiate boot lockbox service errors and tpm errors.
      LOG(ERROR) << "Failed to create nvspace";
    }
  }

  // Publish the service to dbus. Note that if nvspace is not defined,
  // calls to the interface would receive failure messages.
  const int return_code = brillo::DBusServiceDaemon::OnInit();
  if (return_code != EX_OK) {
    LOG(ERROR) << "Failed to start bootlockbox service";
    return return_code;
  }
  LOG(INFO) << "BootLockboxd started";
  return EX_OK;
}

void BootLockboxService::OnShutdown(int* exit_code) {
  VLOG(1) << "Shutting down bootlockbox service";
  brillo::DBusServiceDaemon::OnShutdown(exit_code);
}

void BootLockboxService::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  VLOG(1) << "Register dbus objects...";
  boot_lockbox_dbus_adaptor_.reset(
      new BootLockboxDBusAdaptor(bus_, boot_lockbox_.get()));
  boot_lockbox_dbus_adaptor_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));
  VLOG(1) << "Register dbus object complete";
}

BootLockboxService::BootLockboxService()
    : brillo::DBusServiceDaemon("org.chromium.BootLockbox") {}

BootLockboxService::~BootLockboxService() {}

}  // namespace cryptohome
