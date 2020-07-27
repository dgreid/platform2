// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_DLC_MANAGER_H_
#define DLCSERVICE_DLC_MANAGER_H_

#include <string>

#include <base/time/time.h>
#include <brillo/errors/error.h>
#include <brillo/message_loops/message_loop.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "dlcservice/dlc.h"

namespace dlcservice {

class DlcManager {
 public:
  DlcManager() = default;
  virtual ~DlcManager();

  // Returns a reference to a DLC object given a DLC ID. If the ID is not
  // supported, it will set the error and return |nullptr|.
  DlcBase* GetDlc(const DlcId& id, brillo::ErrorPtr* err);

  // Initializes the state of DlcManager.
  void Initialize();

  // Returns the list of installed DLCs.
  DlcIdList GetInstalled();

  // Returns the list of DLCs with installed content.
  DlcIdList GetExistingDlcs();

  // Returns the list of DLCs that need to be updated.
  DlcIdList GetDlcsToUpdate();

  // Returns the list of all supported DLC(s).
  DlcIdList GetSupported();

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
  //   id: The DLC ID that needs to be installed.
  //   external_install_needed: It is set to true if we need to actually install
  //     the DLC through update_engine.
  //   err: The error that's set when returned false.
  // Return:
  //   True on success, otherwise false.
  bool Install(const DlcId& id,
               bool* external_install_needed,
               brillo::ErrorPtr* err);

  // Install Step 2a:
  // Once the missing DLC(s) are installed or there were no missing DLC(s), this
  // call is still required to finish the installation.
  // If there were missing DLC(s) that were newly installed, this call will go
  // ahead and mount those DLC(s) to be ready for use.
  // Args:
  //   id: The DLC to finish the installation for.
  //   err: The error that's set when returned false.
  // Return:
  //   True on success, otherwise false.
  bool FinishInstall(const DlcId& id, brillo::ErrorPtr* err);

  // Install Step 2b:
  // If for any reason, the init'ed DLC(s) should not follow through with
  // mounting it can be cancelled by invoking this. The call may fail, in
  // which case the errors will reflect the causes and provide insight in ways
  // dlcservice can be put into a valid state again.
  // Args:
  //   id: The DLC to cancel the installation for.
  //   err_in: The error that caused the install to be cancelled.
  //   err: The error that's set when returned false.
  // Return:
  //   True on success, otherwise false.
  bool CancelInstall(const DlcId& id,
                     const brillo::ErrorPtr& err_in,
                     brillo::ErrorPtr* err);

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
  bool Uninstall(const DlcId& id, brillo::ErrorPtr* err);
  bool Purge(const DlcId& id, brillo::ErrorPtr* err);

  // Changes the progress on all DLCs being installed to |progress|.
  void ChangeProgress(double progress);

 private:
  FRIEND_TEST(DlcManagerTest, CleanupDanglingDlcs);

  // Removes all unsupported/deprecated DLC files and images.
  void CleanupUnsupportedDlcs();

  // Cleans up all DLCs that are dangling based on the ref count.
  void CleanupDanglingDlcs();

  // Posts the |CleanuupDanglingDlcs| as a delayed task with timeout |timeout|.
  void PostCleanupDanglingDlcs(const base::TimeDelta& timeout);

  DlcMap supported_;

  brillo::MessageLoop::TaskId cleanup_dangling_task_id_;

  DlcManager(const DlcManager&) = delete;
  DlcManager& operator=(const DlcManager&) = delete;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_DLC_MANAGER_H_
