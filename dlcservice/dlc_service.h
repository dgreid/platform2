// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_DLC_SERVICE_H_
#define DLCSERVICE_DLC_SERVICE_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/weak_ptr.h>
#include <brillo/message_loops/message_loop.h>
#include <dlcservice/proto_bindings/dlcservice.pb.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST
#include <imageloader/dbus-proxies.h>
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <update_engine/dbus-proxies.h>

#include "dlcservice/dlc.h"
#include "dlcservice/dlc_manager.h"
#include "dlcservice/system_state.h"

namespace dlcservice {

class DlcServiceInterface {
 public:
  virtual ~DlcServiceInterface() = default;

  // Initializes the state of dlcservice.
  virtual void Initialize() = 0;
  virtual bool Install(const DlcId& id,
                       const std::string& omaha_url,
                       brillo::ErrorPtr* err) = 0;
  virtual bool Uninstall(const std::string& id, brillo::ErrorPtr* err) = 0;
  virtual bool Purge(const std::string& id, brillo::ErrorPtr* err) = 0;
  virtual const DlcBase* GetDlc(const DlcId& id, brillo::ErrorPtr* err) = 0;
  virtual DlcIdList GetInstalled() = 0;
  virtual DlcIdList GetExistingDlcs() = 0;
  virtual DlcIdList GetDlcsToUpdate() = 0;
  virtual bool InstallCompleted(const DlcIdList& ids,
                                brillo::ErrorPtr* err) = 0;
  virtual bool UpdateCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) = 0;
};

// DlcService manages life-cycles of DLCs (Downloadable Content) and provides an
// API for the rest of the system to install/uninstall DLCs.
class DlcService : public DlcServiceInterface {
 public:
  static const size_t kUECheckTimeout = 5;

  DlcService();
  ~DlcService() override;

  void Initialize() override;
  bool Install(const DlcId& id,
               const std::string& omaha_url,
               brillo::ErrorPtr* err) override;
  bool Uninstall(const std::string& id, brillo::ErrorPtr* err) override;
  bool Purge(const std::string& id, brillo::ErrorPtr* err) override;
  DlcIdList GetInstalled() override;
  DlcIdList GetExistingDlcs() override;
  const DlcBase* GetDlc(const DlcId& id, brillo::ErrorPtr* err) override;
  DlcIdList GetDlcsToUpdate() override;
  bool InstallCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) override;
  bool UpdateCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) override;

 private:
  friend class DlcServiceTest;
  FRIEND_TEST(DlcServiceTest, InstallCannotSetDlcActiveValue);
  FRIEND_TEST(DlcServiceTest, OnStatusUpdateSignalTest);
  FRIEND_TEST(DlcServiceTest, MountFailureTest);
  FRIEND_TEST(DlcServiceTest, OnStatusUpdateSignalDlcRootTest);
  FRIEND_TEST(DlcServiceTest, OnStatusUpdateSignalNoRemountTest);
  FRIEND_TEST(DlcServiceTest, ReportingFailureCleanupTest);
  FRIEND_TEST(DlcServiceTest, ReportingFailureSignalTest);
  FRIEND_TEST(DlcServiceTest, ProbableUpdateEngineRestartCleanupTest);
  FRIEND_TEST(DlcServiceTest, UpdateEngineFailAfterSignalsSafeTest);
  FRIEND_TEST(DlcServiceTest, OnStatusUpdateSignalDownloadProgressTest);
  FRIEND_TEST(
      DlcServiceTest,
      OnStatusUpdateSignalSubsequentialBadOrNonInstalledDlcsNonBlocking);

  // Install the DLC with ID |id| through update_engine by sending a request to
  // it.
  bool InstallWithUpdateEngine(const DlcId& id,
                               const std::string& omaha_url,
                               brillo::ErrorPtr* err);

  // Sends a signal indicating failure to install and cleans up prepped DLC(s).
  void SendFailedSignalAndCleanup();

  // Handles status result from update_engine. Returns false if the installation
  // fails. Returns true if installation was successful or the installation is
  // not yet completed.
  bool HandleStatusResult(const update_engine::StatusResult& status_result,
                          brillo::ErrorPtr* err);

  // The periodic check that runs as a delayed task that checks update_engine
  // status during an install to make sure update_engine is active.
  void PeriodicInstallCheck();

  // Schedules the method |PeriodicInstallCheck()| to be ran at a later time,
  // taking as an argument a boolean |retry| that determines a once retry when
  // update_engine indicates an idle status while dlcservice expects an install.
  void SchedulePeriodicInstallCheck(bool retry);

  // Gets update_engine's operation status.
  bool GetUpdateEngineStatus(update_engine::Operation* operation);

  // Called on receiving update_engine's |StatusUpdate| signal.
  void OnStatusUpdateAdvancedSignal(
      const update_engine::StatusResult& status_result);

  // Called on being connected to update_engine's |StatusUpdate| signal.
  void OnStatusUpdateAdvancedSignalConnected(const std::string& interface_name,
                                             const std::string& signal_name,
                                             bool success);

  // Called when we are connected to the session_manager's |SessionStateChanged|
  // signal.
  void OnSessionStateChangedSignalConnected(const std::string& interface_name,
                                            const std::string& signal_name,
                                            bool success);

  // Called when the session state changes (user logs in or logs out).
  void OnSessionStateChangedSignal(const std::string& state);

  std::unique_ptr<DlcManager> dlc_manager_{};

  // Holds the ML task id of the delayed |PeriodicInstallCheck()| if an install
  // is in progress.
  brillo::MessageLoop::TaskId scheduled_period_ue_check_id_;

  // Indicates whether a retry to check update_engine's status during an install
  // needs to happen to make sure the install completion signal is not lost.
  bool scheduled_period_ue_check_retry_ = false;

  base::WeakPtrFactory<DlcService> weak_ptr_factory_;

  DlcService(const DlcService&) = delete;
  DlcService& operator=(const DlcService&) = delete;
};

}  // namespace dlcservice

#endif  // DLCSERVICE_DLC_SERVICE_H_
