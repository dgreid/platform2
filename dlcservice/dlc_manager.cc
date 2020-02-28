// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/dlc_manager.h"

#include <set>
#include <utility>
#include <vector>

#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/message_loops/message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dlcservice/dbus-constants.h>

#include "dlcservice/system_state.h"
#include "dlcservice/utils.h"

using base::Callback;
using base::File;
using base::FilePath;
using std::string;
using std::unique_ptr;
using std::vector;

namespace dlcservice {

namespace {
// Timeout in ms for DBus method calls into imageloader.
constexpr int kImageLoaderTimeoutMs = 5000;

void LogOrSetError(const string& err_code_in,
                   const string& err_msg_in,
                   string* err_code_out,
                   string* err_msg_out) {
  if (err_code_out)
    *err_code_out = err_code_in;
  if (err_msg_out)
    *err_msg_out = err_msg_in;
  if (!err_code_out && !err_msg_out)
    LOG(ERROR) << err_code_in << "|" << err_msg_in;
}
}  // namespace

const char kDlcMetadataActiveValue[] = "1";
// Keep kDlcMetadataFilePingActive in sync with update_engine's.
const char kDlcMetadataFilePingActive[] = "active";

class DlcManager::DlcManagerImpl {
 public:
  DlcManagerImpl() {
    const auto system_state = SystemState::Get();
    image_loader_proxy_ = system_state->image_loader();
    manifest_dir_ = system_state->manifest_dir();
    preloaded_content_dir_ = system_state->preloaded_content_dir();
    content_dir_ = system_state->content_dir();
    metadata_dir_ = system_state->metadata_dir();

    string boot_disk_name;
    if (!system_state->boot_slot().GetCurrentSlot(&boot_disk_name,
                                                  &current_boot_slot_))
      LOG(FATAL) << "Can not get current boot slot.";

    // Initialize supported DLC modules.
    for (const auto& id : ScanDirectory(manifest_dir_))
      supported_dlcs_[id] = DlcInfo(DlcState::NOT_INSTALLED);
  }
  ~DlcManagerImpl() = default;

  bool IsBusy() {
    return std::any_of(std::begin(supported_dlcs_), std::end(supported_dlcs_),
                       [](const decltype(supported_dlcs_)::value_type& pr) {
                         return pr.second.state.state() == DlcState::INSTALLING;
                       });
  }

  bool IsSupported(const DlcId& id) {
    return supported_dlcs_.find(id) != supported_dlcs_.end();
  }

  DlcMap GetInstalled() {
    return FilterState(supported_dlcs_, DlcState::INSTALLED);
  }

  const DlcMap& GetSupported() { return supported_dlcs_; }

  // Loads the preloadable DLC(s) from |preloaded_content_dir_| by scanning the
  // preloaded DLC(s) and verifying the validity to be preloaded before doing
  // so.
  // Note: Keep each preload separate to keep isolation.
  void PreloadImages() {
    string err_code, err_msg;
    // Load all preloaded DLC modules into |content_dir_| one by one.
    for (auto id : ScanDirectory(preloaded_content_dir_)) {
      if (!IsSupported(id)) {
        LOG(ERROR) << "Can't preload an unsupported DLC: " << id;
        continue;
      }

      if (!IsPreloadAllowed(id)) {
        LOG(ERROR) << "Preloading is not allowed for DLC: " << id;
        continue;
      }

      DlcSet s = {id};
      if (!InitInstall(s, &err_code, &err_msg)) {
        LOG(ERROR) << "Preloading failed to create DLC: " << id;
        continue;
      }

      if (!PreloadCopier(id)) {
        LOG(ERROR) << "Please check for previous errors, something went wrong "
                   << "during preloading DLC: " << id;
        CancelInstall(kErrorInternal, &err_code, &err_msg);
        continue;
      }

      // TODO(crbug.com/1059445): Validate before finishing the install.

      // When the copying is successful, go ahead and finish installation.
      if (!FinishInstall(&err_code, &err_msg)) {
        LOG(ERROR) << "Failed to finish installing preloaded DLC (" << id
                   << ") because: " << err_code << "|" << err_msg;
        continue;
      }

      // Delete the preloaded DLC only after both copies into A and B succeed as
      // well as mounting the currently active slot image.
      FilePath image_preloaded_path = JoinPaths(
          preloaded_content_dir_, id, GetPackage(id), kDlcImageFileName);
      if (!base::DeleteFile(image_preloaded_path.DirName().DirName(), true)) {
        PLOG(ERROR) << "Failed to delete preloaded DLC: " << id;
        continue;
      }
    }
  }

  // Sets the initial state when dlcservice is starting up with DLC(s) on disk.
  void LoadImages() {
    // Hard refresh from cache directory.
    for (const auto& id : ScanDirectory(content_dir_)) {
      if (!IsSupported(id)) {
        LOG(ERROR) << "Found unsupported DLC that is installed: " << id;
        Delete(id);
        continue;
      }
      string err_code, err_msg;
      // Create the metadata directory if it doesn't exist.
      if (!CreateMetadata(id, &err_code, &err_msg))
        LOG(WARNING) << "Failed to create metadata DLC (" << id
                     << "): " << err_code << "|" << err_msg;

      // Validate images are in a good state.
      if (!ValidateImageFiles(id, &err_code, &err_msg)) {
        LOG(ERROR) << "Failed to validate DLC (" << id << "): " << err_code
                   << "|" << err_msg;
        Delete(id, err_code);
      }

      // If |root| exists set it, else try mounting.
      string mount;
      DlcRoot root = GetInfo(id).root;
      if (base::PathExists(base::FilePath(root))) {
        SetInstalled(id, root);
      } else if (Mount(id, &mount, &err_code, &err_msg)) {
        SetInstalled(id, GetRoot(FilePath(mount)).value());
      } else {
        LOG(ERROR) << "Failed to mount DLC (" << id << "): " << err_code << "|"
                   << err_msg;
        Delete(id, err_code);
      }
    }
  }

  bool GetState(const DlcId& id,
                DlcState* state,
                string* err_code,
                string* err_msg) {
    if (!IsSupported(id)) {
      LogOrSetError(kErrorInvalidDlc,
                    "Trying to get state of unsupported DLC: " + id, err_code,
                    err_msg);
      return false;
    }
    *state = GetInfo(id).state;
    return true;
  }

  bool InitInstall(const DlcSet& ids, string* err_code, string* err_msg) {
    CHECK(!IsBusy());
    // Check earlier if any requested DLC(s) are unsupported.
    for (const auto& id : ids) {
      if (!IsSupported(id)) {
        LogOrSetError(kErrorInvalidDlc,
                      "Trying to install an unsupported DLC: " + id, err_code,
                      err_msg);
        return false;
      }
    }
    for (const auto& id : ids) {
      string local_err_code, local_err_msg;
      switch (GetInfo(id).state.state()) {
        case DlcState::NOT_INSTALLED: {
          if (!Create(id, err_code, err_msg)) {
            CancelInstall(*err_code, &local_err_code, &local_err_msg);
            return false;
          }
          break;
        }
        case DlcState::INSTALLING: {
          CancelInstall(kErrorInternal, &local_err_code, &local_err_msg);
          return false;
        }
        case DlcState::INSTALLED:
          break;
        default:
          NOTREACHED();
      }
      // Failure to set the metadata flags should not fail the install.
      if (!SetActive(id, &local_err_code, &local_err_msg)) {
        LOG(WARNING) << "Failed to set active DLC (" << id
                     << "): " << local_err_code << "|" << local_err_msg;
      }
    }
    return true;
  }

  DlcMap GetInstalling() {
    // NOTE: Do not refresh before returning as it's an internal state clearer
    // from what |InitInstall()| setup.
    return FilterState(supported_dlcs_, DlcState::INSTALLING);
  }

  bool FinishInstall(string* err_code, string* err_msg) {
    for (const auto& pr : supported_dlcs_) {
      const DlcId& id = pr.first;
      if (!IsInstalling(id))
        continue;
      string mount_point;
      if (!Mount(id, &mount_point, err_code, err_msg))
        Delete(id, *err_code);
      else
        SetInstalled(id, GetRoot(FilePath(mount_point)).value());
    }
    return true;
  }

  bool CancelInstall(const string& set_err_code,
                     string* err_code,
                     string* err_msg) {
    bool ret = true;
    for (const auto& pr : supported_dlcs_) {
      const DlcId& id = pr.first;
      if (!IsInstalling(id))
        continue;
      if (!Delete(id, set_err_code, err_code, err_msg))
        ret = false;
    }
    return ret;
  }

  // Deletes all directories related to the given DLC |id|. If |err_code| or
  // |err_msg| are passed in, they will be set. Otherwise error will be logged.
  bool Delete(const DlcId& id,
              const string& set_err_code = kErrorNone,
              string* err_code = nullptr,
              string* err_msg = nullptr) {
    if (IsInstalled(id)) {
      string local_err_code, local_err_msg;
      if (!Unmount(id, &local_err_code, &local_err_msg)) {
        LogOrSetError(local_err_code, local_err_msg, err_code, err_msg);
        return false;
      }
    }
    vector<string> undeleted_paths;
    for (const auto& path :
         {JoinPaths(content_dir_, id), JoinPaths(metadata_dir_, id)}) {
      if (!base::DeleteFile(path, true))
        undeleted_paths.push_back(path.value());
    }
    bool ret = undeleted_paths.empty();
    if (!ret) {
      LogOrSetError(
          kErrorInternal,
          base::StringPrintf("DLC directories (%s) could not be deleted.",
                             base::JoinString(undeleted_paths, ",").c_str()),
          err_code, err_msg);
    }
    SetNotInstalled(id, set_err_code);
    return ret;
  }

  bool Mount(const string& id,
             string* mount_point,
             string* err_code,
             string* err_msg) {
    if (!image_loader_proxy_->LoadDlcImage(
            id, GetPackage(id),
            current_boot_slot_ == BootSlot::Slot::A ? imageloader::kSlotNameA
                                                    : imageloader::kSlotNameB,
            mount_point, nullptr, kImageLoaderTimeoutMs)) {
      *err_code = kErrorInternal;
      *err_msg = "Imageloader is unavailable.";
      return false;
    }
    if (mount_point->empty()) {
      *err_code = kErrorInternal;
      *err_msg = "Imageloader LoadDlcImage() call failed.";
      return false;
    }
    return true;
  }

  bool Unmount(const string& id, string* err_code, string* err_msg) {
    bool success = false;
    if (!image_loader_proxy_->UnloadDlcImage(id, GetPackage(id), &success,
                                             nullptr, kImageLoaderTimeoutMs)) {
      *err_code = kErrorInternal;
      *err_msg = "Imageloader is unavailable.";
      return false;
    }
    if (!success) {
      *err_code = kErrorInternal;
      *err_msg = "Imageloader UnloadDlcImage() call failed for DLC: " + id;
      return false;
    }
    return true;
  }

 private:
  bool IsInstalling(const DlcId& id) {
    return GetInfo(id).state.state() == DlcState::INSTALLING;
  }

  bool IsInstalled(const DlcId& id) {
    return GetInfo(id).state.state() == DlcState::INSTALLED;
  }

  void SetInstalling(const DlcId& id) {
    supported_dlcs_[id] = DlcInfo(DlcState::INSTALLING);
  }

  void SetInstalled(const DlcId& id, const DlcRoot& root) {
    supported_dlcs_[id] = DlcInfo(DlcState::INSTALLED, root);
  }

  void SetNotInstalled(const DlcId& id, const string& err_code) {
    supported_dlcs_[id] = DlcInfo(DlcState::NOT_INSTALLED, "", err_code);
  }

  DlcInfo GetInfo(const DlcId& id) { return supported_dlcs_[id]; }

  string GetPackage(const DlcId& id) {
    const auto& packages = ScanDirectory(JoinPaths(manifest_dir_, id));
    if (packages.empty())
      LOG(FATAL) << "No package exists for DLC: " << id;
    if (packages.size() > 1)
      LOG(WARNING) << "Taking the first alphabetical package among many for "
                   << "DLC: " << id;
    return *packages.begin();
  }

  // Returns true if the DLC module has a boolean true for 'preload-allowed'
  // attribute in the manifest for the given |id| and |package|.
  bool IsPreloadAllowed(const std::string& id) {
    imageloader::Manifest manifest;
    if (!GetManifest(manifest_dir_, id, GetPackage(id), &manifest)) {
      // Failing to read the manifest will be considered a preloading blocker.
      return false;
    }
    return manifest.preload_allowed();
  }

  bool CreateMetadata(const std::string& id,
                      string* err_code,
                      string* err_msg) {
    // Create the DLC ID metadata directory with correct permissions if it
    // doesn't exist.
    FilePath metadata_path_local = JoinPaths(metadata_dir_, id);
    if (!base::PathExists(metadata_path_local)) {
      if (!CreateDir(metadata_path_local)) {
        *err_code = kErrorInternal;
        *err_msg = "Failed to create the DLC metadata directory for DLC:" + id;
        return false;
      }
    }
    return true;
  }

  bool SetActive(const string& id, string* err_code, string* err_msg) {
    // Create the metadata directory if it doesn't exist.
    if (!CreateMetadata(id, err_code, err_msg))
      return false;
    auto active_metadata_path =
        JoinPaths(metadata_dir_, id, kDlcMetadataFilePingActive);
    if (!WriteToFile(active_metadata_path, kDlcMetadataActiveValue)) {
      *err_code = kErrorInternal;
      *err_msg = "Failed to write into active metadata file: " +
                 active_metadata_path.value();
      return false;
    }
    return true;
  }

  // Create the DLC |id| and |package| directories if they don't exist.
  bool CreateDlcPackagePath(const string& id,
                            const string& package,
                            string* err_code,
                            string* err_msg) {
    FilePath content_path_local = JoinPaths(content_dir_, id);
    FilePath content_package_path = JoinPaths(content_dir_, id, package);

    // Create the DLC ID directory with correct permissions.
    if (!CreateDir(content_path_local)) {
      *err_code = kErrorInternal;
      *err_msg = "Failed to create DLC (" + id + ") directory";
      return false;
    }
    // Create the DLC package directory with correct permissions.
    if (!CreateDir(content_package_path)) {
      *err_code = kErrorInternal;
      *err_msg = "Failed to create DLC (" + id + ") package directory";
      return false;
    }
    return true;
  }

  bool Create(const string& id, string* err_code, string* err_msg) {
    CHECK(err_code);
    CHECK(err_msg);

    if (!IsSupported(id)) {
      *err_code = kErrorInvalidDlc;
      *err_msg = "The DLC (" + id + ") provided is not supported.";
      return false;
    }

    const string& package = GetPackage(id);
    FilePath content_path_local = JoinPaths(content_dir_, id);

    if (base::PathExists(content_path_local)) {
      *err_code = kErrorInternal;
      *err_msg = "The DLC (" + id + ") is installed or duplicate.";
      return false;
    }

    if (!CreateDlcPackagePath(id, package, err_code, err_msg))
      return false;

    // Creates DLC module storage.
    // TODO(xiaochu): Manifest currently returns a signed integer, which means
    // it will likely fail for modules >= 2 GiB in size.
    // https://crbug.com/904539
    imageloader::Manifest manifest;
    if (!GetManifest(manifest_dir_, id, package, &manifest)) {
      *err_code = kErrorInternal;
      *err_msg = "Failed to create DLC (" + id + ") manifest.";
      return false;
    }
    int64_t image_size = manifest.preallocated_size();
    if (image_size <= 0) {
      *err_code = kErrorInternal;
      *err_msg = "Preallocated size in manifest is illegal: " +
                 base::Int64ToString(image_size);
      return false;
    }

    // Creates image A.
    FilePath image_a_path =
        GetImagePath(content_dir_, id, package, BootSlot::Slot::A);
    if (!CreateFile(image_a_path, image_size)) {
      *err_code = kErrorAllocation;
      *err_msg = "Failed to create slot A DLC (" + id + ") image file.";
      return false;
    }

    // Creates image B.
    FilePath image_b_path =
        GetImagePath(content_dir_, id, package, BootSlot::Slot::B);
    if (!CreateFile(image_b_path, image_size)) {
      *err_code = kErrorAllocation;
      *err_msg = "Failed to create slot B DLC (" + id + ") image file.";
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
  bool ValidateImageFiles(const string& id, string* err_code, string* err_msg) {
    string mount_point;
    const string& package = GetPackage(id);
    FilePath inactive_img_path = GetImagePath(
        content_dir_, id, package,
        current_boot_slot_ == BootSlot::Slot::A ? BootSlot::Slot::B
                                                : BootSlot::Slot::A);

    imageloader::Manifest manifest;
    if (!GetManifest(manifest_dir_, id, package, &manifest)) {
      return false;
    }
    int64_t max_allowed_img_size = manifest.preallocated_size();

    // [1]
    if (!base::PathExists(inactive_img_path)) {
      LOG(WARNING) << "The DLC image " << inactive_img_path.value()
                   << " does not exist.";
      if (!CreateDlcPackagePath(id, package, err_code, err_msg))
        return false;
      if (!CreateFile(inactive_img_path, max_allowed_img_size)) {
        // Don't make this error |kErrorAllocation|, this is during a refresh
        // and should be considered and internal problem of keeping DLC(s) in a
        // completely valid state.
        *err_code = kErrorInternal;
        *err_msg = "Failed to create DLC image during validation: " +
                   inactive_img_path.value();
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
      LOG(ERROR) << "Failed to get size for DLC: " << id;
    } else {
      // When |inactive_img_size| is less than the size permitted in the
      // manifest, this means that we rebooted into an update.
      if (inactive_img_size < max_allowed_img_size) {
        // Only increasing size, the inactive DLC is still usable in case of
        // reverts.
        if (!ResizeFile(inactive_img_path, max_allowed_img_size)) {
          LOG(ERROR)
              << "Failed to increase inactive image, update_engine may face "
              << "problems in updating when stateful is full later for DLC: "
              << id;
        }
      }
    }

    return true;
  }

  // Helper used by |Preload()| to load in (copy + cleanup) preloadable
  // files for the given DLC ID.
  bool PreloadCopier(const string& id) {
    const string& package = GetPackage(id);
    FilePath image_preloaded_path =
        JoinPaths(preloaded_content_dir_, id, package, kDlcImageFileName);
    FilePath image_a_path =
        GetImagePath(content_dir_, id, package, BootSlot::Slot::A);
    FilePath image_b_path =
        GetImagePath(content_dir_, id, package, BootSlot::Slot::B);

    // Check the size of file to copy is valid.
    imageloader::Manifest manifest;
    if (!GetManifest(manifest_dir_, id, package, &manifest)) {
      LOG(ERROR) << "Failed to get manifest for preloaded DLC: " << id;
      return false;
    }
    int64_t max_allowed_image_size = manifest.preallocated_size();
    // Scope the |image_preloaded| file so it always closes before deleting.
    {
      int64_t image_preloaded_size;
      if (!base::GetFileSize(image_preloaded_path, &image_preloaded_size)) {
        LOG(ERROR) << "Failed to get size for preloaded DLC: " << id;
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
    }
    // TODO(kimjae): when preloaded images are place into unencrypted, this
    // operation can be a move.
    if (!CopyAndResizeFile(image_preloaded_path, image_boot_path,
                           max_allowed_image_size)) {
      LOG(ERROR) << "Failed to preload into boot slot for DLC: " << id;
      return false;
    }

    return true;
  }

  org::chromium::ImageLoaderInterfaceProxyInterface* image_loader_proxy_;

  FilePath manifest_dir_;
  FilePath preloaded_content_dir_;
  FilePath content_dir_;
  FilePath metadata_dir_;

  BootSlot::Slot current_boot_slot_;

  DlcMap supported_dlcs_;
};

DlcManager::DlcManager() {
  impl_ = std::make_unique<DlcManagerImpl>();
}

DlcManager::~DlcManager() = default;

bool DlcManager::IsBusy() {
  return impl_->IsBusy();
}

DlcModuleList DlcManager::GetInstalled() {
  return ToDlcModuleList(impl_->GetInstalled());
}

DlcModuleList DlcManager::GetSupported() {
  return ToDlcModuleList(impl_->GetSupported());
}

void DlcManager::LoadDlcModuleImages() {
  impl_->PreloadImages();
  impl_->LoadImages();
}

bool DlcManager::GetState(const DlcId& id,
                          DlcState* state,
                          string* err_code,
                          string* err_msg) {
  CHECK(err_code);
  CHECK(err_msg);

  return impl_->GetState(id, state, err_code, err_msg);
}

bool DlcManager::InitInstall(const DlcModuleList& dlc_module_list,
                             string* err_code,
                             string* err_msg) {
  CHECK(err_code);
  CHECK(err_msg);

  DlcSet s = ToDlcSet(dlc_module_list);
  if (s.empty()) {
    *err_code = kErrorInvalidDlc;
    *err_msg = "Must provide at lease one DLC to install.";
    return false;
  }

  return impl_->InitInstall(s, err_code, err_msg);
}

DlcModuleList DlcManager::GetMissingInstalls() {
  return ToDlcModuleList(impl_->GetInstalling());
}

bool DlcManager::FinishInstall(DlcModuleList* dlc_module_list,
                               string* err_code,
                               string* err_msg) {
  CHECK(dlc_module_list);
  CHECK(err_code);
  CHECK(err_msg);

  if (!impl_->FinishInstall(err_code, err_msg))
    return false;

  *dlc_module_list = ToDlcModuleList(impl_->GetSupported());
  return true;
}

bool DlcManager::CancelInstall(const std::string& set_err_code,
                               std::string* err_code,
                               std::string* err_msg) {
  return impl_->CancelInstall(set_err_code, err_code, err_msg);
}

bool DlcManager::Delete(const string& id,
                        const string& set_err_code,
                        std::string* err_code,
                        std::string* err_msg) {
  CHECK(err_code);
  CHECK(err_msg);

  if (!impl_->IsSupported(id)) {
    *err_code = kErrorInvalidDlc;
    *err_msg = "Trying to delete unsupported DLC: " + id;
    return false;
  }

  return impl_->Delete(id, set_err_code, err_code, err_msg);
}

}  // namespace dlcservice
