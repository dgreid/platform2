// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TPM_MANAGER_COMMON_TPM_OWNERSHIP_DBUS_INTERFACE_H_
#define TPM_MANAGER_COMMON_TPM_OWNERSHIP_DBUS_INTERFACE_H_

namespace tpm_manager {

constexpr char kTpmOwnershipInterface[] = "org.chromium.TpmManager";

// Methods exported by tpm_manager ownership D-Bus interface.

// TODO(garryxiao): Tpm status, version, and the DA methods are not directly
// related to TPM ownership. Probably rename the interface later.
constexpr char kGetTpmStatus[] = "GetTpmStatus";
constexpr char kGetVersionInfo[] = "GetVersionInfo";
constexpr char kGetDictionaryAttackInfo[] = "GetDictionaryAttackInfo";
constexpr char kResetDictionaryAttackLock[] = "ResetDictionaryAttackLock";
constexpr char kTakeOwnership[] = "TakeOwnership";
constexpr char kRemoveOwnerDependency[] = "RemoveOwnerDependency";
constexpr char kClearStoredOwnerPassword[] = "ClearStoredOwnerPassword";

// Signal registered by tpm_manager ownership D-Bus interface.
constexpr char kOwnershipTakenSignal[] = "SignalOwnershipTaken";

}  // namespace tpm_manager

#endif  // TPM_MANAGER_COMMON_TPM_OWNERSHIP_DBUS_INTERFACE_H_
