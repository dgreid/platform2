// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_DLC_MANAGER_H_
#define DLCSERVICE_DLC_MANAGER_H_

#include <string>

#include <base/macros.h>
#include <brillo/errors/error.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>

#include "dlcservice/dlc.h"

namespace dlcservice {

class DlcManager {
 public:
  DlcManager() = default;
  virtual ~DlcManager() = default;

  // Returns a reference to a DLC object given a DLC ID. We assume the ID is
  // valid.
  const DlcBase* GetDlc(const DlcId& id);

  // Initializes the state of DlcManager.
  void Initialize();

  // Returns true when an install is currently running.
  // If the desire is to |Initnstall()| again, then |FinishInstall()| or
  // |CancelInstall()| should be called before |InitInstall()|'ing again.
  bool IsInstalling();

  // Returns the list of installed DLCs.
  DlcIdList GetInstalled();

  // Returns the list of DLCs with installed content.
  DlcIdList GetExistingDlcs();

  // Returns the list of DLCs that need to be updated.
  DlcIdList GetDlcsToUpdate();

  // Returns the list of all supported DLC(s).
  DlcIdList GetSupported();

  // Returns true and sets |state| if the DLC is supported.
  bool GetDlcState(const DlcId& id, DlcState* state, brillo::ErrorPtr* err);

  // Persists the verified pref for given DLC(s) on install completion.
  bool InstallCompleted(const DlcIdList& ids, brillo::ErrorPtr* err);

  // Persists the verified pref for given DLC(s) on update completion.
  bool UpdateCompleted(const DlcIdList& ids, brillo::ErrorPtr* err);

  // DLC Installation Flow

  // Install Step 1:
  // To start an install, the initial requirement is to call this function.
  // During this phase, all necessary setup for update_engine to successfully
  // install DLC(s) and other files that require creation are handled.
  // Args:
  //   dlcs: All the DLC(s) that want to be installed.
  //   err: The error that's set when returned false.
  // Return:
  //   True on success, otherwise false.
  bool InitInstall(const DlcIdList& dlcs, brillo::ErrorPtr* err);

  // Install Step 2:
  // To get the actual list of DLC(s) to pass into update_engine.
  // If the returned list of DLC(s) are empty there are no missing DLC(s) to
  // inform update_engine to install and can move onto the next step.
  // Args:
  //   none
  // Return:
  //   Will return all the DLC(s) that update_engine needs to download/install.
  DlcIdList GetMissingInstalls();

  // Install Step 3a:
  // Once the missing DLC(s) are installed or there were no missing DLC(s), this
  // call is still required to finish the installation.
  // If there were missing DLC(s) that were newly installed, this call will go
  // ahead and mount those DLC(s) to be ready for use.
  // Args:
  //   dlc_module_list: Will contain all the DLC(s) and their root mount points
  //                    when returned true, otherwise unmodified.
  //   err: The error that's set when returned false.
  // Return:
  //   True on success, otherwise false.
  bool FinishInstall(brillo::ErrorPtr* err);

  // Install Step 3b:
  // If for any reason, the init'ed DLC(s) should not follow through with
  // mounting it can be cancelled by invoking this. The call may fail, in
  // which case the errors will reflect the causes and provide insight in ways
  // dlcservice can be put into a valid state again.
  // Args:
  //   err: The error that's set when returned false.
  // Return:
  //   True on success, otherwise false.
  bool CancelInstall(brillo::ErrorPtr* err);

  // DLC Deletion Flow

  // Delete Step 1:
  // To delete the DLC this can be invoked, no prior step is required.
  // Args:
  //   id: The DLC ID that is to be uninstalled.
  //   err: The error that's set when returned false.
  // Return:
  //   True if the DLC with the ID passed in is successfully uninstalled,
  //   otherwise false. Deleting a valid DLC that's not installed is considered
  //   successfully uninstalled, however uninstalling a DLC that's not supported
  //   is a failure. Uninstalling a DLC that is installing is also a failure.
  bool Delete(const DlcId& id, brillo::ErrorPtr* err);

 private:
  // Preloads preloadable DLCs from preloaded content directory.
  void PreloadDlcs();

  bool IsSupported(const DlcId& id);

  DlcMap supported_;

  DISALLOW_COPY_AND_ASSIGN(DlcManager);
};

}  // namespace dlcservice

#endif  // DLCSERVICE_DLC_MANAGER_H_
