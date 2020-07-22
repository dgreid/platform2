// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlcservice/dlc.h"

#include <algorithm>
#include <cinttypes>
#include <memory>
#include <utility>
#include <vector>

#include <base/strings/stringprintf.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/dlcservice/dbus-constants.h>

#include "dlcservice/error.h"
#include "dlcservice/prefs.h"
#include "dlcservice/system_state.h"
#include "dlcservice/utils.h"

using base::FilePath;
using brillo::ErrorPtr;
using std::string;
using std::vector;

namespace dlcservice {

// static
vector<FilePath> DlcBase::GetPathsToDelete(const DlcId& id) {
  const auto* system_state = SystemState::Get();
  return {JoinPaths(system_state->content_dir(), id),
          JoinPaths(system_state->dlc_prefs_dir(), id)};
}

// TODO(ahassani): Instead of initialize function, create a factory method so
// we can develop different types of DLC classes.
bool DlcBase::Initialize() {
  const auto* system_state = SystemState::Get();
  const auto& manifest_dir = system_state->manifest_dir();
  package_ = *ScanDirectory(manifest_dir.Append(id_)).begin();
  if (!GetDlcManifest(system_state->manifest_dir(), id_, package_,
                      &manifest_)) {
    // Failing to read the manifest will be considered a blocker.
    LOG(ERROR) << "Failed to read the manifest of DLC " << id_;
    return false;
  }

  const auto& content_dir = system_state->content_dir();
  content_id_path_ = JoinPaths(content_dir, id_);
  content_package_path_ = JoinPaths(content_id_path_, package_);
  prefs_path_ = JoinPaths(system_state->dlc_prefs_dir(), id_);
  prefs_package_path_ = JoinPaths(prefs_path_, package_);
  preloaded_image_path_ = JoinPaths(system_state->preloaded_content_dir(), id_,
                                    package_, kDlcImageFileName);
  ref_count_ = RefCountInterface::Create(manifest_.used_by(), prefs_path_);

  state_.set_state(DlcState::NOT_INSTALLED);
  state_.set_id(id_);
  state_.set_progress(0);
  state_.set_last_error_code(kErrorNone);

  if (manifest_.mount_file_required()) {
    if (!Prefs(prefs_package_path_).Delete(kDlcRootMount))
      LOG(ERROR)
          << "Failed to delete indirect root mount file during Initialization: "
          << JoinPaths(prefs_package_path_, kDlcRootMount);
  }
  is_verified_ =
      Prefs(*this, system_state->active_boot_slot()).Exists(kDlcPrefVerified);
  return true;
}

const DlcId& DlcBase::GetId() const {
  return id_;
}

const std::string& DlcBase::GetName() const {
  return manifest_.name();
}

const std::string& DlcBase::GetDescription() const {
  return manifest_.description();
}

DlcState DlcBase::GetState() const {
  return state_;
}

bool DlcBase::IsInstalling() const {
  return state_.state() == DlcState::INSTALLING;
}

bool DlcBase::IsInstalled() const {
  return state_.state() == DlcState::INSTALLED;
}

bool DlcBase::IsVerified() const {
  return is_verified_;
}

bool DlcBase::HasContent() const {
  for (const auto& path :
       {GetImagePath(BootSlot::Slot::A), GetImagePath(BootSlot::Slot::B)}) {
    if (base::PathExists(path))
      return true;
  }
  return false;
}

uint64_t DlcBase::GetUsedBytesOnDisk() const {
  uint64_t total_size = 0;
  for (const auto& path :
       {GetImagePath(BootSlot::Slot::A), GetImagePath(BootSlot::Slot::B)}) {
    if (!base::PathExists(path))
      continue;
    int64_t size = 0;
    if (!base::GetFileSize(path, &size)) {
      LOG(WARNING) << "Failed to get file size for path: " << path.value();
    }
    total_size += size;
  }
  return total_size;
}

bool DlcBase::IsPreloadAllowed() const {
  return manifest_.preload_allowed();
}

base::FilePath DlcBase::GetRoot() const {
  if (mount_point_.empty())
    return {};
  return JoinPaths(mount_point_, kRootDirectoryInsideDlcModule);
}

bool DlcBase::InstallCompleted(ErrorPtr* err) {
  if (!MarkVerified()) {
    state_.set_last_error_code(kErrorInternal);
    *err = Error::Create(
        FROM_HERE, state_.last_error_code(),
        base::StringPrintf("Failed to mark active DLC=%s as verified.",
                           id_.c_str()));
    return false;
  }
  return true;
}

bool DlcBase::UpdateCompleted(ErrorPtr* err) {
  if (!Prefs(*this, SystemState::Get()->inactive_boot_slot())
           .Create(kDlcPrefVerified)) {
    *err = Error::Create(
        FROM_HERE, kErrorInternal,
        base::StringPrintf("Failed to mark inactive DLC=%s as verified.",
                           id_.c_str()));
    return false;
  }
  return true;
}

FilePath DlcBase::GetImagePath(BootSlot::Slot slot) const {
  return JoinPaths(content_package_path_, BootSlot::ToString(slot),
                   kDlcImageFileName);
}

bool DlcBase::CreateDlc(ErrorPtr* err) {
  // Create content directories.
  for (const auto& path :
       {content_id_path_, content_package_path_, prefs_path_}) {
    if (!CreateDir(path)) {
      *err = Error::CreateInternal(
          FROM_HERE, error::kFailedToCreateDirectory,
          base::StringPrintf("Failed to create directory %s for DLC=%s",
                             path.value().c_str(), id_.c_str()));
      state_.set_last_error_code(Error::GetDbusErrorCode(*err));
      return false;
    }
  }

  // Creates image A and B.
  for (const auto& slot : {BootSlot::Slot::A, BootSlot::Slot::B}) {
    FilePath image_path = GetImagePath(slot);
    if (!CreateFile(image_path, manifest_.preallocated_size())) {
      state_.set_last_error_code(kErrorAllocation);
      *err = Error::Create(
          FROM_HERE, state_.last_error_code(),
          base::StringPrintf("Failed to create image file %s for DLC=%s",
                             image_path.value().c_str(), id_.c_str()));
      return false;
    }
  }

  ChangeState(DlcState::INSTALLING);
  return true;
}

bool DlcBase::MakeReadyForUpdate() const {
  // Deleting the inactive verified pref should always happen before anything
  // else here otherwise if we failed to delete, on a reboot after an update, we
  // might assume the image is verified, which is not.
  if (!Prefs(*this, SystemState::Get()->inactive_boot_slot())
           .Delete(kDlcPrefVerified)) {
    PLOG(ERROR) << "Failed to mark inactive DLC=" << id_ << " as not-verified.";
    return false;
  }

  if (!IsVerified()) {
    return false;
  }

  const FilePath& inactive_image_path =
      GetImagePath(SystemState::Get()->inactive_boot_slot());
  if (!CreateFile(inactive_image_path, manifest_.preallocated_size())) {
    LOG(ERROR) << "Failed to create inactive image "
               << inactive_image_path.value() << " when making DLC=" << id_
               << " ready for update.";
    return false;
  }
  return true;
}

bool DlcBase::MarkVerified() {
  is_verified_ = true;
  return Prefs(*this, SystemState::Get()->active_boot_slot())
      .Create(kDlcPrefVerified);
}

bool DlcBase::MarkUnverified() {
  is_verified_ = false;
  return Prefs(*this, SystemState::Get()->active_boot_slot())
      .Delete(kDlcPrefVerified);
}

bool DlcBase::Verify() {
  auto image_path = GetImagePath(SystemState::Get()->active_boot_slot());
  vector<uint8_t> image_sha256;
  if (!HashFile(image_path, manifest_.size(), &image_sha256)) {
    LOG(ERROR) << "Failed to hash image file: " << image_path.value();
    return false;
  }

  const auto& manifest_image_sha256 = manifest_.image_sha256();
  if (image_sha256 != manifest_image_sha256) {
    LOG(WARNING) << "Verification failed for image file: " << image_path.value()
                 << ". Expected: "
                 << base::HexEncode(manifest_image_sha256.data(),
                                    manifest_image_sha256.size())
                 << " Found: "
                 << base::HexEncode(image_sha256.data(), image_sha256.size());
    return false;
  }

  if (!MarkVerified()) {
    LOG(WARNING) << "Failed to mark the image as verified, but temporarily"
                 << " we assume the image is verified.";
  }
  return true;
}

bool DlcBase::PreloadedCopier(ErrorPtr* err) {
  int64_t preloaded_image_size;
  if (!base::GetFileSize(preloaded_image_path_, &preloaded_image_size)) {
    auto err_str = base::StringPrintf("Failed to get preloaded DLC (%s) size.",
                                      id_.c_str());
    *err = Error::Create(FROM_HERE, kErrorInternal, err_str);
    return false;
  }
  if (preloaded_image_size != manifest_.size()) {
    auto err_str = base::StringPrintf(
        "Preloaded DLC (%s) is (%" PRId64 ") different than the size (%" PRId64
        ") in the manifest.",
        id_.c_str(), preloaded_image_size, manifest_.size());
    *err = Error::Create(FROM_HERE, kErrorInternal, err_str);
    return false;
  }

  // Before touching the image, we need to mark it as unverified.
  MarkUnverified();

  // TODO(kimjae): When preloaded images are place into unencrypted, this
  // operation can be a move.
  FilePath image_path = GetImagePath(SystemState::Get()->active_boot_slot());
  vector<uint8_t> image_sha256;
  if (!CopyAndHashFile(preloaded_image_path_, image_path, manifest_.size(),
                       &image_sha256)) {
    auto err_str =
        base::StringPrintf("Failed to copy preload DLC (%s) into path %s",
                           id_.c_str(), image_path.value().c_str());
    *err = Error::Create(FROM_HERE, kErrorInternal, err_str);
    return false;
  }

  auto manifest_image_sha256 = manifest_.image_sha256();
  if (image_sha256 != manifest_image_sha256) {
    auto err_str = base::StringPrintf(
        "Image is corrupted or modified for DLC=%s. Expected: %s Found: %s",
        id_.c_str(),
        base::HexEncode(manifest_image_sha256.data(),
                        manifest_image_sha256.size())
            .c_str(),
        base::HexEncode(image_sha256.data(), image_sha256.size()).c_str());
    *err = Error::Create(FROM_HERE, kErrorInternal, err_str);
    return false;
  }

  if (!MarkVerified())
    LOG(ERROR) << "Failed to mark the image verified for DLC=" << id_;

  return true;
}

bool DlcBase::Install(ErrorPtr* err) {
  switch (state_.state()) {
    case DlcState::NOT_INSTALLED: {
      bool active_image_existed = IsActiveImagePresent();
      // Always try to create the DLC files and directories to make sure they
      // all exist before we start the install.
      if (!CreateDlc(err)) {
        ErrorPtr tmp_err;
        if (!CancelInstall(*err, &tmp_err))
          LOG(ERROR) << "Failed to cancel the install correctly.";
        return false;
      }

      // Finish the installation for verified images so they can be mounted.
      if (IsVerified()) {
        LOG(INFO) << "Installing already verified DLC=" << id_;
        break;
      }

      // Try verifying images that already existed before creation. If verified,
      // finish the installation so they can be mounted.
      if (active_image_existed && Verify()) {
        LOG(INFO) << "Verified existing, but previously not verified DLC="
                  << id_;
        break;
      }

      // Preload the DLC if possible.
      if (IsPreloadAllowed() && base::PathExists(preloaded_image_path_)) {
        if (!PreloadedCopier(err)) {
          LOG(ERROR)
              << "Preloading failed, so assuming installation failed for DLC="
              << id_;
          ErrorPtr tmp_err;
          if (!CancelInstall(*err, &tmp_err))
            LOG(ERROR) << "Failed to cancel the install from preloading.";
          return false;
        }
        LOG(INFO) << "Preloading DLC=" << id_;
        break;
      }

      if (!IsVerified()) {
        // By now if the image is not verified, it needs to be installed
        // through update_engine. So don't go any further.
        return true;
      }
      break;
    }
    case DlcState::INSTALLING:
      // If the image is already in this state, nothing need to be done. It is
      // already being installed.
      // Skip reporting this scenario to the metrics, since the Install call
      // might be from the same client, and reporting this is not useful.
      return true;
    case DlcState::INSTALLED:
      // If the image is already installed, we need to finish the install so it
      // gets mounted in case it has been unmounted externally.
      break;
    default:
      NOTREACHED();
      return false;
  }

  // Let's try to finish the installation.
  if (!FinishInstall(/*installed_by_ue=*/false, err)) {
    return false;
  }

  // Note: Don't remove preloaded DLC images. F20 transition to provision DLC
  // images will allow for preloading to be deprecated.
  return true;
}

bool DlcBase::FinishInstall(bool installed_by_ue, ErrorPtr* err) {
  DCHECK(err);
  DCHECK(err->get() == NULL);  // Check there is no error set.
  switch (state_.state()) {
    case DlcState::INSTALLED:
    case DlcState::INSTALLING:
      if (!IsVerified()) {
        // If the image is not verified, try to verify it. This is to combat
        // update_engine failing to call into |InstallCompleted()| even after a
        // successful DLC installation.
        if (Verify()) {
          LOG(WARNING) << "Missing verification mark for DLC=" << id_
                       << ", but verified to be a valid image.";
        }
      }
      if (IsVerified() && Mount(err)) {
        break;
      } else {
        // By now, the image is either not verified or it is not mounted.
        // The error is empty only if verification was not successful, since
        // |Mount| would have set the error otherwise.
        if (err->get() == NULL)
          *err = Error::CreateInternal(
              FROM_HERE, error::kFailedToVerifyImage,
              base::StringPrintf("Cannot verify image for DLC=%s",
                                 id_.c_str()));

        SystemState::Get()->metrics()->SendInstallResultFailure(err);
        ErrorPtr tmp_err;
        if (!CancelInstall(*err, &tmp_err))
          LOG(ERROR) << "Failed during install finalization for DLC=" << id_;
        return false;
      }
    case DlcState::NOT_INSTALLED:
      // Should not try to finish install on a not-installed DLC.
    default:
      NOTREACHED();
      return false;
  }

  // Increase the ref count.
  ref_count_->InstalledDlc();

  // Now that we are sure the image is installed, we can go ahead and set it as
  // active. Failure to set the metadata flags should not fail the install.
  SetActiveValue(true);
  SystemState::Get()->metrics()->SendInstallResultSuccess(installed_by_ue);

  return true;
}

bool DlcBase::CancelInstall(const ErrorPtr& err_in, ErrorPtr* err) {
  state_.set_last_error_code(Error::GetDbusErrorCode(err_in));
  ChangeState(DlcState::NOT_INSTALLED);

  // Consider as not installed even if delete fails below, correct errors
  // will be propagated later and should not block on further installs.
  if (!DeleteInternal(err)) {
    LOG(ERROR) << "Failed during install cancellation for DLC=" << id_;
    return false;
  }
  return true;
}

bool DlcBase::Mount(ErrorPtr* err) {
  string mount_point;
  if (!SystemState::Get()->image_loader()->LoadDlcImage(
          id_, package_,
          SystemState::Get()->active_boot_slot() == BootSlot::Slot::A
              ? imageloader::kSlotNameA
              : imageloader::kSlotNameB,
          &mount_point, nullptr, kImageLoaderTimeoutMs)) {
    *err =
        Error::CreateInternal(FROM_HERE, error::kFailedToMountImage,
                              "Imageloader is unavailable for LoadDlcImage().");
    state_.set_last_error_code(Error::GetDbusErrorCode(*err));
    return false;
  }
  if (mount_point.empty()) {
    *err = Error::CreateInternal(FROM_HERE, error::kFailedToMountImage,
                                 "Imageloader LoadDlcImage() call failed.");
    state_.set_last_error_code(Error::GetDbusErrorCode(*err));
    return false;
  }
  mount_point_ = FilePath(mount_point);

  // Creates a file which holds the root mount path, allowing for indirect
  // access for processes/scripts which can't access DBus.
  if (manifest_.mount_file_required() &&
      Prefs(prefs_package_path_).SetKey(kDlcRootMount, GetRoot().value()))
    LOG(ERROR) << "Failed to create indirect root mount file: "
               << JoinPaths(prefs_package_path_, kDlcRootMount);

  ChangeState(DlcState::INSTALLED);
  return true;
}

bool DlcBase::Unmount(ErrorPtr* err) {
  bool success = false;
  if (!SystemState::Get()->image_loader()->UnloadDlcImage(
          id_, package_, &success, nullptr, kImageLoaderTimeoutMs)) {
    state_.set_last_error_code(kErrorInternal);
    *err = Error::Create(FROM_HERE, state_.last_error_code(),
                         "Imageloader is unavailable for UnloadDlcImage().");
    return false;
  }
  if (!success) {
    state_.set_last_error_code(kErrorInternal);
    *err = Error::Create(FROM_HERE, state_.last_error_code(),
                         "Imageloader UnloadDlcImage() call failed.");
    return false;
  }

  if (manifest_.mount_file_required()) {
    if (!Prefs(prefs_package_path_).Delete(kDlcRootMount))
      LOG(ERROR) << "Failed to delete indirect root mount file: "
                 << JoinPaths(prefs_package_path_, kDlcRootMount);
  }

  mount_point_.clear();
  // TODO(crbug.com/1069162): Currently, when we do unmount, we remove the DLC
  // too. So we should not change the state here. But once we switched to
  // ref-counting, and we only do unmount, then state could be changed here too.
  return true;
}

bool DlcBase::IsActiveImagePresent() const {
  return base::PathExists(GetImagePath(SystemState::Get()->active_boot_slot()));
}

// Deletes all directories related to this DLC.
bool DlcBase::DeleteInternal(ErrorPtr* err) {
  // If we're deleting the image, we need to set it as unverified.
  MarkUnverified();

  vector<string> undeleted_paths;
  for (const auto& path : GetPathsToDelete(id_)) {
    if (base::PathExists(path)) {
      if (!base::DeleteFile(path, true)) {
        PLOG(ERROR) << "Failed to delete path=" << path;
        undeleted_paths.push_back(path.value());
      } else {
        LOG(INFO) << "Deleted path=" << path;
      }
    }
  }

  if (!undeleted_paths.empty()) {
    state_.set_last_error_code(kErrorInternal);
    *err = Error::Create(
        FROM_HERE, state_.last_error_code(),
        base::StringPrintf("DLC directories (%s) could not be deleted.",
                           base::JoinString(undeleted_paths, ",").c_str()));
    return false;
  }
  return true;
}

bool DlcBase::Uninstall(ErrorPtr* err) {
  switch (state_.state()) {
    case DlcState::NOT_INSTALLED:
      // We still have to uninstall the DLC, in case we never mounted in this
      // session.
      LOG(WARNING) << "Trying to uninstall not installed DLC=" << id_;
      FALLTHROUGH;
    case DlcState::INSTALLED: {
      ref_count_->UninstalledDlc();
      ErrorPtr tmp_err;
      Unmount(&tmp_err);
      ChangeState(DlcState::NOT_INSTALLED);
      break;
    }
    case DlcState::INSTALLING:
      // We cannot uninstall the image while it is being installed by the
      // update_engine.
      state_.set_last_error_code(kErrorBusy);
      *err = Error::Create(
          FROM_HERE, state_.last_error_code(),
          base::StringPrintf("Trying to uninstall an installing DLC=%s",
                             id_.c_str()));
      return false;
    default:
      NOTREACHED();
      return false;
  }

  return true;
}

bool DlcBase::Purge(ErrorPtr* err) {
  // If the DLC is not verified, its not being updated, so there is no danger
  // purging it.
  auto ue_operation =
      SystemState::Get()->update_engine_status().current_operation();
  bool ue_is_busy = ue_operation != update_engine::IDLE &&
                    ue_operation != update_engine::UPDATED_NEED_REBOOT;
  if (IsVerified() && ue_is_busy) {
    *err = Error::Create(FROM_HERE, kErrorBusy,
                         "Install or update is in progress.");
    return false;
  }

  if (!Uninstall(err))
    return false;

  SetActiveValue(false);
  return DeleteInternal(err);
}

bool DlcBase::ShouldPurge() {
  // We can only automatically purge a DLC that is not installed.
  return state_.state() == DlcState::NOT_INSTALLED &&
         ref_count_->ShouldPurgeDlc();
}

void DlcBase::SetActiveValue(bool active) {
  ErrorPtr tmp_err;
  if (!SystemState::Get()->update_engine()->SetDlcActiveValue(active, id_,
                                                              &tmp_err))
    LOG(WARNING) << "Failed to set DLC=" << id_ << (active ? " " : " in")
                 << "active."
                 << (tmp_err ? Error::ToString(tmp_err)
                             : "Missing error from update engine proxy.");
}

void DlcBase::ChangeState(DlcState::State state) {
  switch (state) {
    case DlcState::NOT_INSTALLED:
      state_.set_state(state);
      state_.set_progress(0);
      state_.clear_root_path();
      break;

    case DlcState::INSTALLING:
      state_.set_state(state);
      state_.set_progress(0);
      state_.set_last_error_code(kErrorNone);
      break;

    case DlcState::INSTALLED:
      state_.set_state(state);
      state_.set_progress(1.0);
      state_.set_root_path(mount_point_.value());
      break;

    default:
      NOTREACHED();
  }

  LOG(INFO) << "Changing DLC=" << id_ << " state to " << state_.state();
  SystemState::Get()->state_change_reporter()->DlcStateChanged(state_);
}

void DlcBase::ChangeProgress(double progress) {
  if (state_.state() != DlcState::INSTALLING) {
    LOG(WARNING) << "Cannot change the progress if DLC is not being installed.";
    return;
  }

  // Make sure the progress is not decreased.
  if (state_.progress() < progress) {
    state_.set_progress(std::min(progress, 1.0));
    SystemState::Get()->state_change_reporter()->DlcStateChanged(state_);
  }
}

}  // namespace dlcservice
