// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tpm_manager/client/tpm_ownership_dbus_proxy.h"

#include <base/bind.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/dbus_signal_handler.h>
#include <tpm_manager-client/tpm_manager/dbus-constants.h>

#include "tpm_manager/common/tpm_ownership_dbus_interface.h"

namespace {

// Use a two minute timeout because TPM operations can take a long time.
const int kDBusTimeoutMS = 2 * 60 * 1000;

}  // namespace

namespace tpm_manager {

TpmOwnershipDBusProxy::~TpmOwnershipDBusProxy() {
  if (bus_) {
    bus_->ShutdownAndBlock();
  }
}

bool TpmOwnershipDBusProxy::Initialize() {
  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(options);
  object_proxy_ = bus_->GetObjectProxy(
      tpm_manager::kTpmManagerServiceName,
      dbus::ObjectPath(tpm_manager::kTpmManagerServicePath));
  return (object_proxy_ != nullptr);
}

bool TpmOwnershipDBusProxy::ConnectToSignal(
    TpmOwnershipTakenSignalHandler* handler) {
  if (ownership_taken_signal_handler_) {
    LOG(ERROR) << __func__ << ": |handler| is set already.";
    return false;
  }
  if (!handler) {
    LOG(ERROR) << __func__ << ": null handler.";
    return false;
  }
  ownership_taken_signal_handler_ = handler;
  brillo::dbus_utils::ConnectToSignal(
      object_proxy_, kTpmOwnershipInterface, kOwnershipTakenSignal,
      base::Bind(&TpmOwnershipTakenSignalHandler::OnOwnershipTaken,
                 base::Unretained(handler)),
      base::Bind(&TpmOwnershipTakenSignalHandler::OnSignalConnected,
                 base::Unretained(handler)));
  return true;
}

void TpmOwnershipDBusProxy::GetTpmStatus(const GetTpmStatusRequest& request,
                                         const GetTpmStatusCallback& callback) {
  CallMethod<GetTpmStatusReply>(tpm_manager::kGetTpmStatus, request, callback);
}

void TpmOwnershipDBusProxy::GetVersionInfo(
    const GetVersionInfoRequest& request,
    const GetVersionInfoCallback& callback) {
  CallMethod<GetVersionInfoReply>(tpm_manager::kGetVersionInfo, request,
                                  callback);
}

void TpmOwnershipDBusProxy::GetDictionaryAttackInfo(
    const GetDictionaryAttackInfoRequest& request,
    const GetDictionaryAttackInfoCallback& callback) {
  CallMethod<GetDictionaryAttackInfoReply>(
      tpm_manager::kGetDictionaryAttackInfo, request, callback);
}

void TpmOwnershipDBusProxy::ResetDictionaryAttackLock(
    const ResetDictionaryAttackLockRequest& request,
    const ResetDictionaryAttackLockCallback& callback) {
  CallMethod<ResetDictionaryAttackLockReply>(
      tpm_manager::kResetDictionaryAttackLock, request, callback);
}

void TpmOwnershipDBusProxy::TakeOwnership(
    const TakeOwnershipRequest& request,
    const TakeOwnershipCallback& callback) {
  CallMethod<TakeOwnershipReply>(tpm_manager::kTakeOwnership, request,
                                 callback);
}

void TpmOwnershipDBusProxy::RemoveOwnerDependency(
    const RemoveOwnerDependencyRequest& request,
    const RemoveOwnerDependencyCallback& callback) {
  CallMethod<RemoveOwnerDependencyReply>(tpm_manager::kRemoveOwnerDependency,
                                         request, callback);
}

void TpmOwnershipDBusProxy::ClearStoredOwnerPassword(
    const ClearStoredOwnerPasswordRequest& request,
    const ClearStoredOwnerPasswordCallback& callback) {
  CallMethod<ClearStoredOwnerPasswordReply>(
      tpm_manager::kClearStoredOwnerPassword, request, callback);
}

template <typename ReplyProtobufType,
          typename RequestProtobufType,
          typename CallbackType>
void TpmOwnershipDBusProxy::CallMethod(const std::string& method_name,
                                       const RequestProtobufType& request,
                                       const CallbackType& callback) {
  auto on_error = [](CallbackType callback, brillo::Error* error) {
    ReplyProtobufType reply;
    reply.set_status(STATUS_NOT_AVAILABLE);
    callback.Run(reply);
  };
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusTimeoutMS, object_proxy_, tpm_manager::kTpmOwnershipInterface,
      method_name, callback, base::Bind(on_error, callback), request);
}

}  // namespace tpm_manager
