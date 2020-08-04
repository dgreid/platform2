// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DEFAULT_PROFILE_H_
#define SHILL_DEFAULT_PROFILE_H_

#include <random>
#include <string>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/event_dispatcher.h"
#include "shill/manager.h"
#include "shill/profile.h"
#include "shill/property_store.h"
#include "shill/refptr_types.h"

namespace shill {

class DefaultProfile : public Profile {
 public:
  static const char kDefaultId[];

  DefaultProfile(Manager* manager,
                 const base::FilePath& storage_directory,
                 const std::string& profile_id,
                 const Manager::Properties& manager_props);
  ~DefaultProfile() override;

  // Loads global configuration into manager properties.  This should
  // only be called by the Manager.
  virtual void LoadManagerProperties(Manager::Properties* manager_props,
                                     DhcpProperties* dhcp_properties);

  // Override the Profile superclass implementation to accept all Ethernet
  // services, since these should have an affinity for the default profile.
  bool ConfigureService(const ServiceRefPtr& service) override;

  // Persists profile information, as well as that of discovered devices
  // and bound services, to disk.
  // Returns true on success, false on failure.
  bool Save() override;

  // Inherited from Profile.
  bool UpdateDevice(const DeviceRefPtr& device) override;

  bool IsDefault() const override { return true; }

#if !defined(DISABLE_WIFI)
  bool GetFTEnabled(Error* error);
#endif  // DISABLE_WIFI

 private:
  friend class DefaultProfileTest;
  FRIEND_TEST(DefaultProfileTest, GetStoragePath);
  FRIEND_TEST(DefaultProfileTest, LoadManagerDefaultProperties);
  FRIEND_TEST(DefaultProfileTest, LoadManagerProperties);
  FRIEND_TEST(DefaultProfileTest, Save);

  static const char kStorageId[];
  static const char kStorageArpGateway[];
  static const char kStorageCheckPortalList[];
  static const char kStorageConnectionIdSalt[];
  static const char kStorageHostName[];
  static const char kStorageIgnoredDNSSearchPaths[];
  static const char kStorageLinkMonitorTechnologies[];
  static const char kStorageName[];
  static const char kStorageNoAutoConnectTechnologies[];
  static const char kStorageProhibitedTechnologies[];
#if !defined(DISABLE_WIFI)
  static const char kStorageWifiGlobalFTEnabled[];

  void HelpRegisterConstDerivedBool(const std::string& name,
                                    bool (DefaultProfile::*get)(Error* error));
#endif  // DISABLE_WIFI

  const std::string profile_id_;
  const Manager::Properties& props_;
  std::default_random_engine random_engine_;

  DISALLOW_COPY_AND_ASSIGN(DefaultProfile);
};

}  // namespace shill

#endif  // SHILL_DEFAULT_PROFILE_H_
