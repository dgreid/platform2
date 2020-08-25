// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/dev_mode_no_owner_restriction.h"

#include <memory>
#include <string>
#include <vector>

#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>
#include <google/protobuf/message_lite.h>

#include "debugd/src/error_utils.h"
#include "debugd/src/process_with_output.h"

#include "rpc.pb.h"  // NOLINT(build/include_directory)

namespace debugd {

namespace {

const char kAccessDeniedErrorString[] =
    "org.chromium.debugd.error.AccessDenied";
const char kDevModeAccessErrorString[] =
    "Use of this tool is restricted to dev mode.";
const char kOwnerAccessErrorString[] =
    "Unavailable after device has an owner or boot lockbox is finalized.";
const char kOwnerQueryErrorString[] =
    "Error encountered when querying D-Bus, cryptohome may be busy.";

// Queries the cryptohome GetLoginStatus D-Bus interface.
//
// Handles lower-level logic for dbus methods and the cryptohome protobuf
// classes. Cryptohome protobuf responses work by extending the BaseReply class,
// so if an error occurs it's possible to get a reply that does not contain the
// GetLoginStatusReply extension.
//
// |reply| will be filled if a response was received regardless of extension,
// but the function will only return true if reply is filled and has the
// correct GetLoginStatusReply extension.
bool CryptohomeGetLoginStatus(dbus::Bus* bus, cryptohome::BaseReply* reply) {
  cryptohome::GetLoginStatusRequest request;

  dbus::ObjectProxy* proxy =
      bus->GetObjectProxy(cryptohome::kCryptohomeServiceName,
                          dbus::ObjectPath(cryptohome::kCryptohomeServicePath));
  dbus::MethodCall method_call(cryptohome::kCryptohomeInterface,
                               cryptohome::kCryptohomeGetLoginStatus);
  dbus::MessageWriter writer(&method_call);
  writer.AppendProtoAsArrayOfBytes(request);
  std::unique_ptr<dbus::Response> response = proxy->CallMethodAndBlock(
      &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);

  if (!response)
    return false;

  dbus::MessageReader reader(response.get());
  return reader.PopArrayOfBytesAsProto(reply);
}

}  // namespace

DevModeNoOwnerRestriction::DevModeNoOwnerRestriction(
    scoped_refptr<dbus::Bus> bus)
    : bus_(bus) {}

bool DevModeNoOwnerRestriction::AllowToolUse(brillo::ErrorPtr* error) {
  // Check dev mode first to avoid unnecessary cryptohome query delays.
  if (!InDevMode()) {
    DEBUGD_ADD_ERROR(error, kAccessDeniedErrorString,
                     kDevModeAccessErrorString);
    return false;
  }

  bool owner_exists, boot_lockbox_finalized;
  if (!GetOwnerAndLockboxStatus(&owner_exists, &boot_lockbox_finalized)) {
    // We want to specifically indicate when the query failed since it may
    // mean that cryptohome is busy and could be tried again later.
    DEBUGD_ADD_ERROR(error, kAccessDeniedErrorString, kOwnerQueryErrorString);
    return false;
  }

  if (owner_exists || boot_lockbox_finalized) {
    DEBUGD_ADD_ERROR(error, kAccessDeniedErrorString, kOwnerAccessErrorString);
    return false;
  }

  return true;
}

bool DevModeNoOwnerRestriction::InDevMode() const {
  // The is_developer_end_user script provides a common way to access this
  // information rather than duplicating logic here.
  return ProcessWithOutput::RunProcess("/usr/sbin/is_developer_end_user",
                                       ProcessWithOutput::ArgList{},
                                       true,     // needs root to run properly.
                                       false,    // disable_sandbox.
                                       nullptr,  // no stdin.
                                       nullptr,  // no stdout.
                                       nullptr,  // no stderr.
                                       nullptr) == 0;  // no D-Bus error.
}

// Checks for owner user and boot lockbox status.
//
// This function handles the high-level code of checking the cryptohome
// protocol buffer response. Lower-level details of sending the D-Bus function
// and parsing the protocol buffer are handled in CryptohomeGetLoginStatus().
//
// If cryptohome was queried successfully, returns true and |owner_user_exists|
// and |boot_lockbox_finalized| are updated.
bool DevModeNoOwnerRestriction::GetOwnerAndLockboxStatus(
    bool* owner_user_exists, bool* boot_lockbox_finalized) {
  cryptohome::BaseReply base_reply;
  if (CryptohomeGetLoginStatus(bus_.get(), &base_reply)) {
    cryptohome::GetLoginStatusReply reply =
        base_reply.GetExtension(cryptohome::GetLoginStatusReply::reply);
    if (reply.has_owner_user_exists() && reply.has_boot_lockbox_finalized()) {
      *owner_user_exists = reply.owner_user_exists();
      *boot_lockbox_finalized = reply.boot_lockbox_finalized();
      return true;
    }
  }
  return false;
}

}  // namespace debugd
