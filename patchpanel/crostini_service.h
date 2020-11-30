// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_CROSTINI_SERVICE_H_
#define PATCHPANEL_CROSTINI_SERVICE_H_

#include <map>
#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>

#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/device.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/traffic_forwarder.h"

namespace patchpanel {

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
  CrostiniService(const CrostiniService&) = delete;
  CrostiniService& operator=(const CrostiniService&) = delete;

  ~CrostiniService();

  bool Start(uint64_t vm_id, bool is_termina, int subnet_index);
  void Stop(uint64_t vm_id, bool is_termina);

  const Device* const TAP(uint64_t vm_id, bool is_termina) const;

  // Walks the current list of devices managed by the service invoking the
  // callback for each, allowing for safe inspection/evaluation.
  // The first two callback args correspond to the vm_id and is_termina values
  // originally provided to the TAP() function that created the device.
  void ScanDevices(base::RepeatingCallback<void(uint64_t, bool, const Device&)>
                       callback) const;

 private:
  std::unique_ptr<Device> AddTAP(bool is_termina, int subnet_index);
  void OnDefaultDeviceChanged(const ShillClient::Device& new_device,
                              const ShillClient::Device& prev_device);
  void StartForwarding(const std::string& phys_ifname,
                       const std::string& virt_ifname);
  void StopForwarding(const std::string& phys_ifname,
                      const std::string& virt_ifname);

  // Checks ADB sideloading status and set it to |adb_sideloading_enabled_|.
  // This function will call itself again if ADB sideloading status is not
  // known yet. Otherwise, it will process all currently running Crostini VMs.
  void CheckAdbSideloadingStatus();

  // Start and stop ADB traffic forwarding from Crostini's TAP device
  // patchpanel's adb-proxy. |ifname| is the Crostini's TAP interface that
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

  base::WeakPtrFactory<CrostiniService> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_CROSTINI_SERVICE_H_
