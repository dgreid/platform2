// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_CELLULAR_CAPABILITY_H_
#define SHILL_CELLULAR_CELLULAR_CAPABILITY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/callbacks.h"
#include "shill/cellular/cellular.h"

namespace shill {

class CellularBearer;
class ControlInterface;
class Error;
class ModemInfo;

// Cellular devices instantiate subclasses of CellularCapability that
// handle the specific modem technologies and capabilities.
//
// The CellularCapability is directly subclassed by
// CellularCapability3gpp which handles all modems managed by
// a modem manager using the the org.freedesktop.ModemManager1 D-Bus
// interface.
//
// Pictorially:
//
// CellularCapability
//       |
//       |-- CellularCapability3gpp
//                    |
//                    |-- CellularCapabilityCdma
class CellularCapability {
 public:
  static const int kTimeoutActivate;
  static const int kTimeoutConnect;
  static const int kTimeoutDefault;
  static const int kTimeoutDisconnect;
  static const int kTimeoutEnable;
  static const int kTimeoutGetLocation;
  static const int kTimeoutRegister;
  static const int kTimeoutReset;
  static const int kTimeoutScan;
  static const int kTimeoutSetInitialEpsBearer;
  static const int kTimeoutSetupLocation;

  static std::unique_ptr<CellularCapability> Create(Cellular::Type type,
                                                    Cellular* cellular,
                                                    ModemInfo* modem_info);

  virtual ~CellularCapability();

  virtual std::string GetTypeString() const = 0;

  // Called when the modem manager has sent a property change notification
  // signal.
  virtual void OnPropertiesChanged(
      const std::string& interface,
      const KeyValueStore& changed_properties,
      const std::vector<std::string>& invalidated_properties) = 0;

  // -------------------------------------------------------------------------
  // Modem management
  // -------------------------------------------------------------------------

  // StartModem attempts to put the modem in a state in which it is usable for
  // creating services and establishing connections (if network conditions
  // permit). It potentially consists of multiple non-blocking calls to the
  // modem-manager server. After each call, control is passed back up to the
  // main loop. Each time a reply to a non-blocking call is received, the
  // operation advances to the next step, until either an error occurs in one of
  // them, or all the steps have been completed, at which point StartModem() is
  // finished.
  virtual void StartModem(Error* error, const ResultCallback& callback) = 0;

  // StopModem disconnects and disables a modem asynchronously.  |callback| is
  // invoked when this completes and the result is passed to the callback.
  virtual void StopModem(Error* error, const ResultCallback& callback) = 0;

  // Resets the modem.
  virtual void Reset(Error* error, const ResultCallback& callback) = 0;

  // Checks to see if all proxies have been initialized.
  virtual bool AreProxiesInitialized() const = 0;

  // -------------------------------------------------------------------------
  // Activation
  // -------------------------------------------------------------------------

  // Returns true if service activation is required.
  virtual bool IsServiceActivationRequired() const = 0;

  // Returns true if the modem is being activated.
  virtual bool IsActivating() const = 0;

  // Initiates the necessary to steps to verify that the cellular service has
  // been activated. Once these steps have been completed, the service should
  // be marked as activated.
  virtual void CompleteActivation(Error* error) = 0;

  // -------------------------------------------------------------------------
  // Network service and registration
  // -------------------------------------------------------------------------

  // Asks the modem to scan for networks.
  //
  // Subclasses should implement this by fetching scan results asynchronously.
  // When the results are ready, update the kFoundNetworksProperty and send a
  // property change notification.  Finally, callback must be invoked to inform
  // the caller that the scan has completed.
  //
  // Errors are not generally reported, but on error the kFoundNetworksProperty
  // should be cleared and a property change notification sent out.
  //
  // TODO(jglasgow): Refactor to reuse code by putting notification logic into
  // Cellular or CellularCapability.
  //
  // TODO(jglasgow): Implement real error handling.
  virtual void Scan(Error* error, const ResultStringmapsCallback& callback) = 0;

  // Sets the parameters specified by |properties| for the LTE initial EPS
  // bearer used at registration, particularly the 'Attach' APN settings.
  // specified by |properties|.
  virtual void SetInitialEpsBearer(const KeyValueStore& properties,
                                   Error* error,
                                   const ResultCallback& callback) = 0;

  // Registers on a network with |network_id|.
  virtual void RegisterOnNetwork(const std::string& network_id,
                                 Error* error,
                                 const ResultCallback& callback) = 0;

  // Returns true if the modem is registered on a network, which can be a home
  // or roaming network. It is possible that we cannot determine whether it is
  // a home or roaming network, but we still consider the modem is registered.
  virtual bool IsRegistered() const = 0;

  // If we are informed by means of something other than a signal indicating
  // a registration state change that the modem has unregistered from the
  // network, we need to update the network-type-specific capability object.
  virtual void SetUnregistered(bool searching) = 0;

  // Invoked by the parent Cellular device when a new service is created.
  virtual void OnServiceCreated() = 0;

  virtual void UpdateServiceOLP() = 0;

  // Returns an empty string if the network technology is unknown.
  virtual std::string GetNetworkTechnologyString() const = 0;

  virtual std::string GetRoamingStateString() const = 0;

  // -------------------------------------------------------------------------
  // Location reporting
  // -------------------------------------------------------------------------
  virtual void SetupLocation(uint32_t sources,
                             bool signal_location,
                             const ResultCallback& callback) = 0;

  virtual void GetLocation(const StringCallback& callback) = 0;

  virtual bool IsLocationUpdateSupported() const = 0;

  // -------------------------------------------------------------------------
  // Connection management
  // -------------------------------------------------------------------------

  // Fills |properties| with properties for establishing a connection, which
  // will be passed to Connect().
  virtual void SetupConnectProperties(KeyValueStore* properties) = 0;

  // Connects the modem to a network based on the connection properties
  // specified by |properties|.
  virtual void Connect(const KeyValueStore& properties,
                       Error* error,
                       const ResultCallback& callback) = 0;

  // Disconnects the modem from a network.
  virtual void Disconnect(Error* error, const ResultCallback& callback) = 0;

  // Returns a pointer to the current active bearer object or nullptr if no
  // active bearer exists. The returned bearer object is managed by this
  // capability object.
  virtual CellularBearer* GetActiveBearer() const = 0;

  virtual const std::vector<std::unique_ptr<MobileOperatorInfo::MobileAPN>>&
  GetProfiles() const = 0;

  // -------------------------------------------------------------------------
  // SIM PIN management
  // -------------------------------------------------------------------------

  virtual void RequirePin(const std::string& pin,
                          bool require,
                          Error* error,
                          const ResultCallback& callback) = 0;

  virtual void EnterPin(const std::string& pin,
                        Error* error,
                        const ResultCallback& callback) = 0;

  virtual void UnblockPin(const std::string& unblock_code,
                          const std::string& pin,
                          Error* error,
                          const ResultCallback& callback) = 0;

  virtual void ChangePin(const std::string& old_pin,
                         const std::string& new_pin,
                         Error* error,
                         const ResultCallback& callback) = 0;

  // -------------------------------------------------------------------------

  Cellular* cellular() const { return cellular_; }
  ControlInterface* control_interface() const { return control_interface_; }
  ModemInfo* modem_info() const { return modem_info_; }

 protected:
  // |cellular| is the parent Cellular device.
  CellularCapability(Cellular* cellular, ModemInfo* modem_info);
  CellularCapability(const CellularCapability&) = delete;
  CellularCapability& operator=(const CellularCapability&) = delete;

  // Releases all proxies held by the object. This is most useful during unit
  // tests.
  virtual void ReleaseProxies() = 0;

  static void OnUnsupportedOperation(const char* operation, Error* error);

 private:
  friend class CellularCapability3gppTest;
  friend class CellularCapabilityCdmaTest;
  friend class CellularTest;
  FRIEND_TEST(CellularCapability3gppMainTest, UpdateActiveBearer);
  FRIEND_TEST(CellularTest, Connect);

  Cellular* cellular_;
  ControlInterface* control_interface_;
  ModemInfo* modem_info_;
};

}  // namespace shill

#endif  // SHILL_CELLULAR_CELLULAR_CAPABILITY_H_
