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
  // |DlcService| calls the registered implementation of this class when a
  // |StatusResult| signal needs to be propagated.
  class Observer {
   public:
    virtual ~Observer() = default;

    virtual void SendInstallStatus(const InstallStatus& status) = 0;
  };

  virtual ~DlcServiceInterface() = default;

  // Initializes the state of dlcservice.
  virtual void Initialize() = 0;
  virtual bool Install(const DlcId& id,
                       const std::string& omaha_url,
                       brillo::ErrorPtr* err) = 0;
  virtual bool Uninstall(const std::string& id, brillo::ErrorPtr* err) = 0;
  virtual bool Purge(const std::string& id, brillo::ErrorPtr* err) = 0;
  virtual const DlcBase* GetDlc(const DlcId& id) = 0;
  virtual DlcIdList GetInstalled() = 0;
  virtual DlcIdList GetExistingDlcs() = 0;
  virtual DlcIdList GetDlcsToUpdate() = 0;
  virtual bool InstallCompleted(const DlcIdList& ids,
                                brillo::ErrorPtr* err) = 0;
  virtual bool UpdateCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) = 0;

  // Adds a new observer to report install result status changes.
  virtual void AddObserver(Observer* observer) = 0;
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
  const DlcBase* GetDlc(const DlcId& id) override;
  DlcIdList GetDlcsToUpdate() override;
  bool InstallCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) override;
  bool UpdateCompleted(const DlcIdList& ids, brillo::ErrorPtr* err) override;
  void AddObserver(Observer* observer) override;

 private:
  friend class DlcServiceTest;
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
  // Sends a signal indicating failure to install and cleans up prepped DLC(s).
  void SendFailedSignalAndCleanup();

  // Handles necessary actions prior to update_engine's install completion, but
  // when update_engine's install is complete it will return true.
  bool HandleStatusResult(const update_engine::StatusResult& status_result);

  // The periodic check that runs as a delayed task that checks update_engine
  // status during an install to make sure update_engine is active.
  void PeriodicInstallCheck();

  // Schedules the method |PeriodicInstallCheck()| to be ran at a later time,
  // taking as an argument a boolean |retry| that determines a once retry when
  // update_engine indicates an idle status while dlcservice expects an install.
  void SchedulePeriodicInstallCheck(bool retry);

  // Gets update_engine's operation status.
  bool GetUpdateEngineStatus(update_engine::Operation* operation);

  // Send |OnInstallStatus| D-Bus signal.
  void SendOnInstallStatusSignal(const dlcservice::Status& status,
                                 const std::string& error_code,
                                 const DlcIdList& ids,
                                 double progress);

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

  // The list of observers that will be called when a new status is ready.
  std::vector<Observer*> observers_;

  base::WeakPtrFactory<DlcService> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DlcService);
};

}  // namespace dlcservice

#endif  // DLCSERVICE_DLC_SERVICE_H_
