// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_DLC_MANAGER_H_
#define DLCSERVICE_DLC_MANAGER_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <imageloader/dbus-proxies.h>

#include "dlcservice/boot/boot_slot.h"
#include "dlcservice/types.h"

namespace dlcservice {

// Value to be stored in |kDlcMetadataFilePingActive| to indicate an active DLC.
extern const char kDlcMetadataActiveValue[];
// |kDlcMetadataFilePingActive| is the name of the file used to store the value
// of the 'active' metadata. This value indicates if the DLC was active since
// the last time this value was sent to Omaha. This file name should not be
// modified since it is also used in update_engine/common/constants.h as
// |kPrefsPingActive|.
extern const char kDlcMetadataFilePingActive[];

class DlcManager {
 public:
  DlcManager();
  ~DlcManager();

  // Returns true when an install is currently running.
  // If the desire is to |Initnstall()| again, then |FinishInstall()| or
  // |CancelInstall()| should be called before |InitInstall()|'ing again.
  bool IsInstalling();

  // Returns the list of fully installed + mounted DLC(s).
  DlcModuleList GetInstalled();

  // Returns true and sets |state| if the DLC is supported.
  bool GetState(const DlcId& id,
                DlcState* state,
                std::string* err_code,
                std::string* err_msg);

  // Loads installed DLC module images.
  void LoadDlcModuleImages();

  // DLC Installation Flow

  // Install Step 1:
  // To start an install, the initial requirement is to call this function.
  // During this phase, all necessary setup for update_engine to successfully
  // install DLC(s) and other files that require creation are handled.
  // Args:
  //   dlc_module_list: All the DLC(s) that want to be installed.
  //   err_code: The error code that should be checked when returned false.
  //   err_msg: The error message that should be checked when returned false.
  // Return:
  //   True on success, otherwise false.
  bool InitInstall(const DlcModuleList& dlc_module_list,
                   std::string* err_code,
                   std::string* err_msg);

  // Install Step 2:
  // To get the actual list of DLC(s) to pass into update_engine.
  // If the returned list of DLC(s) are empty there are no missing DLC(s) to
  // inform update_engine to install and can move onto the next step.
  // Args:
  //   none
  // Return:
  //   Will return all the DLC(s) that update_engine needs to download/install.
  DlcModuleList GetMissingInstalls();

  // Install Step 3a:
  // Once the missing DLC(s) are installed or there were no missing DLC(s), this
  // call is still required to finish the installation.
  // If there were missing DLC(s) that were newly installed, this call will go
  // ahead and mount those DLC(s) to be ready for use.
  // Args:
  //   dlc_module_list: Will contain all the DLC(s) and their root mount points
  //                    when returned true, otherwise unmodified.
  //   err_code: The error code that should be checked when returned false.
  //   err_msg: The error message that should be checked when returned false.
  // Return:
  //   True on success, otherwise false.
  bool FinishInstall(DlcModuleList* dlc_module_list,
                     std::string* err_code,
                     std::string* err_msg);

  // Install Step 3b:
  // If for any reason, the init'ed DLC(s) should not follow through with
  // mounting it can be cancelled by invoking this. The call may fail, in
  // which case the errors will reflect the causes and provide insight in ways
  // dlcservice can be put into a valid state again.
  // Args:
  //   err_code: The error code that should be checked when returned false.
  //   err_msg: The error message that should be checked when returned false.
  // Return:
  //   True on success, otherwise false.
  bool CancelInstall(std::string* err_code, std::string* err_msg);

  // DLC Deletion Flow

  // Delete Step 1:
  // To delete the DLC this can be invoked, no prior step is required.
  // Args:
  //   id: The DLC ID that is to be uninstalled.
  //   err_code: The error code that should be checked when returned false.
  //   err_msg: The error message that should be checked when returned false.
  // Return:
  //   True if the DLC with the ID passed in is successfully uninstalled,
  //   otherwise false. Deleting a valid DLC that's not installed is considered
  //   successfully uninstalled, however uninstalling a DLC that's not supported
  //   is a failure.
  bool Delete(const std::string& id,
              std::string* err_code,
              std::string* err_msg);

 private:
  class DlcManagerImpl;
  std::unique_ptr<DlcManagerImpl> impl_;

  DISALLOW_COPY_AND_ASSIGN(DlcManager);
};

}  // namespace dlcservice

#endif  // DLCSERVICE_DLC_MANAGER_H_
