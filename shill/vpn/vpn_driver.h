// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_VPN_VPN_DRIVER_H_
#define SHILL_VPN_VPN_DRIVER_H_

#include <string>
#include <vector>

#include <base/cancelable_callback.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/callbacks.h"
#include "shill/key_value_store.h"
#include "shill/mockable.h"
#include "shill/refptr_types.h"
#include "shill/vpn/vpn_service.h"

namespace shill {

class ControlInterface;
class Error;
class EventDispatcher;
class Manager;
class Metrics;
class ProcessManager;
class PropertyStore;
class StoreInterface;

class VPNDriver {
 public:
  // Indicating how virtual interface is managed for this type of driver
  enum IfType {
    // Driver keeps track of interface and maintain the state machine.
    // TODO(taoyl): As per b/170478571 those ownership should be moved
    // to service, and one of the three following values should be used
    // instead. This value is only used during the period of migration.
    kDriverManaged = 0,
    // VPNService calls DeviceInfo to create a tun interface, and pass
    // the ifname to driver before ConnectAsync().
    kTunnel = 1,
    // A ppp interface will be created by external pppd process after
    // ConnectAsync() and VPNService will capture it.
    kPPP = 2,
    // Uses the always-present arc bridge interface
    kArcBridge = 3,
  };

  virtual ~VPNDriver();

  virtual void Connect(const VPNServiceRefPtr& service, Error* error) = 0;
  virtual void Disconnect() = 0;
  virtual std::string GetProviderType() const = 0;
  virtual IfType GetIfType() const = 0;

  // TODO(taoyl): Implement these in all drivers and change them to abstract.
  virtual void ConnectAsync(const VPNService::DriverEventCallback& callback) {
    LOG(DFATAL) << "Not implemented";
  }
  virtual IPConfig::Properties GetIPProperties() const {
    LOG(DFATAL) << "Not implemented";
    return IPConfig::Properties();
  }

  virtual void InitPropertyStore(PropertyStore* store);

  virtual bool Load(const StoreInterface* storage,
                    const std::string& storage_id);
  void MigrateDeprecatedStorage(StoreInterface* storage,
                                const std::string& storage_id);
  virtual bool Save(StoreInterface* storage,
                    const std::string& storage_id,
                    bool save_credentials);
  mockable void UnloadCredentials();

  // Power management events.
  virtual void OnBeforeSuspend(const ResultCallback& callback);
  virtual void OnAfterResume();
  virtual void OnDefaultServiceStateChanged(const ServiceRefPtr& service);

  mockable std::string GetHost() const;

  std::string interface_name() const { return interface_name_; }
  void set_interface_name(const std::string& interface_name) {
    interface_name_ = interface_name;
  }

  KeyValueStore* args() { return &args_; }
  const KeyValueStore* const_args() const { return &args_; }

 protected:
  struct Property {
    enum Flags {
      kEphemeral = 1 << 0,   // Never load or save.
      kCredential = 1 << 1,  // Save if saving credentials (crypted).
      kWriteOnly = 1 << 2,   // Never read over RPC.
      kArray = 1 << 3,       // Property is an array of strings.
    };

    const char* property;
    int flags;
  };

  VPNDriver(Manager* manager,
            ProcessManager* process_manager,
            const Property* properties,
            size_t property_count);

  ControlInterface* control_interface() const;
  EventDispatcher* dispatcher() const;
  Metrics* metrics() const;
  Manager* manager() const { return manager_; }
  ProcessManager* process_manager() const { return process_manager_; }

  virtual KeyValueStore GetProvider(Error* error);

  // Initializes a callback that will invoke OnConnectTimeout after
  // |timeout_seconds|. The timeout will not be restarted if it's already
  // scheduled.
  void StartConnectTimeout(int timeout_seconds);
  // Cancels the connect timeout callback, if any, previously scheduled through
  // StartConnectTimeout.
  void StopConnectTimeout();
  // Returns true if a connect timeout is scheduled, false otherwise.
  bool IsConnectTimeoutStarted() const;

  // Called if a connect timeout scheduled through StartConnectTimeout
  // fires. Cancels the timeout callback.
  virtual void OnConnectTimeout();

  int connect_timeout_seconds() const { return connect_timeout_seconds_; }

  VPNServiceRefPtr service() const { return service_; }
  void set_service(const VPNServiceRefPtr& service) { service_ = service; }

  std::string interface_name_;

 private:
  friend class VPNDriverTest;

  static const char kCredentialPrefix[];

  void ClearMappedStringProperty(const size_t& index, Error* error);
  void ClearMappedStringsProperty(const size_t& index, Error* error);
  std::string GetMappedStringProperty(const size_t& index, Error* error);
  std::vector<std::string> GetMappedStringsProperty(const size_t& index,
                                                    Error* error);
  bool SetMappedStringProperty(const size_t& index,
                               const std::string& value,
                               Error* error);
  bool SetMappedStringsProperty(const size_t& index,
                                const std::vector<std::string>& value,
                                Error* error);

  Manager* manager_;
  ProcessManager* process_manager_;

  VPNServiceRefPtr service_;

  const Property* const properties_;
  const size_t property_count_;
  KeyValueStore args_;

  base::CancelableClosure connect_timeout_callback_;
  int connect_timeout_seconds_;

  base::WeakPtrFactory<VPNDriver> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(VPNDriver);
};

}  // namespace shill

#endif  // SHILL_VPN_VPN_DRIVER_H_
