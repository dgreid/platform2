// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_DLC_H_
#define DLCSERVICE_DLC_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/errors/error.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <libimageloader/manifest.h>
#include <chromeos/dbus/service_constants.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/ref_count.h"

namespace dlcservice {

// |DlcId| is the ID of the DLC.
using DlcId = std::string;

class DlcBase {
 public:
  explicit DlcBase(DlcId id) : id_(std::move(id)) {}
  virtual ~DlcBase() = default;

  // Returns the list of directories related to a DLC for deletion.
  static std::vector<base::FilePath> GetPathsToDelete(const DlcId& id);

  // Initializes the DLC. This should be called right after creating the DLC
  // object.
  bool Initialize();

  // Returns the ID of the DLC.
  const DlcId& GetId() const;

  // Returns the human readable name of the DLC.
  const std::string& GetName() const;

  // Returns the description of the DLC.
  const std::string& GetDescription() const;

  // Returns the current state of the DLC.
  DlcState GetState() const;

  // Returns the root directory inside a mounted DLC module.
  base::FilePath GetRoot() const;

  // Returns true if the DLC is currently being installed.
  bool IsInstalling() const;

  // Returns true if the DLC is already installed and mounted.
  bool IsInstalled() const;

  // Returns true if the DLC is marked verified.
  bool IsVerified() const;

  // Returns true if the DLC has any content on disk that is taking space. This
  // means mainly if it has images on disk.
  bool HasContent() const;

  // Returns the amount of disk space this DLC is using right now.
  uint64_t GetUsedBytesOnDisk() const;

  // Returns true if the DLC has a boolean true for 'preload-allowed'
  // attribute in the manifest for the given |id| and |package|.
  bool IsPreloadAllowed() const;

  // Creates the DLC image based on the fields from the manifest if the DLC is
  // not installed. If the DLC image exists or is installed already, some
  // verifications are passed to validate that the DLC is mounted.
  // Initializes the installation like creating the necessary files, etc.
  bool Install(brillo::ErrorPtr* err);

  // This is called after the update_engine finishes the installation of a
  // DLC. This marks the DLC as installed and mounts the DLC image.
  bool FinishInstall(bool installed_by_ue, brillo::ErrorPtr* err);

  // Cancels the ongoing installation of this DLC. The state will be set to
  // uninstalled after this call if successful.
  // The |err_in| argument is the error that causes the install to be cancelled.
  bool CancelInstall(const brillo::ErrorPtr& err_in, brillo::ErrorPtr* err);

  // Uninstalls the DLC.
  bool Uninstall(brillo::ErrorPtr* err);

  // Deletes all files associated with the DLC.
  bool Purge(brillo::ErrorPtr* err);

  // Returns true if the DLC has to be removed/purged.
  bool ShouldPurge();

  // Is called when the DLC image is finally installed on the disk and is
  // verified.
  bool InstallCompleted(brillo::ErrorPtr* err);

  // Is called when the inactive DLC image is updated and verified.
  bool UpdateCompleted(brillo::ErrorPtr* err);

  // Makes the DLC ready to be updated (creates and resizes the inactive
  // image). Returns false if anything goes wrong.
  bool MakeReadyForUpdate() const;

  // Changes the install progress on this DLC. Only changes if the |progress| is
  // greater than the current progress value.
  void ChangeProgress(double progress);

 private:
  friend class DBusServiceTest;
  FRIEND_TEST(DBusServiceTest, GetInstalled);
  FRIEND_TEST(DlcBaseTest, GetUsedBytesOnDisk);
  FRIEND_TEST(DlcBaseTest, DefaultState);
  FRIEND_TEST(DlcBaseTest, ChangeStateNotInstalled);
  FRIEND_TEST(DlcBaseTest, ChangeStateInstalling);
  FRIEND_TEST(DlcBaseTest, ChangeStateInstalled);
  FRIEND_TEST(DlcBaseTest, ChangeProgress);
  FRIEND_TEST(DlcBaseTest, MakeReadyForUpdate);
  FRIEND_TEST(DlcBaseTest, MarkUnverified);
  FRIEND_TEST(DlcBaseTest, MarkVerified);
  FRIEND_TEST(DlcBaseTest, PreloadCopyShouldMarkUnverified);
  FRIEND_TEST(DlcBaseTest, PreloadCopyFailOnInvalidFileSize);
  FRIEND_TEST(DlcBaseTest, InstallingCorruptPreloadedImageCleansUp);
  FRIEND_TEST(DlcBaseTest, PreloadingSkippedOnAlreadyVerifiedDlc);
  FRIEND_TEST(DlcBaseTest, UnmountClearsMountPoint);

  // Returns the path to the DLC image given the slot number.
  base::FilePath GetImagePath(BootSlot::Slot slot) const;

  // Creates the DLC directories and files if they don't exist. This function
  // should be used as fall-through. We should call this even if we presumably
  // know the files are already there. This allows us to create any new DLC
  // files that didn't exist on a previous version of the DLC.
  bool CreateDlc(brillo::ErrorPtr* err);

  // Mark the current active DLC image as verified.
  bool MarkVerified();

  // Mark the current active DLC image as unverified.
  bool MarkUnverified();

  // Returns true if the DLC image in the current active slot matches the hash
  // of that in the rootfs manifest for the DLC.
  bool Verify();

  // Helper used to load in (copy + cleanup) preloadable files for the DLC.
  bool PreloadedCopier(brillo::ErrorPtr* err);

  // Mounts the DLC image.
  bool Mount(brillo::ErrorPtr* err);

  // Unmounts the DLC image.
  bool Unmount(brillo::ErrorPtr* err);

  // Returns true if the active DLC image is present.
  bool IsActiveImagePresent() const;

  // Deletes all directories related to this DLC.
  bool DeleteInternal(brillo::ErrorPtr* err);

  // Changes the state of the current DLC. It also notifies the state change
  // reporter that a state change has been made.
  void ChangeState(DlcState::State state);

  // Sets the DLC as being active or not based on |active| value.
  void SetActiveValue(bool active);

  DlcId id_;
  std::string package_;

  DlcState state_;

  base::FilePath mount_point_;

  imageloader::Manifest manifest_;

  // The directories on the stateful partition where the DLC image will reside.
  base::FilePath content_id_path_;
  base::FilePath content_package_path_;
  base::FilePath prefs_path_;
  base::FilePath prefs_package_path_;
  base::FilePath preloaded_image_path_;

  // The object that keeps track of ref counts. NOTE: Do NOT access this object
  // directly. Use |GetRefCount()| instead.
  std::unique_ptr<RefCountInterface> ref_count_;

  DlcBase(const DlcBase&) = delete;
  DlcBase& operator=(const DlcBase&) = delete;
};

using DlcMap = std::map<DlcId, DlcBase>;
using DlcIdList = std::vector<DlcId>;

}  // namespace dlcservice

#endif  // DLCSERVICE_DLC_H_
