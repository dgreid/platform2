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
  auto system_state = SystemState::Get();
  // Initialize supported DLC(s).
  for (const auto& id : ScanDirectory(system_state->manifest_dir())) {
    auto result = supported_.emplace(id, id);
    if (!result.first->second.Initialize()) {
      LOG(ERROR) << "Failed to initialize DLC " << id;
      supported_.erase(id);
    }
  }

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

  PreloadDlcs();
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
  CHECK(iter != supported_.end()) << "Passed invalid DLC: " << id;
  return &iter->second;
}

// Loads the preloadable DLC(s) from preloaded content directory by scanning the
// preloaded DLC(s) and verifying the validity to be preloaded before doing
// so.
void DlcManager::PreloadDlcs() {
  auto preloaded_dir = SystemState::Get()->preloaded_content_dir();
  // Load all preloaded DLC(s) into |content_dir_| one by one.
  for (const auto& id : ScanDirectory(preloaded_dir)) {
    if (!IsSupported(id)) {
      LOG(ERROR) << "Preloading is not allowed for unsupported DLC=" << id;
      auto preloaded_path = JoinPaths(preloaded_dir, id);
      if (!base::DeleteFile(preloaded_path, /*recursive=*/true))
        PLOG(ERROR) << "Failed to delete path=" << preloaded_path.value();
      continue;
    }

    auto& dlc = supported_.find(id)->second;
    if (!dlc.IsPreloadAllowed()) {
      LOG(ERROR) << "Preloading is not allowed for DLC=" << id;
      continue;
    }

    dlc.PreloadImage();
  }
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
  ErrorPtr tmp_err;
  for (const auto& pr : supported_)
    if (!pr.second.MakeReadyForUpdate(&tmp_err))
      PLOG(WARNING) << Error::ToString(tmp_err);
  return ToDlcIdList(supported_,
                     [](const DlcBase& dlc) { return dlc.IsVerified(); });
}

DlcIdList DlcManager::GetSupported() {
  return ToDlcIdList(supported_, [](const DlcBase&) { return true; });
}

DlcIdList DlcManager::GetMissingInstalls() {
  // Only return the DLC(s) that aren't already installed.
  return ToDlcIdList(supported_,
                     [](const DlcBase& dlc) { return dlc.IsInstalling(); });
}

bool DlcManager::GetDlcState(const DlcId& id, DlcState* state, ErrorPtr* err) {
  DCHECK(state);
  DCHECK(err);
  if (!IsSupported(id)) {
    *err = Error::Create(
        FROM_HERE, kErrorInvalidDlc,
        base::StringPrintf("Cannot get state for unsupported DLC=%s",
                           id.c_str()));
    return false;
  }

  *state = supported_.find(id)->second.GetState();
  return true;
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

bool DlcManager::InitInstall(const DlcIdList& dlcs, ErrorPtr* err) {
  DCHECK(err);
  if (dlcs.empty()) {
    *err = Error::Create(FROM_HERE, kErrorInvalidDlc,
                         "Must provide at least one DLC to install.");
    return false;
  }

  // Don't even start installing if we have some unsupported DLC request.
  for (const auto& id : dlcs) {
    if (!IsSupported(id)) {
      *err = Error::Create(
          FROM_HERE, kErrorInvalidDlc,
          base::StringPrintf("Trying to install unsupported DLC=%s",
                             id.c_str()));
      return false;
    }
  }

  DCHECK(!IsInstalling());

  ErrorPtr tmp_err;
  for (const auto& id : dlcs) {
    DlcBase& dlc = supported_.find(id)->second;
    if (!dlc.InitInstall(&tmp_err)) {
      *err = Error::Create(
          FROM_HERE, kErrorInternal,
          base::StringPrintf(
              "Failed to initialize installation of images for DLC %s",
              id.c_str()));
      CancelInstall(&tmp_err);
      return false;
    }
  }
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
    if (!dlc.FinishInstall(&tmp_err))
      ret = false;
  }
  if (!ret)
    *err = Error::Create(FROM_HERE, kErrorInternal,
                         "Not all DLC(s) successfully mounted.");
  return ret;
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

}  // namespace dlcservice
