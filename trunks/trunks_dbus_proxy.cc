// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/trunks_dbus_proxy.h"

#include <memory>

#include <base/bind.h>

#include "trunks/dbus_interface.h"
#include "trunks/error_codes.h"
#include "trunks/interface.pb.h"

namespace {

// Use a five minute timeout because some commands on some TPM hardware can take
// a very long time. If a few lengthy operations are already in the queue, a
// subsequent command needs to wait for all of them. Timeouts are always
// possible but under normal conditions 5 minutes seems to be plenty.
const int kDBusMaxTimeout = 5 * 60 * 1000;

}  // namespace

namespace trunks {

TrunksDBusProxy::~TrunksDBusProxy() {
  if (bus_) {
    bus_->ShutdownAndBlock();
  }
}

bool TrunksDBusProxy::Init() {
  origin_thread_id_ = base::PlatformThread::CurrentId();
  if (!bus_) {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::Bus(options);
  }
  if (!bus_->Connect()) {
    return false;
  }
  if (!object_proxy_) {
    object_proxy_ =
        bus_->GetObjectProxy(trunks::kTrunksServiceName,
                             dbus::ObjectPath(trunks::kTrunksServicePath));
    if (!object_proxy_) {
      return false;
    }
  }
  base::TimeTicks deadline = base::TimeTicks::Now() + init_timeout_;
  while (!IsServiceReady(false /* force_check */) &&
         base::TimeTicks::Now() < deadline) {
    base::PlatformThread::Sleep(init_attempt_delay_);
  }
  return IsServiceReady(false /* force_check */);
}

bool TrunksDBusProxy::IsServiceReady(bool force_check) {
  if (!service_ready_ || force_check) {
    service_ready_ = CheckIfServiceReady();
  }
  return service_ready_;
}

bool TrunksDBusProxy::CheckIfServiceReady() {
  if (!bus_ || !object_proxy_) {
    return false;
  }
  std::string owner = bus_->GetServiceOwnerAndBlock(trunks::kTrunksServiceName,
                                                    dbus::Bus::SUPPRESS_ERRORS);
  return !owner.empty();
}

void TrunksDBusProxy::SendCommand(const std::string& command,
                                  const ResponseCallback& callback) {
  if (origin_thread_id_ != base::PlatformThread::CurrentId()) {
    LOG(ERROR) << "Error TrunksDBusProxy cannot be shared by multiple threads.";
    callback.Run(CreateErrorResponse(TRUNKS_RC_IPC_ERROR));
    return;
  }
  if (!IsServiceReady(false /* force_check */)) {
    LOG(ERROR) << "Error TrunksDBusProxy cannot connect to trunksd.";
    callback.Run(CreateErrorResponse(SAPI_RC_NO_CONNECTION));
    return;
  }
  SendCommandRequest tpm_command_proto;
  tpm_command_proto.set_command(command);
  auto on_success = base::Bind(
      [](const ResponseCallback& callback,
         const SendCommandResponse& response) {
        callback.Run(response.response());
      },
      callback);
  brillo::dbus_utils::CallMethodWithTimeout(
      kDBusMaxTimeout, object_proxy_, trunks::kTrunksInterface,
      trunks::kSendCommand, on_success,
      base::Bind(&TrunksDBusProxy::OnError, GetWeakPtr(), callback),
      tpm_command_proto);
}

void TrunksDBusProxy::OnError(const ResponseCallback& callback,
                              brillo::Error* error) {
  TPM_RC error_code = IsServiceReady(true /* force_check */)
                          ? SAPI_RC_NO_RESPONSE_RECEIVED
                          : SAPI_RC_NO_CONNECTION;
  callback.Run(CreateErrorResponse(error_code));
}

std::string TrunksDBusProxy::SendCommandAndWait(const std::string& command) {
  if (origin_thread_id_ != base::PlatformThread::CurrentId()) {
    LOG(ERROR) << "Error TrunksDBusProxy cannot be shared by multiple threads.";
    return CreateErrorResponse(TRUNKS_RC_IPC_ERROR);
  }
  if (!IsServiceReady(false /* force_check */)) {
    LOG(ERROR) << "Error TrunksDBusProxy cannot connect to trunksd.";
    return CreateErrorResponse(SAPI_RC_NO_CONNECTION);
  }
  SendCommandRequest tpm_command_proto;
  tpm_command_proto.set_command(command);
  brillo::ErrorPtr error;
  std::unique_ptr<dbus::Response> dbus_response =
      brillo::dbus_utils::CallMethodAndBlockWithTimeout(
          kDBusMaxTimeout, object_proxy_, trunks::kTrunksInterface,
          trunks::kSendCommand, &error, tpm_command_proto);
  SendCommandResponse tpm_response_proto;
  if (dbus_response.get() &&
      brillo::dbus_utils::ExtractMethodCallResults(dbus_response.get(), &error,
                                                   &tpm_response_proto)) {
    return tpm_response_proto.response();
  } else {
    LOG(ERROR) << "TrunksProxy could not parse response: "
               << error->GetMessage();
    TPM_RC error_code;
    if (!IsServiceReady(true /* force_check */)) {
      error_code = SAPI_RC_NO_CONNECTION;
    } else if (dbus_response == nullptr) {
      error_code = SAPI_RC_NO_RESPONSE_RECEIVED;
    } else {
      error_code = SAPI_RC_MALFORMED_RESPONSE;
    }
    return CreateErrorResponse(error_code);
  }
}

}  // namespace trunks
