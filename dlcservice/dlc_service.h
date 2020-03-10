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
#include <imageloader/dbus-proxies.h>
#include <update_engine/proto_bindings/update_engine.pb.h>
#include <update_engine/dbus-proxies.h>

#include "dlcservice/dlc_manager.h"
#include "dlcservice/system_state.h"
#include "dlcservice/types.h"

namespace dlcservice {

// DlcService manages life-cycles of DLCs (Downloadable Content) and provides an
// API for the rest of the system to install/uninstall DLCs.
class DlcService {
 public:
  static const size_t kUECheckTimeout = 5;

  // |DlcService| calls the registered implementation of this class when a
  // |StatusResult| signal needs to be propagated.
  class Observer {
   public:
    virtual ~Observer() = default;

    virtual void SendInstallStatus(const InstallStatus& status) = 0;
  };

  DlcService();
  ~DlcService();

  // Loads installed DLC module images.
  void LoadDlcModuleImages();

  bool Install(const DlcModuleList& dlc_module_list_in, brillo::ErrorPtr* err);
  bool Uninstall(const std::string& id_in, brillo::ErrorPtr* err);
  bool GetInstalled(DlcModuleList* dlc_module_list_out, brillo::ErrorPtr* err);

  // Adds a new observer to report install result status changes.
  void AddObserver(Observer* observer);
  // Called on receiving update_engine's |StatusUpdate| signal.
  void OnStatusUpdateAdvancedSignal(
      const update_engine::StatusResult& status_result);

 private:
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
  void SendOnInstallStatusSignal(const InstallStatus& install_status);

  // Called on being connected to update_engine's |StatusUpdate| signal.
  void OnStatusUpdateAdvancedSignalConnected(const std::string& interface_name,
                                             const std::string& signal_name,
                                             bool success);

  org::chromium::UpdateEngineInterfaceProxyInterface* update_engine_proxy_;
  std::unique_ptr<DlcManager> dlc_manager_;

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
