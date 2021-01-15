// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_PORT_MANAGER_H_
#define TYPECD_PORT_MANAGER_H_

#include <map>
#include <memory>
#include <utility>

#include <gtest/gtest_prod.h>

#include "typecd/ec_util.h"
#include "typecd/port.h"
#include "typecd/session_manager_observer_interface.h"
#include "typecd/udev_monitor.h"

namespace typecd {

// This class is used to manage Type C ports and related state. Its role is to
// provide the daemon with an accurate view of the Type C state (after reading
// from the Type C connector class framework sysfs files), as well as provide a
// means to change this state according to policy defined in the daemon.
class PortManager : public UdevMonitor::Observer,
                    public SessionManagerObserverInterface {
 public:
  PortManager();
  PortManager(const PortManager&) = delete;
  PortManager& operator=(const PortManager&) = delete;

  void SetECUtil(ECUtil* ec_util) { ec_util_ = ec_util; }

  bool GetModeEntrySupported() { return mode_entry_supported_; }
  void SetModeEntrySupported(bool supported) {
    mode_entry_supported_ = supported;
  }

  bool GetUserActive() { return user_active_; }
  void SetUserActive(bool active) { user_active_ = active; }

 private:
  friend class PortManagerTest;
  FRIEND_TEST(PortManagerTest, ModeEntryNotSupported);
  FRIEND_TEST(PortManagerTest, SimpleModeEntry);

  // UdevMonitor::Observer overrides.
  void OnPortAddedOrRemoved(const base::FilePath& path,
                            int port_num,
                            bool added) override;
  void OnPartnerAddedOrRemoved(const base::FilePath& path,
                               int port_num,
                               bool added) override;
  void OnPartnerAltModeAddedOrRemoved(const base::FilePath& path,
                                      int port_num,
                                      bool added) override;
  void OnCableAddedOrRemoved(const base::FilePath& path,
                             int port_num,
                             bool added) override;
  void OnCablePlugAdded(const base::FilePath& path, int port_num) override;
  void OnCableAltModeAdded(const base::FilePath& path, int port_num) override;
  void OnPartnerChanged(int port_num) override;

  // SessionManagerObserverInterface overrides.
  void OnScreenIsLocked() override;
  void OnScreenIsUnlocked() override;
  void OnSessionStarted() override;
  void OnSessionStopped() override;

  // The central function which contains the main mode entry logic. This decides
  // which partner mode we select, based on partner/cable characteristics as
  // well as host properties and any other device specific policy we choose to
  // implement.
  void RunModeEntry(int port_num);

  std::map<int, std::unique_ptr<Port>> ports_;
  bool mode_entry_supported_;
  ECUtil* ec_util_;

  // Variable that is used to determine what alt mode should be entered. It is
  // updated in response to session manager events. It is set to false when the
  // screen is locked, and true when unlocked. In addition to that, it is also
  // set to true when a session starts i.e when a user logs in, and false when a
  // session ends i.e the user logs out.
  bool user_active_;
};

}  // namespace typecd

#endif  // TYPECD_PORT_MANAGER_H_
