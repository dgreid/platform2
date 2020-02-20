// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_ARC_SERVICE_H_
#define ARC_NETWORK_ARC_SERVICE_H_

#include <map>
#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <shill/net/rtnl_handler.h>
#include <shill/net/rtnl_listener.h>

#include "arc/network/datapath.h"
#include "arc/network/device.h"
#include "arc/network/device_manager.h"
#include "arc/network/ipc.pb.h"
#include "arc/network/shill_client.h"

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

    bool IsLinkUp() const override;
    // Returns true if the internal state changed.
    bool SetLinkUp(bool link_up);

    // For ARCVM only.
    const std::string& TAP() const;
    void SetTAP(const std::string& tap);

   private:
    // Indicates the device was started.
    bool started_;
    // Indicates Android has brought up the interface.
    bool link_up_;
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
    ContainerImpl(DeviceManagerBase* dev_mgr,
                  Datapath* datapath,
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
    // Handles RT netlink messages in the container net namespace and if it
    // determines the link status has changed, toggles the device services
    // accordingly.
    void LinkMsgHandler(const shill::RTNLMessage& msg);

    uint32_t pid_;
    DeviceManagerBase* dev_mgr_;
    Datapath* datapath_;
    GuestMessage::GuestType guest_;

    // These are installed in the ARC net namespace.
    std::unique_ptr<shill::RTNLHandler> rtnl_handler_;
    std::unique_ptr<shill::RTNLListener> link_listener_;

    base::WeakPtrFactory<ContainerImpl> weak_factory_{this};
    DISALLOW_COPY_AND_ASSIGN(ContainerImpl);
  };

  // Encapsulates all ARC VM-specific logic.
  class VmImpl : public Impl {
   public:
    VmImpl(DeviceManagerBase* dev_mgr, Datapath* datapath);
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
    DeviceManagerBase* dev_mgr_;
    Datapath* datapath_;
    std::string tap_;

    DISALLOW_COPY_AND_ASSIGN(VmImpl);
  };

  // All pointers are required and cannot be null, and are owned by the caller.
  ArcService(ShillClient* shill_client,
             DeviceManagerBase* dev_mgr,
             Datapath* datapath);
  ~ArcService();

  bool Start(uint32_t id);
  void Stop(uint32_t id);

  void OnDeviceAdded(Device* device);
  void OnDeviceRemoved(Device* device);
  void OnDefaultInterfaceChanged(const std::string& new_ifname,
                                 const std::string& prev_ifname);

  Device* ArcDevice() const;

 private:
  void StartDevice(Device* device);
  void StopDevice(Device* device);

  // Returns true if the device should be processed by the service.
  bool AllowDevice(Device* device) const;

  ShillClient* shill_client_;
  DeviceManagerBase* dev_mgr_;
  Datapath* datapath_;
  std::unique_ptr<Impl> impl_;

  base::WeakPtrFactory<ArcService> weak_factory_{this};
  DISALLOW_COPY_AND_ASSIGN(ArcService);
};

namespace test {
extern GuestMessage::GuestType guest;
}  // namespace test

}  // namespace arc_networkd

#endif  // ARC_NETWORK_ARC_SERVICE_H_
