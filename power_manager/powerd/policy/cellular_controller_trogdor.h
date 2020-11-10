// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef POWER_MANAGER_POWERD_POLICY_CELLULAR_CONTROLLER_TROGDOR_H_
#define POWER_MANAGER_POWERD_POLICY_CELLULAR_CONTROLLER_TROGDOR_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>

#include "power_manager/common/power_constants.h"
#include "power_manager/powerd/policy/user_proximity_handler.h"
#include "power_manager/powerd/system/udev.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"

namespace power_manager {

class PrefsInterface;

namespace policy {

// CellularController initiates power-related changes to the cellular chipset.
class CellularControllerTrogdor : public UserProximityHandler::Delegate {
 public:
  enum class Type {
    kQrtr,
    kMbim,
  };
  struct PacketMetadata {
    uint32_t port;
    uint32_t node;
  };
  // Performs work on behalf of CellularControllerTrogdor.
  class Delegate {
   public:
    virtual ~Delegate() = default;

    // Updates the transmit power to |power| via the dynamic power reduction
    // signal controlled by QMI CLI command.
    virtual void SetCellularTransmitPower(RadioTransmitPower power) = 0;
  };

  CellularControllerTrogdor();
  CellularControllerTrogdor(const CellularControllerTrogdor&) = delete;
  CellularControllerTrogdor& operator=(const CellularControllerTrogdor&) =
      delete;

  ~CellularControllerTrogdor() override;

  // Ownership of raw pointers remains with the caller.
  void Init(Delegate* delegate, PrefsInterface* prefs);

  // Called when the tablet mode changes.
  void HandleTabletModeChange(TabletMode mode);
  void HandleModemStateChange(ModemState state);
  // UserProximityHandler::Delegate overrides:
  void ProximitySensorDetected(UserProximity proximity) override;
  void HandleProximityChange(UserProximity proximity) override;

 private:
  // Updates transmit power via |delegate_|.
  void UpdateTransmitPower();

  RadioTransmitPower DetermineTransmitPower() const;

  // Functions to read modem status from QRTR
  bool InitQrtrSocket();
  void OnFileCanReadWithoutBlocking();
  void OnDataAvailable(CellularControllerTrogdor* cc);
  void ProcessQrtrPacket(uint32_t node, uint32_t port, int size);
  int Recv(void* buf, size_t size, void* metadata);
  int Send(const void* data, size_t size, const void* metadata);
  bool StartServiceLookup(uint32_t service,
                          uint16_t version_major,
                          uint16_t version_minor);
  bool StopServiceLookup(uint32_t service,
                         uint16_t version_major,
                         uint16_t version_minor);

  Delegate* delegate_ = nullptr;  // Not owned.

  TabletMode tablet_mode_ = TabletMode::UNSUPPORTED;
  UserProximity proximity_ = UserProximity::UNKNOWN;
  ModemState state_ = ModemState::UNKNOWN;

  // True if powerd has been configured to set cellular transmit power in
  // response to tablet mode or proximity changes.
  bool set_transmit_power_for_tablet_mode_ = false;
  bool set_transmit_power_for_proximity_ = false;

  base::ScopedFD socket_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher_;
  std::vector<uint8_t> buffer_;
};

}  // namespace policy
}  // namespace power_manager

#endif  // POWER_MANAGER_POWERD_POLICY_CELLULAR_CONTROLLER_TROGDOR_H_
