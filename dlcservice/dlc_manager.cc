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

DlcManager::DlcManager() {
  // Initialize supported DLC(s).
  for (const auto& id : ScanDirectory(SystemState::Get()->manifest_dir())) {
    auto result = supported_.emplace(id, id);
    if (!result.first->second.Initialize()) {
      LOG(ERROR) << "Failed to initialize DLC " << id;
      supported_.erase(id);
    }
  }
}

DlcManager::~DlcManager() = default;

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

const DlcBase& DlcManager::GetDlc(const DlcId& id) {
  const auto& iter = supported_.find(id);
  CHECK(iter != supported_.end()) << "Passed invalid DLC: " << id;
  return iter->second;
}

// Loads the preloadable DLC(s) from preloaded content directory by scanning the
// preloaded DLC(s) and verifying the validity to be preloaded before doing
// so.
void DlcManager::PreloadDlcModuleImages() {
  // Load all preloaded DLC(s) into |content_dir_| one by one.
  for (const auto& id :
       ScanDirectory(SystemState::Get()->preloaded_content_dir())) {
    if (!IsSupported(id)) {
      LOG(ERROR) << "Preloading is not allowed for unsupported DLC=" << id;
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

DlcSet DlcManager::GetInstalled() {
  return ToDlcSet(supported_,
                  [](const DlcBase& dlc) { return dlc.IsInstalled(); });
}

DlcSet DlcManager::GetSupported() {
  return ToDlcSet(supported_, [](const DlcBase&) { return true; });
}

DlcSet DlcManager::GetMissingInstalls() {
  // Only return the DLC(s) that aren't already installed.
  return ToDlcSet(supported_,
                  [](const DlcBase& dlc) { return dlc.IsInstalling(); });
}

bool DlcManager::GetState(const DlcId& id, DlcState* state, ErrorPtr* err) {
  DCHECK(state);
  DCHECK(err);
  if (!IsSupported(id)) {
    *err = Error::Create(
        kErrorInvalidDlc,
        base::StringPrintf("Cannot get state for unsupported DLC=%s",
                           id.c_str()));
    return false;
  }

  *state = supported_.find(id)->second.GetState();
  return true;
}

bool DlcManager::InitInstall(const DlcSet& dlcs, ErrorPtr* err) {
  DCHECK(err);
  if (dlcs.empty()) {
    *err = Error::Create(kErrorInvalidDlc,
                         "Must provide at least one DLC to install.");
    return false;
  }

  // Don't even start installing if we have some unsupported DLC request.
  for (const auto& id : dlcs) {
    if (!IsSupported(id)) {
      *err = Error::Create(
          kErrorInvalidDlc,
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
          kErrorInternal,
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
        kErrorInvalidDlc,
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
    *err =
        Error::Create(kErrorInternal, "Not all DLC(s) successfully mounted.");
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
      PLOG(ERROR) << "Failed during install cancellation: "
                  << Error::ToString(*err);
      ret = false;
    }
  }
  return ret;
}

}  // namespace dlcservice
