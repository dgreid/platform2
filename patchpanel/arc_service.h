// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_ARC_SERVICE_H_
#define PATCHPANEL_ARC_SERVICE_H_

#include <deque>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "patchpanel/address_manager.h"
#include "patchpanel/datapath.h"
#include "patchpanel/device.h"
#include "patchpanel/ipc.pb.h"
#include "patchpanel/shill_client.h"
#include "patchpanel/traffic_forwarder.h"

namespace patchpanel {

class ArcService {
 public:
  enum class InterfaceType {
    UNKNOWN,
    ETHERNET,
    WIFI,
    CELL,
  };

  // All pointers are required and cannot be null, and are owned by the caller.
  ArcService(ShillClient* shill_client,
             Datapath* datapath,
             AddressManager* addr_mgr,
             TrafficForwarder* forwarder,
             GuestMessage::GuestType guest);
  ~ArcService();

  bool Start(uint32_t id);
  void Stop(uint32_t id);

  // Returns a list of device configurations. This method only really is useful
  // when ARCVM is running as it enables the caller to discover which
  // configurations, if any, are currently associated to TAP devices.
  std::vector<const Device::Config*> GetDeviceConfigs() const;

 private:
  // Returns true if the service has been started for ARC container or ARCVM.
  bool IsStarted() const;

  // Callback from ShillClient, invoked whenever the device list changes.
  // |devices_| will contain all devices currently connected to shill
  // (e.g. "eth0", "wlan0", etc).
  void OnDevicesChanged(const std::set<std::string>& added,
                        const std::set<std::string>& removed);

  // Build and configure an ARC device for the interface |name| provided by
  // Shill. The new device will be added to |devices_|. If ArcService is
  // already running, the device will be started.
  void AddDevice(const std::string& ifname);

  // Deletes the ARC device; if ArcService is running, the device will be
  // stopped first.
  void RemoveDevice(const std::string& ifname);

  // Starts a device by setting up the bridge and configuring some NAT rules,
  // then invoking the container or VM specific start routine.
  void StartDevice(Device* device);

  // Stops and cleans up any virtual interfaces and associated datapath.
  void StopDevice(Device* device);

  // Creates device configurations for all available IPv4 subnets which will be
  // assigned to devices as they are added.
  void AllocateAddressConfigs();

  // This function will temporarily remove existing devices, reallocate
  // address configurations and re-add existing devices. This is necessary to
  // properly handle the IPv4 addressing binding difference between ARC++ and
  // ARCVM.
  void ReallocateAddressConfigs();

  // Reserve a configuration for an interface.
  std::unique_ptr<Device::Config> AcquireConfig(const std::string& ifname);

  // Returns a configuration to the pool.
  void ReleaseConfig(const std::string& ifname,
                     std::unique_ptr<Device::Config> config);

  ShillClient* shill_client_;
  Datapath* datapath_;
  AddressManager* addr_mgr_;
  TrafficForwarder* forwarder_;
  GuestMessage::GuestType guest_;
  // A set of preallocated device configurations keyed by technology type and
  // used for setting up ARCVM tap devices at VM booting time.
  std::map<InterfaceType, std::deque<std::unique_ptr<Device::Config>>>
      available_configs_;
  // The list of all Device configurations. Also includes ARC management device
  // for ARCVM.
  std::vector<Device::Config*> all_configs_;
  // The ARC device configurations corresponding to the host physical devices,
  // keyed by device interface name.
  std::map<std::string, std::unique_ptr<Device>> devices_;
  // The ARC management device used for legacy adb-over-tcp support and VPN
  // forwarding.
  std::unique_ptr<Device> arc_device_;
  // The PID of the ARC container instance or the CID of ARCVM instance.
  uint32_t id_;

  FRIEND_TEST(ArcServiceTest, ContainerImpl_OnStartDevice);
  FRIEND_TEST(ArcServiceTest, ContainerImpl_FailsToConfigureInterface);
  FRIEND_TEST(ArcServiceTest, ContainerImpl_OnStopDevice);
  FRIEND_TEST(ArcServiceTest, ContainerImpl_Start);
  FRIEND_TEST(ArcServiceTest, ContainerImpl_FailsToCreateInterface);
  FRIEND_TEST(ArcServiceTest, ContainerImpl_Stop);
  FRIEND_TEST(ArcServiceTest, NotStarted_AddDevice);
  FRIEND_TEST(ArcServiceTest, NotStarted_AddRemoveDevice);
  FRIEND_TEST(ArcServiceTest, StableArcVmMacAddrs);
  FRIEND_TEST(ArcServiceTest, StopDevice);
  FRIEND_TEST(ArcServiceTest, VerifyAddrConfigs);
  FRIEND_TEST(ArcServiceTest, VerifyAddrOrder);
  FRIEND_TEST(ArcServiceTest, VmImpl_Start);
  FRIEND_TEST(ArcServiceTest, VmImpl_StartDevice);
  FRIEND_TEST(ArcServiceTest, VmImpl_StartMultipleDevices);
  FRIEND_TEST(ArcServiceTest, VmImpl_Stop);
  FRIEND_TEST(ArcServiceTest, VmImpl_StopDevice);

  base::WeakPtrFactory<ArcService> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(ArcService);
};

}  // namespace patchpanel

#endif  // PATCHPANEL_ARC_SERVICE_H_
