// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string>

#include <base/bind.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/message_loop/message_loop.h>
#include <base/run_loop.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/secure_blob.h>

#include "attestation/client/dbus_proxy.h"
#include "attestation/common/print_interface_proto.h"
#include "cryptohome/tpm.h"
#include "tpm_manager/client/tpm_ownership_dbus_proxy.h"
#include "tpm_manager/common/print_tpm_manager_proto.h"

namespace cryptohome {

namespace tpm_manager {

using attestation::AttestationStatus;

template <typename MethodType, typename ReplyProtoType>
void SendAndWait(const MethodType& method,
                 ReplyProtoType* reply_proto) {
  base::RunLoop loop;
  auto callback = base::Bind(
      [](ReplyProtoType* reply_proto, base::RunLoop* loop,
         const ReplyProtoType& reply) {
        *reply_proto = reply;
        loop->Quit();
      },
      reply_proto, &loop);
  method.Run(callback);
  loop.Run();
}

int TakeOwnership(bool finalize) {
  base::MessageLoopForIO loop;
  base::FileDescriptorWatcher watcher(loop.task_runner());
  base::Time start_time = base::Time::Now();
  ::tpm_manager::TpmOwnershipDBusProxy proxy;
  if (!proxy.Initialize()) {
    LOG(ERROR) << "Failed to start tpm ownership proxy";
    return -1;
  }
  LOG(INFO) << "Initializing TPM.";
  ::tpm_manager::TakeOwnershipRequest request;
  auto method = base::Bind(&::tpm_manager::TpmOwnershipDBusProxy::TakeOwnership,
                           base::Unretained(&proxy), request);
  ::tpm_manager::TakeOwnershipReply reply;
  SendAndWait(method, &reply);
  if (reply.status() != ::tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << "Failed to take ownership.";
    puts(GetProtoDebugString(reply).c_str());
    return -1;
  }
  if (finalize) {
    LOG(WARNING) << "Finalization is ignored for TPM2.0";
  }
  base::TimeDelta duration = base::Time::Now() - start_time;
  LOG(INFO) << "TPM initialization successful ("
            << duration.InMilliseconds() << " ms).";
  return 0;
}

int VerifyEK(bool is_cros_core) {
  attestation::DBusProxy proxy;
  base::MessageLoopForIO loop;
  base::FileDescriptorWatcher watcher(loop.task_runner());
  if (!proxy.Initialize()) {
    LOG(ERROR) << "Failed to start attestation proxy";
    return -1;
  }
  attestation::VerifyRequest request;
  request.set_cros_core(is_cros_core);
  request.set_ek_only(true);
  auto method = base::Bind(&attestation::DBusProxy::Verify,
                           base::Unretained(&proxy), request);
  attestation::VerifyReply reply;
  SendAndWait(method, &reply);
  if (reply.status() != AttestationStatus::STATUS_SUCCESS) {
    LOG(ERROR) << "Failed to verify TPM endorsement.";
    puts(GetProtoDebugString(reply).c_str());
    return -1;
  }
  if (!reply.verified()) {
    LOG(ERROR) << "TPM endorsement verification failed.";
    return -1;
  }
  LOG(INFO) << "TPM endorsement verified successfully.";
  return 0;
}

int DumpStatus() {
  LOG(ERROR) << "Not implemented";
  return -1;
}

int GetRandom(unsigned int random_bytes_count) {
  cryptohome::Tpm* tpm = cryptohome::Tpm::GetSingleton();
  brillo::SecureBlob random_bytes;
  tpm->GetRandomDataSecureBlob(random_bytes_count, &random_bytes);
  if (random_bytes_count != random_bytes.size())
    return -1;

  std::string hex_bytes =
      base::HexEncode(random_bytes.data(), random_bytes.size());
  puts(hex_bytes.c_str());
  return 0;
}

bool GetVersionInfo(cryptohome::Tpm::TpmVersionInfo* version_info) {
  ::tpm_manager::TpmOwnershipDBusProxy proxy;
  base::MessageLoopForIO loop;
  base::FileDescriptorWatcher watcher(loop.task_runner());
  if (!proxy.Initialize()) {
    LOG(ERROR) << "Failed to start tpm ownership proxy";
    return false;
  }
  ::tpm_manager::GetVersionInfoRequest request;
  auto method = base::Bind(
      &::tpm_manager::TpmOwnershipDBusProxy::GetVersionInfo,
      base::Unretained(&proxy), request);
  ::tpm_manager::GetVersionInfoReply reply;
  SendAndWait(method, &reply);
  if (reply.status() != ::tpm_manager::STATUS_SUCCESS) {
    LOG(ERROR) << "Failed to get version info.";
    puts(GetProtoDebugString(reply).c_str());
    return false;
  }

  version_info->family = reply.family();
  version_info->spec_level = reply.spec_level();
  version_info->manufacturer = reply.manufacturer();
  version_info->tpm_model = reply.tpm_model();
  version_info->firmware_version = reply.firmware_version();
  version_info->vendor_specific = reply.vendor_specific();
  return true;
}

bool GetIFXFieldUpgradeInfo(cryptohome::Tpm::IFXFieldUpgradeInfo* info) {
  LOG(ERROR) << "Not implemented";
  return false;
}

bool GetTpmStatus(cryptohome::Tpm::TpmStatusInfo* status) {
  LOG(ERROR) << "Not implemented";
  return false;
}

}  // namespace tpm_manager

}  // namespace cryptohome
