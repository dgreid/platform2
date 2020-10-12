// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_VPN_SERVICE_H_
#define SHILL_VPN_VPN_SERVICE_H_

#include <memory>
#include <string>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/callbacks.h"
#include "shill/service.h"

namespace shill {

class KeyValueStore;
class VPNDriver;

class VPNService : public Service {
 public:
  enum DriverEvent { kEventConnectionSuccess = 0 };
  using DriverEventCallback = base::RepeatingCallback<void(DriverEvent)>;

  VPNService(Manager* manager, std::unique_ptr<VPNDriver> driver);
  ~VPNService() override;

  // Inherited from Service.
  std::string GetStorageIdentifier() const override;
  bool IsAlwaysOnVpn(const std::string& package) const override;
  bool Load(const StoreInterface* storage) override;
  void MigrateDeprecatedStorage(StoreInterface* storage) override;
  bool Save(StoreInterface* storage) override;
  bool Unload() override;
  void EnableAndRetainAutoConnect() override;
  bool SetNameProperty(const std::string& name, Error* error) override;

  // Power management events.
  void OnBeforeSuspend(const ResultCallback& callback) override;
  void OnAfterResume() override;
  void OnDefaultServiceStateChanged(const ServiceRefPtr& service) override;

  virtual void InitDriverPropertyStore();

  VPNDriver* driver() const { return driver_.get(); }

  static std::string CreateStorageIdentifier(const KeyValueStore& args,
                                             Error* error);
  void set_storage_id(const std::string& id) { storage_id_ = id; }

  // Returns the Type name of the lowest connection (presumably the "physical"
  // connection) that this service depends on.
  std::string GetPhysicalTechnologyProperty(Error* error);

 protected:
  // Inherited from Service.
  void OnConnect(Error* error) override;
  void OnDisconnect(Error* error, const char* reason) override;
  bool IsAutoConnectable(const char** reason) const override;
  std::string GetTethering(Error* error) const override;

  virtual void OnDriverEvent(DriverEvent event);

 private:
  friend class VPNServiceTest;
  FRIEND_TEST(VPNServiceTest, GetDeviceRpcId);
  FRIEND_TEST(VPNServiceTest, GetPhysicalTechnologyPropertyFailsIfNoCarrier);
  FRIEND_TEST(VPNServiceTest, GetPhysicalTechnologyPropertyOverWifi);
  FRIEND_TEST(VPNServiceTest, GetTethering);
  FRIEND_TEST(VPNServiceTest, ConfigureDeviceAndDisconnect);

  static const char kAutoConnNeverConnected[];
  static const char kAutoConnVPNAlreadyActive[];

  RpcIdentifier GetDeviceRpcId(Error* error) const override;

  ConnectionConstRefPtr GetUnderlyingConnection() const;

  void ConfigureDevice();

  std::string storage_id_;
  std::unique_ptr<VPNDriver> driver_;
  VirtualDeviceRefPtr device_;

  base::WeakPtrFactory<VPNService> weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(VPNService);
};

}  // namespace shill

#endif  // SHILL_VPN_VPN_SERVICE_H_
