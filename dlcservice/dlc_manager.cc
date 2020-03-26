// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/dlc_manager.h"

#include <cinttypes>
#include <utility>
#include <vector>

#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <brillo/message_loops/message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dlcservice/dbus-constants.h>

#include "dlcservice/error.h"
#include "dlcservice/system_state.h"
#include "dlcservice/utils.h"

using base::Callback;
using base::FilePath;
using brillo::ErrorPtr;
using std::string;
using std::vector;

namespace dlcservice {

namespace {
// Timeout in ms for DBus method calls into imageloader.
constexpr int kImageLoaderTimeoutMs = 5000;
}  // namespace

class DlcManager::DlcManagerImpl {
 public:
  DlcManagerImpl() {
    const auto system_state = SystemState::Get();
    image_loader_proxy_ = system_state->image_loader();
    manifest_dir_ = system_state->manifest_dir();
    preloaded_content_dir_ = system_state->preloaded_content_dir();
    content_dir_ = system_state->content_dir();

    string boot_disk_name;
    if (!system_state->boot_slot().GetCurrentSlot(&boot_disk_name,
                                                  &current_boot_slot_))
      LOG(FATAL) << "Can not get current boot slot.";

    // Initialize supported DLC(s).
    for (const auto& id : ScanDirectory(manifest_dir_))
      supported_[id];
  }
  ~DlcManagerImpl() = default;

  bool IsSupported(const DlcId& id) {
    return supported_.find(id) != supported_.end();
  }

  bool IsInstalling() {
    for (const auto& pr : supported_)
      if (pr.second.state.state() == DlcState::INSTALLING)
        return true;
    return false;
  }

  DlcMap GetSupported() { return supported_; }

  DlcInfo GetInfo(const DlcId& id) {
    DCHECK(IsSupported(id));
    return supported_[id];
  }

  // Loads the preloadable DLC(s) from |preloaded_content_dir_| by scanning the
  // preloaded DLC(s) and verifying the validity to be preloaded before doing
  // so.
  void PreloadDlcModuleImages() {
    // Load all preloaded DLC(s) into |content_dir_| one by one.
    for (const auto& id : ScanDirectory(preloaded_content_dir_)) {
      if (!IsSupported(id)) {
        LOG(ERROR) << "Preloading is not allowed for unsupported DLC=" << id;
        continue;
      }
      if (!IsDlcPreloadAllowed(manifest_dir_, id)) {
        LOG(ERROR) << "Preloading is not allowed for DLC=" << id;
        continue;
      }

      ErrorPtr tmp_err;
      // Deleting DLC(s) that might already be installed as preloading DLC(s)
      // take precedence in order to allow stale DLC(s) in cache to be cleared.
      // Loading should be run prior to preloading, to enforce this strict
      // precedence.
      // TODO(crbug.com/1059445): Verify before deleting that image to preload
      // has the correct hash.
      if (!Delete(id, &tmp_err)) {
        PLOG(ERROR) << "Failed to delete prior to preloading DLC=" << id << ", "
                    << Error::ToString(tmp_err);
        continue;
      }

      DlcSet dlc_set = {id};
      if (!InitInstall(dlc_set, &tmp_err)) {
        LOG(ERROR) << "Failed to create preloaded DLC=" << id << ", "
                   << Error::ToString(tmp_err);
        continue;
      }

      if (!PreloadedCopier(id)) {
        LOG(ERROR) << "Something went wrong during preloading DLC (" << id
                   << "), please check for previous errors.";
        if (!CancelInstall(&tmp_err))
          LOG(WARNING) << Error::ToString(tmp_err);
        continue;
      }

      // When the copying is successful, go ahead and finish installation.
      if (!FinishInstall(&tmp_err)) {
        LOG(ERROR) << "Failed to finish installation for preloaded DLC=" << id
                   << ", " << Error::ToString(tmp_err);
        continue;
      }

      // Delete the preloaded DLC only after both copies into A and B succeed as
      // well as mounting.
      auto image_preloaded_path = JoinPaths(
          preloaded_content_dir_, id, GetDlcPackage(id), kDlcImageFileName);
      if (!base::DeleteFile(image_preloaded_path.DirName().DirName(), true)) {
        LOG(ERROR) << "Failed to delete image after preloading DLC=" << id;
        continue;
      }
    }
  }

  // Check installed DLC(s) at startup.
  void LoadDlcModuleImages() {
    for (const auto& id : ScanDirectory(content_dir_)) {
      ErrorPtr tmp_err;
      if (!IsSupported(id)) {
        LOG(ERROR)
            << "Deleting during startup an installed but unsupported DLC="
            << id;
        if (!Delete(id, &tmp_err))
          PLOG(ERROR) << "Failed during startup: " << Error::ToString(tmp_err);
        continue;
      }
      if (!ValidateImageFiles(id, &tmp_err)) {
        LOG(ERROR) << "Failed to validate during startup for DLC=" << id << ", "
                   << Error::ToString(tmp_err);
        if (!Delete(id, &tmp_err))
          PLOG(ERROR) << "Failed during startup: " << Error::ToString(tmp_err);
        continue;
      }
      // - If the root is empty and is currently installing then skip.
      // - If the root exists set it and continue.
      // - Try mounting, if mounted set it and continue.
      // - Remove the DLC if none of the previous checks are met.
      string mount_point;
      if (Mount(id, &mount_point, &tmp_err)) {
        SetInstalled(id, GetDlcRoot(FilePath(mount_point)).value());
      } else {
        LOG(ERROR) << "Failed to mount during startup for DLC=" << id << ", "
                   << Error::ToString(tmp_err);
        Delete(id, &tmp_err);
      }
    }
  }

  bool InitInstall(const DlcSet& requested_install, ErrorPtr* err) {
    DCHECK(!IsInstalling());

    ErrorPtr tmp_err;
    for (const auto& id : requested_install) {
      if (!IsSupported(id)) {
        *err = Error::Create(
            kErrorInvalidDlc,
            base::StringPrintf("Trying to install unsupported DLC=%s",
                               id.c_str()));
        if (!CancelInstall(&tmp_err))
          LOG(ERROR) << "Failed during install initialization: "
                     << Error::ToString(tmp_err);
        return false;
      }
      switch (GetInfo(id).state.state()) {
        case DlcState::NOT_INSTALLED:
          if (!Create(id, err)) {
            if (!CancelInstall(&tmp_err))
              LOG(ERROR) << "Failed during install initialization: "
                         << Error::ToString(tmp_err);
            return false;
          }
          break;
        case DlcState::INSTALLED:
          TryMount(id);
          break;
        case DlcState::INSTALLING:
        default:
          NOTREACHED();
          return false;
      }
      // Failure to set the metadata flags should not fail the install.
      if (!SystemState::Get()->update_engine()->SetDlcActiveValue(true, id,
                                                                  &tmp_err)) {
        LOG(WARNING) << "Update Engine failed to set DLC to active:" << id
                     << (tmp_err ? Error::ToString(tmp_err)
                                 : "Missing error from update engine proxy.");
      }
    }
    return true;
  }

  bool FinishInstall(ErrorPtr* err) {
    bool ret = true;
    for (const auto& pr : supported_) {
      const auto& id = pr.first;
      const auto& info = pr.second;
      if (info.state.state() != DlcState::INSTALLING)
        continue;
      string mount_point;
      ErrorPtr tmp_err;
      if (!Mount(id, &mount_point, &tmp_err)) {
        LOG(ERROR) << "Failed during install finalization: "
                   << Error::ToString(tmp_err);
        if (!Delete(id, &tmp_err))
          LOG(ERROR) << "Failed during install finalization: "
                     << Error::ToString(tmp_err);
        ret = false;
        continue;
      }
      SetInstalled(id, GetDlcRoot(FilePath(mount_point)).value());
    }
    if (!ret)
      *err =
          Error::Create(kErrorInternal, "Not all DLC(s) successfully mounted.");
    return ret;
  }

  bool CancelInstall(ErrorPtr* err) {
    bool ret = true;
    if (!IsInstalling()) {
      LOG(WARNING) << "No install started to being with, nothing to cancel.";
      return ret;
    }
    for (const auto& pr : supported_) {
      const auto& id = pr.first;
      const auto& info = pr.second;
      if (info.state.state() != DlcState::INSTALLING)
        continue;
      // Consider as not installed even if delete fails below, correct errors
      // will be propagated later and should not block on further installs.
      SetNotInstalled(id);
      ErrorPtr tmp_err;
      if (!Delete(id, &tmp_err)) {
        PLOG(ERROR) << "Failed during install cancellation: "
                    << Error::ToString(tmp_err);
        ret = false;
      }
    }
    if (!ret)
      *err = Error::Create(kErrorInternal,
                           "Not all installing DLC(s) successfully cancelled.");
    return ret;
  }

  // Deletes all directories related to the given DLC |id|.
  bool Delete(const string& id, ErrorPtr* err) {
    vector<string> undeleted_paths;
    for (const auto& path : {JoinPaths(content_dir_, id)}) {
      if (!base::DeleteFile(path, true)) {
        PLOG(ERROR) << "Failed to delete path=" << path;
        undeleted_paths.push_back(path.value());
      }
      // Failure to set DLC to inactive should not fail uninstall.
      ErrorPtr tmp_err;
      if (!SystemState::Get()->update_engine()->SetDlcActiveValue(false, id,
                                                                  &tmp_err))
        LOG(WARNING) << "Failed to set DLC(" << id << ") to inactive."
                     << (tmp_err ? Error::ToString(tmp_err)
                                 : "Missing error from update engine proxy.");
    }
    bool ret = undeleted_paths.empty();
    if (!ret) {
      *err = Error::Create(
          kErrorInternal,
          base::StringPrintf("DLC directories (%s) could not be deleted.",
                             base::JoinString(undeleted_paths, ",").c_str()));
    }
    SetNotInstalled(id);
    return ret;
  }

  bool Mount(const string& id, string* mount_point, ErrorPtr* err) {
    if (!image_loader_proxy_->LoadDlcImage(
            id, GetDlcPackage(id),
            current_boot_slot_ == BootSlot::Slot::A ? imageloader::kSlotNameA
                                                    : imageloader::kSlotNameB,
            mount_point, nullptr, kImageLoaderTimeoutMs)) {
      *err = Error::Create(kErrorInternal,
                           "Imageloader is unavailable for LoadDlcImage().");
      return false;
    }
    if (mount_point->empty()) {
      *err = Error::Create(kErrorInternal,
                           "Imageloader LoadDlcImage() call failed.");
      return false;
    }
    return true;
  }

  bool Unmount(const string& id, ErrorPtr* err) {
    bool success = false;
    if (!image_loader_proxy_->UnloadDlcImage(id, GetDlcPackage(id), &success,
                                             nullptr, kImageLoaderTimeoutMs)) {
      *err = Error::Create(kErrorInternal,
                           "Imageloader is unavailable for UnloadDlcImage().");
      return false;
    }
    if (!success) {
      *err = Error::Create(kErrorInternal,
                           "Imageloader UnloadDlcImage() call failed.");
      return false;
    }
    return true;
  }

 private:
  string GetDlcPackage(const DlcId& id) {
    return *(ScanDirectory(JoinPaths(manifest_dir_, id)).begin());
  }

  void SetNotInstalled(const DlcId& id) {
    DCHECK(IsSupported(id));
    supported_[id] = DlcInfo(DlcState::NOT_INSTALLED);
  }

  void SetInstalling(const DlcId& id) {
    DCHECK(IsSupported(id));
    supported_[id] = DlcInfo(DlcState::INSTALLING);
  }

  void SetInstalled(const DlcId& id, const DlcRoot& root) {
    DCHECK(IsSupported(id));
    supported_[id] = DlcInfo(DlcState::INSTALLED, root);
  }

  // Returns true if the DLC module has a boolean true for 'preload-allowed'
  // attribute in the manifest for the given |id| and |package|.
  bool IsDlcPreloadAllowed(const base::FilePath& dlc_manifest_path,
                           const std::string& id) {
    imageloader::Manifest manifest;
    if (!GetDlcManifest(dlc_manifest_path, id, GetDlcPackage(id), &manifest)) {
      // Failing to read the manifest will be considered a preloading blocker.
      return false;
    }
    return manifest.preload_allowed();
  }

  // Create the DLC |id| and |package| directories if they don't exist.
  bool CreateDlcPackagePath(const string& id,
                            const string& package,
                            ErrorPtr* err) {
    FilePath content_path_local = JoinPaths(content_dir_, id);
    FilePath content_package_path = JoinPaths(content_dir_, id, package);

    // Create the DLC ID directory with correct permissions.
    if (!CreateDir(content_path_local)) {
      *err = Error::Create(
          kErrorInternal,
          base::StringPrintf("Failed to create directory for DLC=%s",
                             id.c_str()));
      return false;
    }
    // Create the DLC package directory with correct permissions.
    if (!CreateDir(content_package_path)) {
      *err = Error::Create(
          kErrorInternal,
          base::StringPrintf("Failed to create package directory for DLC=%s",
                             id.c_str()));
      return false;
    }
    return true;
  }

  bool Create(const string& id, ErrorPtr* err) {
    if (!IsSupported(id)) {
      *err = Error::Create(
          kErrorInvalidDlc,
          base::StringPrintf("Cannot create unsupported DLC=%s", id.c_str()));
      return false;
    }

    const string& package = GetDlcPackage(id);
    FilePath content_path_local = JoinPaths(content_dir_, id);

    if (base::PathExists(content_path_local)) {
      *err = Error::Create(
          kErrorInternal,
          base::StringPrintf("Cannot create already existing DLC=%s",
                             id.c_str()));
      return false;
    }

    if (!CreateDlcPackagePath(id, package, err))
      return false;

    imageloader::Manifest manifest;
    if (!dlcservice::GetDlcManifest(manifest_dir_, id, package, &manifest)) {
      *err =
          Error::Create(kErrorInternal,
                        base::StringPrintf(
                            "Cannot read the manifest for DLC=%s", id.c_str()));
      return false;
    }
    int64_t image_size = manifest.preallocated_size();
    if (image_size <= 0) {
      *err =
          Error::Create(kErrorInternal,
                        base::StringPrintf("Preallocated size=%" PRId64
                                           " in manifest is illegal for DLC=%s",
                                           image_size, id.c_str()));
      return false;
    }

    // Creates image A.
    FilePath image_a_path =
        GetDlcImagePath(content_dir_, id, package, BootSlot::Slot::A);
    if (!CreateFile(image_a_path, image_size)) {
      *err = Error::Create(
          kErrorAllocation,
          base::StringPrintf("Failed to create slot A image file for DLC=%s",
                             id.c_str()));
      return false;
    }

    // Creates image B.
    FilePath image_b_path =
        GetDlcImagePath(content_dir_, id, package, BootSlot::Slot::B);
    if (!CreateFile(image_b_path, image_size)) {
      *err = Error::Create(
          kErrorAllocation,
          base::StringPrintf("Failed to create slot B image file for DLC=%s",
                             id.c_str()));
      return false;
    }

    SetInstalling(id);
    return true;
  }

  // Validate that:
  //  - [1] Inactive image for a |dlc_id| exists and create it if missing.
  //    -> Failure to do so returns false.
  //  - [2] Active and inactive images both are the same size and try fixing for
  //        certain scenarios after update only.
  //    -> Failure to do so only logs error.
  bool ValidateImageFiles(const string& id, ErrorPtr* err) {
    string mount_point;
    const auto& package = GetDlcPackage(id);
    FilePath inactive_img_path = GetDlcImagePath(
        content_dir_, id, package,
        current_boot_slot_ == BootSlot::Slot::A ? BootSlot::Slot::B
                                                : BootSlot::Slot::A);

    imageloader::Manifest manifest;
    if (!dlcservice::GetDlcManifest(manifest_dir_, id, package, &manifest)) {
      return false;
    }
    int64_t max_allowed_img_size = manifest.preallocated_size();

    // [1]
    if (!base::PathExists(inactive_img_path)) {
      LOG(WARNING) << "The DLC image " << inactive_img_path.value()
                   << " does not exist.";
      if (!CreateDlcPackagePath(id, package, err))
        return false;
      if (!CreateFile(inactive_img_path, max_allowed_img_size)) {
        // Don't make this error |kErrorAllocation|, this is during startup and
        // should be considered and internal problem of keeping DLC(s) in a
        // completely valid state.
        *err = Error::Create(
            kErrorInternal,
            base::StringPrintf("Failed to create inactive image (%s) during "
                               "validation for DLC=%s",
                               inactive_img_path.value().c_str(), id.c_str()));
        return false;
      }
    }

    // Different scenarios possible to hit this flow:
    //  - Inactive and manifest size are the same -> Do nothing.
    //
    // TODO(crbug.com/943780): This requires further design updates to both
    //  dlcservice and upate_engine in order to fully handle. Solution pending.
    //  - Update applied and not rebooted -> Do nothing. A lot more corner cases
    //    than just always keeping active and inactive image sizes the same.
    //
    //  - Update applied and rebooted -> Try fixing up inactive image.
    // [2]
    int64_t inactive_img_size;
    if (!base::GetFileSize(inactive_img_path, &inactive_img_size)) {
      LOG(ERROR) << "Failed to get DLC (" << id << ") size.";
    } else {
      // When |inactive_img_size| is less than the size permitted in the
      // manifest, this means that we rebooted into an update.
      if (inactive_img_size < max_allowed_img_size) {
        // Only increasing size, the inactive DLC is still usable in case of
        // reverts.
        if (!ResizeFile(inactive_img_path, max_allowed_img_size)) {
          LOG(ERROR)
              << "Failed to increase inactive image, update_engine may "
                 "face problems in updating when stateful is full later.";
        }
      }
    }

    return true;
  }

  // Helper used to load in (copy + cleanup) preloadable files for the |id|.
  bool PreloadedCopier(const string& id) {
    const auto& package = GetDlcPackage(id);
    FilePath image_preloaded_path =
        JoinPaths(preloaded_content_dir_, id, package, kDlcImageFileName);
    FilePath image_a_path =
        GetDlcImagePath(content_dir_, id, package, BootSlot::Slot::A);
    FilePath image_b_path =
        GetDlcImagePath(content_dir_, id, package, BootSlot::Slot::B);

    // Check the size of file to copy is valid.
    imageloader::Manifest manifest;
    if (!dlcservice::GetDlcManifest(manifest_dir_, id, package, &manifest)) {
      LOG(ERROR) << "Failed to get manifest for DLC=" << id;
      return false;
    }
    int64_t max_allowed_image_size = manifest.preallocated_size();
    // Scope the |image_preloaded| file so it always closes before deleting.
    {
      int64_t image_preloaded_size;
      if (!base::GetFileSize(image_preloaded_path, &image_preloaded_size)) {
        LOG(ERROR) << "Failed to get preloaded DLC (" << id << ") size.";
        return false;
      }
      if (image_preloaded_size > max_allowed_image_size) {
        LOG(ERROR) << "Preloaded DLC (" << id << ") is ("
                   << image_preloaded_size
                   << ") larger than the preallocated size ("
                   << max_allowed_image_size << ") in manifest.";
        return false;
      }
    }

    // Based on |current_boot_slot_|, copy the preloadable image.
    FilePath image_boot_path, image_non_boot_path;
    switch (current_boot_slot_) {
      case BootSlot::Slot::A:
        image_boot_path = image_a_path;
        image_non_boot_path = image_b_path;
        break;
      case BootSlot::Slot::B:
        image_boot_path = image_b_path;
        image_non_boot_path = image_a_path;
        break;
      default:
        NOTREACHED();
        return false;
    }
    // TODO(kimjae): when preloaded images are place into unencrypted, this
    // operation can be a move.
    if (!CopyAndResizeFile(image_preloaded_path, image_boot_path,
                           max_allowed_image_size)) {
      LOG(ERROR) << "Failed to preload DLC (" << id << ") into boot slot.";
      return false;
    }

    return true;
  }

  void TryMount(const DlcId& id) {
    const auto info = GetInfo(id);
    if (!base::PathExists(base::FilePath(info.root))) {
      string mount_point;
      ErrorPtr tmp_err;
      if (Mount(id, &mount_point, &tmp_err))
        SetInstalled(id, GetDlcRoot(FilePath(mount_point)).value());
      else
        LOG(ERROR) << "DLC thought to have been installed, but maybe is in a "
                   << "bad state. DLC=" << id << ", "
                   << Error::ToString(tmp_err);
    }
  }

  org::chromium::ImageLoaderInterfaceProxyInterface* image_loader_proxy_;

  FilePath manifest_dir_;
  FilePath preloaded_content_dir_;
  FilePath content_dir_;

  BootSlot::Slot current_boot_slot_;

  DlcMap supported_;
};

DlcManager::DlcManager() {
  impl_ = std::make_unique<DlcManagerImpl>();
}

DlcManager::~DlcManager() = default;

bool DlcManager::IsInstalling() {
  return impl_->IsInstalling();
}

DlcModuleList DlcManager::GetInstalled() {
  return ToDlcModuleList(impl_->GetSupported(),
                         [](const DlcId&, const DlcInfo& info) {
                           return info.state.state() == DlcState::INSTALLED;
                         });
}

DlcModuleList DlcManager::GetSupported() {
  return ToDlcModuleList(impl_->GetSupported(),
                         [](const DlcId&, const DlcInfo&) { return true; });
}

bool DlcManager::GetState(const DlcId& id, DlcState* state, ErrorPtr* err) {
  DCHECK(state);
  DCHECK(err);
  if (!impl_->IsSupported(id)) {
    *err = Error::Create(
        kErrorInvalidDlc,
        base::StringPrintf("Cannot get state for unsupported DLC=%s",
                           id.c_str()));
    return false;
  }

  *state = impl_->GetInfo(id).state;
  return true;
}

void DlcManager::LoadDlcModuleImages() {
  impl_->LoadDlcModuleImages();
  impl_->PreloadDlcModuleImages();
}

bool DlcManager::InitInstall(const DlcModuleList& dlc_module_list,
                             ErrorPtr* err) {
  DCHECK(err);
  const auto dlc_set =
      ToDlcSet(dlc_module_list, [](const DlcModuleInfo&) { return true; });

  if (dlc_set.empty()) {
    *err = Error::Create(kErrorInvalidDlc,
                         "Must provide at least one DLC to install.");
    return false;
  }

  return impl_->InitInstall(dlc_set, err);
}

DlcModuleList DlcManager::GetMissingInstalls() {
  // Only return the DLC(s) that aren't already installed.
  return ToDlcModuleList(impl_->GetSupported(),
                         [](const DlcId&, const DlcInfo& info) {
                           return info.state.state() == DlcState::INSTALLING;
                         });
}

bool DlcManager::FinishInstall(ErrorPtr* err) {
  DCHECK(err);
  return impl_->FinishInstall(err);
}

bool DlcManager::CancelInstall(ErrorPtr* err) {
  return impl_->CancelInstall(err);
}

bool DlcManager::Delete(const string& id, ErrorPtr* err) {
  DCHECK(err);
  if (!impl_->IsSupported(id)) {
    *err = Error::Create(
        kErrorInvalidDlc,
        base::StringPrintf("Trying to delete unsupported DLC=%s", id.c_str()));
    return false;
  }
  switch (impl_->GetInfo(id).state.state()) {
    case DlcState::NOT_INSTALLED:
      LOG(WARNING) << "Trying to uninstall not installed DLC=" << id;
      return true;
    case DlcState::INSTALLING:
      *err = Error::Create(
          kErrorBusy,
          base::StringPrintf("Trying to delete a currently installing DLC=%s",
                             id.c_str()));
      return false;
    case DlcState::INSTALLED:
      return impl_->Unmount(id, err) && impl_->Delete(id, err);
    default:
      NOTREACHED();
      return false;
  }
}

}  // namespace dlcservice
