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

#include "arc/network/address_manager.h"
#include "arc/network/datapath.h"
#include "arc/network/device.h"
#include "arc/network/shill_client.h"
#include "arc/network/traffic_forwarder.h"

namespace arc_networkd {

// Crostini networking service handling address allocation and TAP device
// management for Crostini VMs.
class CrostiniService {
 public:
  // All pointers are required and must not be null, and are owned by the
  // caller.
  CrostiniService(ShillClient* shill_client,
                  AddressManager* addr_mgr,
                  Datapath* datapath,
                  TrafficForwarder* forwarder);
  ~CrostiniService();

  bool Start(uint64_t vm_id, bool is_termina, int subnet_index);
  void Stop(uint64_t vm_id, bool is_termina);

  const Device* const TAP(uint64_t vm_id, bool is_termina) const;

 private:
  std::unique_ptr<Device> AddTAP(bool is_termina, int subnet_index);
  void OnDefaultInterfaceChanged(const std::string& new_ifname,
                                 const std::string& prev_ifname);
  void StartForwarding(const std::string& phys_ifname,
                       const std::string& virt_ifname);
  void StopForwarding(const std::string& phys_ifname,
                      const std::string& virt_ifname);

  bool SetupFirewallClient();

  // Checks ADB sideloading status and set it to |adb_sideloading_enabled_|.
  // This function will call itself again if ADB sideloading status is not
  // known yet. Otherwise, it will process all currently running Crostini VMs.
  void CheckAdbSideloadingStatus();

  // Start and stop ADB traffic forwarding from Crostini's TAP device
  // arc-networkd's adb-proxy. |ifname| is the Crostini's TAP interface that
  // will be forwarded. These methods call permission broker DBUS APIs to port
  // forward and accept traffic.
  void StartAdbPortForwarding(const std::string& ifname);
  void StopAdbPortForwarding(const std::string& ifname);

  ShillClient* shill_client_;
  AddressManager* addr_mgr_;
  Datapath* datapath_;
  TrafficForwarder* forwarder_;

  // Mapping of VM IDs to TAP devices
  std::map<std::string, std::unique_ptr<Device>> taps_;

  bool adb_sideloading_enabled_;
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
