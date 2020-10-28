// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/bootlockbox/boot_lockbox_dbus_adaptor.h"

#include <memory>
#include <string>
#include <vector>

#include <base/logging.h>
#include <brillo/errors/error.h>
#include <brillo/errors/error_codes.h>
#include <brillo/secure_blob.h>
#include <dbus/dbus-protocol.h>

#include "bootlockbox/proto_bindings/boot_lockbox_rpc.pb.h"
#include "cryptohome/bootlockbox/tpm_nvspace_interface.h"

namespace {
// Creates a dbus error message.
brillo::ErrorPtr CreateError(const std::string& code,
                             const std::string& message) {
  return brillo::Error::Create(FROM_HERE, brillo::errors::dbus::kDomain, code,
                               message);
}

}  // namespace

namespace cryptohome {

BootLockboxDBusAdaptor::BootLockboxDBusAdaptor(scoped_refptr<dbus::Bus> bus,
                                               NVRamBootLockbox* boot_lockbox)
    : org::chromium::BootLockboxInterfaceAdaptor(this),
      boot_lockbox_(boot_lockbox),
      dbus_object_(
          nullptr,
          bus,
          org::chromium::BootLockboxInterfaceAdaptor::GetObjectPath()) {}

void BootLockboxDBusAdaptor::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

void BootLockboxDBusAdaptor::StoreBootLockbox(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        cryptohome::StoreBootLockboxReply>> response,
    const cryptohome::StoreBootLockboxRequest& in_request) {
  if (!in_request.has_key() || !in_request.has_data()) {
    brillo::ErrorPtr error =
        CreateError(DBUS_ERROR_INVALID_ARGS,
                    "StoreBootLockboxRequest has invalid argument(s).");
    response->ReplyWithError(error.get());
    return;
  }

  cryptohome::StoreBootLockboxReply reply;
  cryptohome::BootLockboxErrorCode boot_lockbox_error;
  if (!boot_lockbox_->Store(in_request.key(), in_request.data(),
                            &boot_lockbox_error)) {
    reply.set_error(boot_lockbox_error);
  }
  response->Return(reply);
}

void BootLockboxDBusAdaptor::ReadBootLockbox(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        cryptohome::ReadBootLockboxReply>> response,
    const cryptohome::ReadBootLockboxRequest& in_request) {
  if (!in_request.has_key()) {
    brillo::ErrorPtr error =
        CreateError(DBUS_ERROR_INVALID_ARGS,
                    "ReadBootLockboxRequest has invalid argument(s).");
    response->ReplyWithError(error.get());
    return;
  }
  cryptohome::ReadBootLockboxReply reply;
  std::string data;
  cryptohome::BootLockboxErrorCode boot_lockbox_error;
  if (!boot_lockbox_->Read(in_request.key(), &data, &boot_lockbox_error)) {
    reply.set_error(boot_lockbox_error);
  } else {
    reply.set_data(data);
  }
  response->Return(reply);
}

void BootLockboxDBusAdaptor::FinalizeBootLockbox(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<
        cryptohome::FinalizeBootLockboxReply>> response,
    const cryptohome::FinalizeNVRamBootLockboxRequest& in_request) {
  cryptohome::FinalizeBootLockboxReply reply;
  if (!boot_lockbox_->Finalize()) {
    // Failed to finalize, could be communication error or other error.
    reply.set_error(cryptohome::BOOTLOCKBOX_ERROR_NVSPACE_OTHER);
  }
  response->Return(reply);
}

}  // namespace cryptohome
