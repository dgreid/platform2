// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CELLULAR_CELLULAR_SERVICE_H_
#define SHILL_CELLULAR_CELLULAR_SERVICE_H_

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <base/macros.h>
#include <base/time/time.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/cellular/cellular.h"
#include "shill/cellular/subscription_state.h"
#include "shill/mockable.h"
#include "shill/refptr_types.h"
#include "shill/service.h"

namespace shill {

class Error;
class Manager;

class CellularService : public Service {
 public:
  enum ActivationType {
    kActivationTypeNonCellular,  // For future use
    kActivationTypeOMADM,        // For future use
    kActivationTypeOTA,
    kActivationTypeOTASP,
    kActivationTypeUnknown
  };

  // A CellularService is associated with a single SIM Profile, uniquely
  // identified by |iccid|.
  // * imsi is also unique to the profile, but may not be set on construction.
  // * sim_card_id uniquely identifies the SIM card associated with this
  //   service, and is used to group available services on a SIM card.
  // A CellularService may not be the active service for the associated
  // device, so its ICCID and IMSI properties may not match the device
  // properties.
  CellularService(Manager* manager,
                  const std::string& imsi,
                  const std::string& iccid,
                  const std::string& sim_card_id);
  CellularService(const CellularService&) = delete;
  CellularService& operator=(const CellularService&) = delete;

  ~CellularService() override;

  void SetDevice(Cellular* device);

  // Public Service overrides
  void AutoConnect() override;
  void CompleteCellularActivation(Error* error) override;
  std::string GetStorageIdentifier() const override;
  std::string GetLoadableStorageIdentifier(
      const StoreInterface& storage) const override;
  bool IsLoadableFrom(const StoreInterface& storage) const override;
  bool Load(const StoreInterface* storage) override;
  void MigrateDeprecatedStorage(StoreInterface* storage) override;
  bool Save(StoreInterface* storage) override;
  bool IsVisible() const override;

  const std::string& imsi() const { return imsi_; }
  const std::string& iccid() const { return iccid_; }
  const std::string& sim_card_id() const { return sim_card_id_; }
  const CellularRefPtr& cellular() const { return cellular_; }

  void SetActivationType(ActivationType type);
  std::string GetActivationTypeString() const;

  mockable void SetActivationState(const std::string& state);
  mockable const std::string& activation_state() const {
    return activation_state_;
  }

  void SetOLP(const std::string& url,
              const std::string& method,
              const std::string& post_data);
  const Stringmap& olp() const { return olp_; }

  void SetUsageURL(const std::string& url);
  const std::string& usage_url() const { return usage_url_; }

  void SetServingOperator(const Stringmap& serving_operator);
  const Stringmap& serving_operator() const { return serving_operator_; }

  // Sets network technology to |technology| and broadcasts the property change.
  void SetNetworkTechnology(const std::string& technology);
  const std::string& network_technology() const { return network_technology_; }

  // Sets roaming state to |state| and broadcasts the property change.
  void SetRoamingState(const std::string& state);
  const std::string& roaming_state() const { return roaming_state_; }

  bool is_auto_connecting() const { return is_auto_connecting_; }

  const std::string& ppp_username() const { return ppp_username_; }
  const std::string& ppp_password() const { return ppp_password_; }

  Stringmap* GetUserSpecifiedApn();
  Stringmap* GetLastGoodApn();
  virtual void SetLastGoodApn(const Stringmap& apn_info);
  virtual void ClearLastGoodApn();

  void NotifySubscriptionStateChanged(SubscriptionState subscription_state);

  static const char kStorageIccid[];
  static const char kStorageImsi[];
  static const char kStoragePPPUsername[];
  static const char kStoragePPPPassword[];
  static const char kStorageSimCardId[];

 protected:
  // Protected Service overrides
  void OnConnect(Error* error) override;
  void OnDisconnect(Error* error, const char* reason) override;
  bool IsAutoConnectable(const char** reason) const override;
  uint64_t GetMaxAutoConnectCooldownTimeMilliseconds() const override;
  bool IsMeteredByServiceProperties() const override;
  RpcIdentifier GetDeviceRpcId(Error* error) const override;

 private:
  friend class CellularCapability3gppTest;
  friend class CellularCapabilityCdmaTest;
  friend class CellularServiceTest;
  friend class CellularTest;

  template <typename key_type, typename value_type>
  friend class ContainsCellularPropertiesMatcherP2;

  FRIEND_TEST(CellularCapability3gppMainTest, UpdatePendingActivationState);
  FRIEND_TEST(CellularTest, Connect);
  FRIEND_TEST(CellularTest, FriendlyServiceName);
  FRIEND_TEST(CellularTest, GetLogin);  // ppp_username_, ppp_password_
  FRIEND_TEST(CellularServiceTest, SetApn);
  FRIEND_TEST(CellularServiceTest, ClearApn);
  FRIEND_TEST(CellularServiceTest, LastGoodApn);
  FRIEND_TEST(CellularServiceTest, IsAutoConnectable);
  FRIEND_TEST(CellularServiceTest, LoadResetsPPPAuthFailure);
  FRIEND_TEST(CellularServiceTest, SaveAndLoadApn);
  FRIEND_TEST(CellularServiceTest, CustomSetterNoopChange);

  static const char kAutoConnActivating[];
  static const char kAutoConnBadPPPCredentials[];
  static const char kAutoConnDeviceDisabled[];
  static const char kAutoConnOutOfCredits[];

  KeyValueStore GetStorageProperties() const;
  std::string GetDefaultStorageIdentifier() const;

  void HelpRegisterDerivedString(
      const std::string& name,
      std::string (CellularService::*get)(Error* error),
      bool (CellularService::*set)(const std::string& value, Error* error));
  void HelpRegisterDerivedStringmap(
      const std::string& name,
      Stringmap (CellularService::*get)(Error* error),
      bool (CellularService::*set)(const Stringmap& value, Error* error));
  void HelpRegisterDerivedBool(const std::string& name,
                               bool (CellularService::*get)(Error* error),
                               bool (CellularService::*set)(const bool&,
                                                            Error*));

  std::set<std::string> GetStorageGroupsWithProperty(
      const StoreInterface& storage,
      const std::string& key,
      const std::string& value) const;

  std::string CalculateActivationType(Error* error);

  Stringmap GetApn(Error* error);
  bool SetApn(const Stringmap& value, Error* error);
  static void LoadApn(const StoreInterface* storage,
                      const std::string& storage_group,
                      const std::string& keytag,
                      Stringmap* apn_info);
  static bool LoadApnField(const StoreInterface* storage,
                           const std::string& storage_group,
                           const std::string& keytag,
                           const std::string& apntag,
                           Stringmap* apn_info);
  static void SaveApn(StoreInterface* storage,
                      const std::string& storage_group,
                      const Stringmap* apn_info,
                      const std::string& keytag);
  static void SaveApnField(StoreInterface* storage,
                           const std::string& storage_group,
                           const Stringmap* apn_info,
                           const std::string& keytag,
                           const std::string& apntag);
  bool IsOutOfCredits(Error* /*error*/);

  // IMSI was previously used as a unique identifuer for CellularService,
  // however it may not be available when a CellularService is created, so
  // we use ICCID instead, which is consistent with Hermes. We still store
  // IMSI for convenience and for debugging.
  std::string imsi_;

  // ICCID uniquely identifies a SIM profile.
  std::string iccid_;

  // Uniquely identifies a SIM Card (physical or eSIM). This value is used to
  // identify services that may be available on the active SIM Card.
  std::string sim_card_id_;

  ActivationType activation_type_;
  std::string activation_state_;
  Stringmap serving_operator_;
  std::string network_technology_;
  std::string roaming_state_;
  Stringmap olp_;
  std::string usage_url_;
  Stringmap apn_info_;
  Stringmap last_good_apn_info_;
  std::string ppp_username_;
  std::string ppp_password_;

  // The storage identifier defaults to cellular_{iccid}.
  std::string storage_identifier_;

  CellularRefPtr cellular_;

  // Flag indicating that a connect request is an auto-connect request.
  // Note: Since Connect() is asynchronous, this flag is only set during the
  // call to Connect().  It does not remain set while the async request is
  // in flight.
  bool is_auto_connecting_;
  // Flag indicating if the user has run out of data credits.
  bool out_of_credits_;
};

}  // namespace shill

#endif  // SHILL_CELLULAR_CELLULAR_SERVICE_H_
