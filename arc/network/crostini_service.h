// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_CROSTINI_SERVICE_H_
#define ARC_NETWORK_CROSTINI_SERVICE_H_

#include <map>
#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>

#include "arc/network/address_manager.h"
#include "arc/network/datapath.h"
#include "arc/network/device.h"
#include "arc/network/device_manager.h"
#include "arc/network/shill_client.h"

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
  ~CrostiniService() = default;

  bool Start(uint64_t vm_id, bool is_termina, int subnet_index);
  void Stop(uint64_t vm_id, bool is_termina);

  const Device* const TAP(uint64_t vm_id, bool is_termina) const;

 private:
  std::unique_ptr<Device> AddTAP(bool is_termina, int subnet_index);
  void OnDefaultInterfaceChanged(const std::string& new_ifname,
                                 const std::string& prev_ifname);
  void StartForwarding(const std::string& phys_ifname,
                       const std::string& virt_ifname,
                       uint32_t ipv4_addr);
  void StopForwarding(const std::string& phys_ifname,
                      const std::string& virt_ifname);

  ShillClient* shill_client_;
  AddressManager* addr_mgr_;
  Datapath* datapath_;
  TrafficForwarder* forwarder_;

  // Mapping of VM IDs to TAP devices
  std::map<std::string, std::unique_ptr<Device>> taps_;

  base::WeakPtrFactory<CrostiniService> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(CrostiniService);
};

}  // namespace arc_networkd

#endif  // ARC_NETWORK_CROSTINI_SERVICE_H_
