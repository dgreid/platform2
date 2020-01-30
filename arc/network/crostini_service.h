// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_CROSTINI_SERVICE_H_
#define ARC_NETWORK_CROSTINI_SERVICE_H_

#include <map>
#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <permission_broker/dbus-proxies.h>

#include "arc/network/datapath.h"
#include "arc/network/device.h"
#include "arc/network/device_manager.h"

namespace arc_networkd {

// Crostini networking service handling address allocation and TAP device
// management for Crostini VMs.
class CrostiniService {
 public:
  // |dev_mgr| and |datapath| cannot be null.
  CrostiniService(DeviceManagerBase* dev_mgr, Datapath* datapath);
  ~CrostiniService() = default;

  bool Start(uint64_t vm_id, bool is_termina, int subnet_index);
  void Stop(uint64_t vm_id, bool is_termina);

  const Device* const TAP(uint64_t vm_id, bool is_termina) const;

 private:
  std::unique_ptr<Device> AddTAP(bool is_termina, int subnet_index);
  void OnDefaultInterfaceChanged(const std::string& ifname);

  bool SetupFirewallClient();

  // Setup lifeline pipe to allow the remote firewall server
  // (permission_broker) to monitor this process, so it can remove the firewall
  // rules in case this process crashes.
  int32_t SetupLifelinePipe();

  // Start and stop ADB traffic forwarding from Crostini's TAP device to
  // arc-networkd's adb-proxy. |ifname| is the Crostini's TAP interface that
  // will be forwarded. These methods call permission broker DBUS APIs to port
  // forward and accept traffic.
  void StartAdbPortForwarding(const std::string& ifname);
  void StopAdbPortForwarding(const std::string& ifname);

  DeviceManagerBase* dev_mgr_;
  Datapath* datapath_;
  // Mapping of VM IDs to TAP devices
  std::map<std::string, std::unique_ptr<Device>> taps_;

  scoped_refptr<dbus::Bus> bus_;
  std::unique_ptr<org::chromium::PermissionBrokerProxy>
      permission_broker_proxy_;

  // Mapping from Crostini's TAP interface to lifeline write file descriptor.
  // The file descriptor is the write end of the pipe used for communicating
  // with remote firewall server (permission_broker), where the remote firewall
  // server will use the read end of the pipe to detect when this process exits
  // or close the write end of the pipe.
  std::map<const std::string, base::ScopedFD> lifeline_fds_;

  base::WeakPtrFactory<CrostiniService> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(CrostiniService);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_CROSTINI_SERVICE_H_
