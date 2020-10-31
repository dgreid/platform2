// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port_manager.h"

#include <base/logging.h>
#include <re2/re2.h>

namespace typecd {

void PortManager::OnPortAddedOrRemoved(const base::FilePath& path,
                                       int port_num,
                                       bool added) {
  auto it = ports_.find(port_num);
  if (added) {
    if (it != ports_.end()) {
      LOG(WARNING) << "Attempting to add an already added port.";
      return;
    }

    ports_.emplace(port_num, std::make_unique<Port>(path, port_num));
  } else {
    if (it == ports_.end()) {
      LOG(WARNING) << "Attempting to remove a non-existent port.";
      return;
    }

    ports_.erase(it);
  }
}

void PortManager::OnPartnerAddedOrRemoved(const base::FilePath& path,
                                          int port_num,
                                          bool added) {
  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING) << "Partner add/remove attempted for non-existent port "
                 << port_num;
    return;
  }

  auto port = it->second.get();
  if (added) {
    port->AddPartner(path);
  } else {
    port->RemovePartner();
  }
}

void PortManager::OnPartnerAltModeAddedOrRemoved(const base::FilePath& path,
                                                 int port_num,
                                                 bool added) {
  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING)
        << "Partner alt mode add/remove attempted for non-existent port "
        << port_num;
    return;
  }

  auto port = it->second.get();
  port->AddRemovePartnerAltMode(path, added);
}

void PortManager::OnCableAddedOrRemoved(const base::FilePath& path,
                                        int port_num,
                                        bool added) {
  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING) << "Cable add/remove attempted for non-existent port "
                 << port_num;
    return;
  }

  auto port = it->second.get();
  if (added) {
    port->AddCable(path);
  } else {
    port->RemoveCable();
  }
}

void PortManager::OnCableAltModeAdded(const base::FilePath& path,
                                      int port_num) {
  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING) << "Cable alt mode add attempted for non-existent port "
                 << port_num;
    return;
  }

  auto port = it->second.get();
  port->AddCableAltMode(path);
}

void PortManager::RunModeEntry(int port_num) {
  if (!ec_util_) {
    LOG(ERROR) << "No EC Util implementation registered, mode entry aborted.";
    return;
  }

  if (!ec_util_->ModeEntrySupported()) {
    LOG(INFO) << "Mode entry not supported on this device.";
    return;
  }

  auto it = ports_.find(port_num);
  if (it == ports_.end()) {
    LOG(WARNING) << "Mode entry attempted for non-existent port " << port_num;
    return;
  }

  auto port = it->second.get();

  if (port->GetDataRole() != "dfp") {
    LOG(WARNING) << "Can't switch modes because data role is not DFP on port "
                 << port_num;
    return;
  }

  // TODO(b/152251292): Check for Cable Discovery complete too.
  if (!port->IsPartnerDiscoveryComplete()) {
    LOG(WARNING)
        << "Can't switch modes Partner/Cable discovery not complete for port "
        << port_num;
    return;
  }

  // If the host supports USB4 and we can enter USB4 in this partner, do so.
  if (port->CanEnterUSB4()) {
    if (!ec_util_->EnterMode(port_num, TYPEC_MODE_USB4))
      LOG(ERROR) << "Attempt to call Enter USB4 failed for port " << port_num;
    return;
  }

  if (port->CanEnterTBTCompatibilityMode()) {
    if (!ec_util_->EnterMode(port_num, TYPEC_MODE_TBT))
      LOG(ERROR) << "Attempt to call Enter TBT failed for port " << port_num;
    return;
  }

  if (port->CanEnterDPAltMode()) {
    if (!ec_util_->EnterMode(port_num, TYPEC_MODE_DP))
      LOG(ERROR) << "Attempt to call Enter DP failed for port " << port_num;
    return;
  }
}

}  // namespace typecd
