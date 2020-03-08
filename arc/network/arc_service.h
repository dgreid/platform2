// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_ARC_SERVICE_H_
#define ARC_NETWORK_ARC_SERVICE_H_

#include <map>
#include <memory>
#include <set>
#include <string>

#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "arc/network/address_manager.h"
#include "arc/network/datapath.h"
#include "arc/network/device.h"
#include "arc/network/ipc.pb.h"
#include "arc/network/shill_client.h"
#include "arc/network/traffic_forwarder.h"

namespace arc_networkd {

class ArcService {
 public:
  class Context : public Device::Context {
   public:
    Context();
    ~Context() = default;

    // Tracks the lifetime of the ARC++ container.
    void Start();
    void Stop();
    bool IsStarted() const;

    // For ARCVM only.
    const std::string& TAP() const;
    void SetTAP(const std::string& tap);

   private:
    // Indicates the device was started.
    bool started_;
    // For ARCVM, the name of the bound TAP device.
    std::string tap_;
  };

  class Impl {
   public:
    virtual ~Impl() = default;

    virtual GuestMessage::GuestType guest() const = 0;
    virtual uint32_t id() const = 0;

    virtual bool Start(uint32_t id) = 0;
    virtual void Stop(uint32_t id) = 0;
    virtual bool IsStarted(uint32_t* id = nullptr) const = 0;
    virtual bool OnStartDevice(Device* device) = 0;
    virtual void OnStopDevice(Device* device) = 0;
    virtual void OnDefaultInterfaceChanged(const std::string& new_ifname,
                                           const std::string& prev_ifname) = 0;

    // Returns the ARC management interface.
    Device* ArcDevice() const { return arc_device_.get(); }

   protected:
    Impl() = default;

    // For now each implementation manages its own ARC device since ARCVM is
    // still single-networked.
    std::unique_ptr<Device> arc_device_;
  };

  // Encapsulates all ARC++ container-specific logic.
  class ContainerImpl : public Impl {
   public:
    ContainerImpl(Datapath* datapath,
                  AddressManager* addr_mgr,
                  TrafficForwarder* forwarder,
                  GuestMessage::GuestType guest);
    ~ContainerImpl() = default;

    GuestMessage::GuestType guest() const override;
    uint32_t id() const override;

    bool Start(uint32_t pid) override;
    void Stop(uint32_t pid) override;
    bool IsStarted(uint32_t* pid = nullptr) const override;
    bool OnStartDevice(Device* device) override;
    void OnStopDevice(Device* device) override;
    void OnDefaultInterfaceChanged(const std::string& new_ifname,
                                   const std::string& prev_ifname) override;

   private:
    uint32_t pid_;
    Datapath* datapath_;
    AddressManager* addr_mgr_;
    TrafficForwarder* forwarder_;
    GuestMessage::GuestType guest_;

    base::WeakPtrFactory<ContainerImpl> weak_factory_{this};
    DISALLOW_COPY_AND_ASSIGN(ContainerImpl);
  };

  // Encapsulates all ARC VM-specific logic.
  class VmImpl : public Impl {
   public:
    VmImpl(ShillClient* shill_client,
           Datapath* datapath,
           AddressManager* addr_mgr,
           TrafficForwarder* forwarder);
    ~VmImpl() = default;

    GuestMessage::GuestType guest() const override;
    uint32_t id() const override;

    bool Start(uint32_t cid) override;
    void Stop(uint32_t cid) override;
    bool IsStarted(uint32_t* cid = nullptr) const override;
    bool OnStartDevice(Device* device) override;
    void OnStopDevice(Device* device) override;
    void OnDefaultInterfaceChanged(const std::string& new_ifname,
                                   const std::string& prev_ifname) override;

   private:
    uint32_t cid_;
    const ShillClient* const shill_client_;
    Datapath* datapath_;
    AddressManager* addr_mgr_;
    TrafficForwarder* forwarder_;
    std::string tap_;

    DISALLOW_COPY_AND_ASSIGN(VmImpl);
  };

  // All pointers are required and cannot be null, and are owned by the caller.
  ArcService(ShillClient* shill_client,
             Datapath* datapath,
             AddressManager* addr_mgr,
             TrafficForwarder* forwarder);
  ~ArcService();

  bool Start(uint32_t id);
  void Stop(uint32_t id);

  // Returns the ARC management interface.
  Device* ArcDevice() const;

 private:
  // Callback from ShillClient, invoked whenever the device list changes.
  // |devices_| will contain all devices currently connected to shill
  // (e.g. "eth0", "wlan0", etc).
  void OnDevicesChanged(const std::set<std::string>& added,
                        const std::set<std::string>& removed);

  // Callback from ShillClient, invoked whenever the default network
  // interface changes or goes away.
  void OnDefaultInterfaceChanged(const std::string& new_ifname,
                                 const std::string& prev_ifname);

  // Build and configure an ARC device for the interface |name| provided by
  // Shill. The new device will be added to |devices_|. If an implementation is
  // already running, the device will be started.
  void AddDevice(const std::string& ifname);

  // Deletes the ARC device; if an implementation is running, the device will be
  // stopped first.
  void RemoveDevice(const std::string& ifname);

  // Starts a device by setting up the bridge and configuring some NAT rules,
  // then invoking the implementation-specific start routine.
  void StartDevice(Device* device);

  // Stops and cleans up any virtual interfaces and associated datapath.
  void StopDevice(Device* device);

  ShillClient* shill_client_;
  Datapath* datapath_;
  AddressManager* addr_mgr_;
  TrafficForwarder* forwarder_;
  std::unique_ptr<Impl> impl_;
  std::map<std::string, std::unique_ptr<Device>> devices_;

  FRIEND_TEST(ArcServiceTest, StartDevice);
  FRIEND_TEST(ArcServiceTest, StopDevice);

  base::WeakPtrFactory<ArcService> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(ArcService);
};

namespace test {
extern GuestMessage::GuestType guest;
}  // namespace test

}  // namespace arc_networkd

#endif  // ARC_NETWORK_ARC_SERVICE_H_
