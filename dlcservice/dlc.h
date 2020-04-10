// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_DLC_H_
#define DLCSERVICE_DLC_H_

#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <brillo/errors/error.h>
#include <dbus/dlcservice/dbus-constants.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <libimageloader/manifest.h>
#include <chromeos/dbus/service_constants.h>

#include "dlcservice/boot/boot_slot.h"

using base::FilePath;
using brillo::ErrorPtr;
using std::string;

namespace dlcservice {

// |DlcId| is the ID of the DLC.
using DlcId = std::string;

class DlcBase {
 public:
  explicit DlcBase(const DlcId& id) : id_(id) {}
  virtual ~DlcBase() = default;

  // Initializes the DLC. This should be called right after creating the DLC
  // object.
  bool Initialize();

  // Returns the ID of the DLC.
  DlcId GetId() const;

  // Returns the current state of the DLC.
  DlcState GetState() const;

  // Returns the root directory inside a mounted DLC module.
  base::FilePath GetRoot() const;

  // Returns true if the DLC is currently being installed.
  bool IsInstalling() const;

  // Returns true if the DLC is already installed and mounted.
  bool IsInstalled() const;

  // Returns true if the DLC module has a boolean true for 'preload-allowed'
  // attribute in the manifest for the given |id| and |package|.
  bool IsPreloadAllowed() const;

  // Loads the preloadable DLC from preloaded content directory.
  void PreloadImage();

  // Initializes the installation like creating the necessary files, etc.
  bool InitInstall(ErrorPtr* err);

  // This is called after the update_engine finishes the installation of a
  // DLC. This marks the DLC as installed and mounts the DLC image.
  bool FinishInstall(ErrorPtr* err);

  // Cancels the ongoing installation of this DLC. The state will be set to
  // uninstalled after this call if successful.
  bool CancelInstall(ErrorPtr* err);

  // Deletes all files associated with the DLC.
  bool Delete(ErrorPtr* err);

  // Persists the mountable pref for DLC.
  bool MarkMountable(const BootSlot::Slot& slot, ErrorPtr* err) const;

  // Removes the mountable pref for DLC.
  bool ClearMountable(const BootSlot::Slot& slot, ErrorPtr* err) const;

 private:
  friend class DBusServiceTest;
  FRIEND_TEST(DBusServiceTest, GetInstalled);

  // Returns the path to the DLC image given the slot number.
  FilePath GetImagePath(BootSlot::Slot slot) const;

  // Create the DLC directories and files if they don't exist.
  bool Create(ErrorPtr* err);

  // Validate that:
  //  - If inactive image for DLC is missing, try creating it.
  //  - If inactive image size is less than size in manifest, increase it.
  bool ValidateInactiveImage() const;

  // Helper used to load in (copy + cleanup) preloadable files for the DLC.
  bool PreloadedCopier() const;

  // Mounts the DLC image.
  bool Mount(ErrorPtr* err);

  // Unmounts the DLC image.
  bool Unmount(ErrorPtr* err);

  // Tries to mount the DLC image if it has not been mounted already.
  bool TryMount();

  // Returns true if the active DLC image is present.
  bool IsActiveImagePresent() const;

  // Deletes all directories related to this DLC.
  bool DeleteInternal(ErrorPtr* err);

  DlcId id_;
  std::string package_;

  DlcState state_;

  base::FilePath mount_point_;

  imageloader::Manifest manifest_;

  // The directories on the stateful partition where the DLC image will reside.
  base::FilePath content_id_path_;
  base::FilePath content_package_path_;
  base::FilePath prefs_path_;

  DISALLOW_COPY_AND_ASSIGN(DlcBase);
};

using DlcMap = std::map<DlcId, DlcBase>;
using DlcSet = std::set<DlcId>;
using DlcVec = std::vector<DlcId>;

}  // namespace dlcservice

#endif  // DLCSERVICE_DLC_H_
