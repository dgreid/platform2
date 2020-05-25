// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/dlc_manager.h"

#include <cinttypes>
#include <utility>

#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>

#include "dlcservice/dlc.h"
#include "dlcservice/error.h"
#include "dlcservice/system_state.h"
#include "dlcservice/utils.h"

using brillo::ErrorPtr;

namespace dlcservice {

void DlcManager::Initialize() {
  // Initialize supported DLC(s).
  for (const auto& id : ScanDirectory(SystemState::Get()->manifest_dir())) {
    auto result = supported_.emplace(id, id);
    if (!result.first->second.Initialize()) {
      LOG(ERROR) << "Failed to initialize DLC " << id;
      supported_.erase(id);
    }
  }
  CleanupUnsupportedDlcs();
}

void DlcManager::CleanupUnsupportedDlcs() {
  auto system_state = SystemState::Get();
  // Delete deprecated DLC(s) in content directory.
  for (const auto& id : ScanDirectory(system_state->content_dir())) {
    if (IsSupported(id))
      continue;
    for (const auto& path : DlcBase::GetPathsToDelete(id))
      if (base::PathExists(path)) {
        if (!base::DeleteFile(path, /*recursive=*/true))
          PLOG(ERROR) << "Failed to delete path=" << path;
        else
          LOG(INFO) << "Deleted path=" << path << " for deprecated DLC=" << id;
      }
  }

  // Delete the unsupported/preload not allowed DLC(s) in the preloaded
  // directory.
  auto preloaded_content_dir = system_state->preloaded_content_dir();
  for (const auto& id : ScanDirectory(preloaded_content_dir)) {
    if (IsSupported(id)) {
      auto* dlc = GetDlc(id);
      if (dlc->IsPreloadAllowed())
        continue;
    }
    // Preloading is not allowed for this image so it will be deleted.
    auto path = JoinPaths(preloaded_content_dir, id);
    if (!base::DeleteFile(path, /*recursive=*/true))
      PLOG(ERROR) << "Failed to delete path=" << path;
    else
      LOG(INFO) << "Deleted path=" << path
                << " for unsupported/preload not allowed DLC=" << id;
  }
}

bool DlcManager::IsSupported(const DlcId& id) {
  // TODO(ahassani): Consider searching through the manifest directory again if
  // we missed to get the manifest for a specific when we initialized it.
  return supported_.find(id) != supported_.end();
}

bool DlcManager::IsInstalling() {
  return std::any_of(
      supported_.begin(), supported_.end(),
      [](const auto& pair) { return pair.second.IsInstalling(); });
}

const DlcBase* DlcManager::GetDlc(const DlcId& id) {
  const auto& iter = supported_.find(id);
  if (iter == supported_.end()) {
    LOG(ERROR) << "Passed invalid DLC: " << id;
    return nullptr;
  }
  return &iter->second;
}

DlcIdList DlcManager::GetInstalled() {
  // TODO(kimjae): Once update_engine repeatedly calls into |GetInstalled()| for
  // updating update, need to handle clearing differently.
  return ToDlcIdList(supported_,
                     [](const DlcBase& dlc) { return dlc.IsInstalled(); });
}

DlcIdList DlcManager::GetExistingDlcs() {
  return ToDlcIdList(supported_,
                     [](const DlcBase& dlc) { return dlc.HasContent(); });
}

DlcIdList DlcManager::GetDlcsToUpdate() {
  return ToDlcIdList(
      supported_, [](const DlcBase& dlc) { return dlc.MakeReadyForUpdate(); });
}

DlcIdList DlcManager::GetSupported() {
  return ToDlcIdList(supported_, [](const DlcBase&) { return true; });
}

bool DlcManager::InstallCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) {
  DCHECK(err);
  bool ret = true;
  for (const auto& id : ids) {
    if (!IsSupported(id)) {
      LOG(WARNING) << "Trying to complete installation for unsupported DLC="
                   << id;
      ret = false;
    } else if (!supported_.find(id)->second.InstallCompleted(err)) {
      PLOG(WARNING) << Error::ToString(*err);
      ret = false;
    }
  }
  if (!ret)
    *err = Error::Create(
        FROM_HERE, kErrorInvalidDlc,
        base::StringPrintf("Failed to mark all installed DLCs as verified."));
  return ret;
}

bool DlcManager::UpdateCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) {
  DCHECK(err);
  bool ret = true;
  for (const auto& id : ids) {
    if (!IsSupported(id)) {
      LOG(WARNING) << "Trying to complete update for unsupported DLC=" << id;
      ret = false;
    } else if (!supported_.find(id)->second.UpdateCompleted(err)) {
      LOG(WARNING) << Error::ToString(*err);
      ret = false;
    }
  }
  if (!ret)
    *err = Error::Create(
        FROM_HERE, kErrorInvalidDlc,
        base::StringPrintf("Failed to mark all updated DLCs as hashed."));
  return ret;
}

bool DlcManager::Install(const DlcId& id,
                         bool* external_install_needed,
                         ErrorPtr* err) {
  DCHECK(err);
  // Don't even start installing if we have some unsupported DLC request.
  if (!IsSupported(id)) {
    *err = Error::Create(
        FROM_HERE, kErrorInvalidDlc,
        base::StringPrintf("Trying to install unsupported DLC=%s", id.c_str()));
    return false;
  }

  DlcBase& dlc = supported_.find(id)->second;
  // If the DLC is being installed, nothing can be done anymore.
  if (dlc.IsInstalling()) {
    return true;
  }

  // Otherwise proceed to install the DLC.
  if (!dlc.Install(err)) {
    LOG(ERROR) << "Failed to initialize installation for DLC=" << id;
    return false;
  }

  // If the DLC is now in installing state, it means it now needs update_engine
  // installation.
  *external_install_needed = dlc.IsInstalling();
  return true;
}

bool DlcManager::Delete(const DlcId& id, ErrorPtr* err) {
  DCHECK(err);
  if (!IsSupported(id)) {
    *err = Error::Create(
        FROM_HERE, kErrorInvalidDlc,
        base::StringPrintf("Trying to delete unsupported DLC=%s", id.c_str()));
    return false;
  }
  return supported_.find(id)->second.Delete(err);
}

bool DlcManager::FinishInstall(ErrorPtr* err) {
  DCHECK(err);
  bool ret = true;
  for (auto& pair : supported_) {
    auto& dlc = pair.second;
    ErrorPtr tmp_err;
    // Only try to finish install for DLCs that were in installing phase. Other
    // DLCs should not be finished this route.
    if (dlc.IsInstalling() && !dlc.FinishInstall(&tmp_err))
      ret = false;
  }
  if (!ret)
    *err = Error::Create(FROM_HERE, kErrorInternal,
                         "Not all DLC(s) successfully mounted.");
  return ret;
}

bool DlcManager::CancelInstall(const DlcId& id, ErrorPtr* err) {
  DCHECK(err);
  if (!IsSupported(id)) {
    *err = Error::Create(
        FROM_HERE, kErrorInvalidDlc,
        base::StringPrintf("Trying to cancle install for unsupported DLC=%s",
                           id.c_str()));
    return false;
  }

  return supported_.find(id)->second.CancelInstall(err);
}

bool DlcManager::CancelInstall(ErrorPtr* err) {
  DCHECK(err);
  if (!IsInstalling()) {
    LOG(WARNING) << "No install started to being with, nothing to cancel.";
    return true;
  }

  bool ret = true;
  for (auto& pr : supported_) {
    auto& dlc = pr.second;
    if (!dlc.CancelInstall(err)) {
      LOG(ERROR) << "Failed during install cancellation: "
                 << Error::ToString(*err);
      ret = false;
    }
  }
  return ret;
}

void DlcManager::ChangeProgress(double progress) {
  for (auto& pr : supported_) {
    auto& dlc = pr.second;
    if (dlc.IsInstalling()) {
      dlc.ChangeProgress(progress);
    }
  }
}

}  // namespace dlcservice
