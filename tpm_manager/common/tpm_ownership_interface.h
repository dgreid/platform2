// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_COMMON_TPM_OWNERSHIP_INTERFACE_H_
#define TPM_MANAGER_COMMON_TPM_OWNERSHIP_INTERFACE_H_

#include <base/callback.h>
#include <tpm_manager/proto_bindings/tpm_manager.pb.h>

#include "tpm_manager/common/export.h"

namespace tpm_manager {

// The command interface for TPM administration. Inherited by both IPC proxy
// and service classes. All methods are asynchronous because all TPM operations
// may take a long time to finish.
class TPM_MANAGER_EXPORT TpmOwnershipInterface {
 public:
  virtual ~TpmOwnershipInterface() = default;

  // Gets TPM status, which includes enabled, owned, passwords, etc. Processes
  // |request| and calls |callback| with a reply when the process is done.
  using GetTpmStatusCallback = base::Callback<void(const GetTpmStatusReply&)>;
  virtual void GetTpmStatus(const GetTpmStatusRequest& request,
                            const GetTpmStatusCallback& callback) = 0;

  // Gets TPM nonsensitive status, which includes enabled, owned, presence of
  // password, etc. Processes |request| and calls |callback| with a reply when
  // the process is done.
  using GetTpmNonsensitiveStatusCallback =
      base::Callback<void(const GetTpmNonsensitiveStatusReply&)>;
  virtual void GetTpmNonsensitiveStatus(
      const GetTpmNonsensitiveStatusRequest& request,
      const GetTpmNonsensitiveStatusCallback& callback) = 0;

  // Gets TPM version info. Processes |request| and calls |callback| with a
  // reply when the process is done.
  using GetVersionInfoCallback =
      base::Callback<void(const GetVersionInfoReply&)>;
  virtual void GetVersionInfo(const GetVersionInfoRequest& request,
                              const GetVersionInfoCallback& callback) = 0;

  // Gets dictionary attack (DA) info. Processes |request| and calls |callback|
  // with a reply when the process is done.
  using GetDictionaryAttackInfoCallback =
      base::Callback<void(const GetDictionaryAttackInfoReply&)>;
  virtual void GetDictionaryAttackInfo(
      const GetDictionaryAttackInfoRequest& request,
      const GetDictionaryAttackInfoCallback& callback) = 0;

  // Resets dictionary attack (DA) lock. Processes |request| and calls
  // |callback| with a reply when the process is done.
  using ResetDictionaryAttackLockCallback =
      base::Callback<void(const ResetDictionaryAttackLockReply&)>;
  virtual void ResetDictionaryAttackLock(
      const ResetDictionaryAttackLockRequest& request,
      const ResetDictionaryAttackLockCallback& callback) = 0;

  // Processes a TakeOwnershipRequest and responds with a TakeOwnershipReply.
  using TakeOwnershipCallback = base::Callback<void(const TakeOwnershipReply&)>;
  virtual void TakeOwnership(const TakeOwnershipRequest& request,
                             const TakeOwnershipCallback& callback) = 0;

  // Processes a RemoveOwnerDependencyRequest and responds with a
  // RemoveOwnerDependencyReply.
  using RemoveOwnerDependencyCallback =
      base::Callback<void(const RemoveOwnerDependencyReply&)>;
  virtual void RemoveOwnerDependency(
      const RemoveOwnerDependencyRequest& request,
      const RemoveOwnerDependencyCallback& callback) = 0;

  // Processes a ClearStoredOwnerPasswordRequest and responds with a
  // ClearStoredOwnerPasswordReply.
  using ClearStoredOwnerPasswordCallback =
      base::Callback<void(const ClearStoredOwnerPasswordReply&)>;
  virtual void ClearStoredOwnerPassword(
      const ClearStoredOwnerPasswordRequest& request,
      const ClearStoredOwnerPasswordCallback& callback) = 0;
};

}  // namespace tpm_manager

#endif  // TPM_MANAGER_COMMON_TPM_OWNERSHIP_INTERFACE_H_
