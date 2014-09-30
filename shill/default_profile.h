// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DEFAULT_PROFILE_H_
#define SHILL_DEFAULT_PROFILE_H_

#include <random>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/memory/scoped_ptr.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/event_dispatcher.h"
#include "shill/manager.h"
#include "shill/profile.h"
#include "shill/property_store.h"
#include "shill/refptr_types.h"

namespace shill {

class ControlInterface;
class WiFiProvider;

class DefaultProfile : public Profile {
 public:
  static const char kDefaultId[];

  DefaultProfile(ControlInterface *control,
                 Metrics *metrics,
                 Manager *manager,
                 const base::FilePath &storage_path,
                 const std::string &profile_id,
                 const Manager::Properties &manager_props);
  ~DefaultProfile() override;

  // Loads global configuration into manager properties.  This should
  // only be called by the Manager.
  virtual void LoadManagerProperties(Manager::Properties *manager_props);

  // Override the Profile superclass implementation to accept all Ethernet
  // services, since these should have an affinity for the default profile.
  virtual bool ConfigureService(const ServiceRefPtr &service);

  // Persists profile information, as well as that of discovered devices
  // and bound services, to disk.
  // Returns true on success, false on failure.
  virtual bool Save();

  // Inherited from Profile.
  virtual bool UpdateDevice(const DeviceRefPtr &device);

  // Inherited from Profile.
  virtual bool UpdateWiFiProvider(const WiFiProvider &wifi_provider);

  bool IsDefault() const override { return true; }

 protected:
  // Sets |path| to the persistent store file path for the default, global
  // profile. Returns true on success, and false if unable to determine an
  // appropriate file location.
  //
  // In this implementation, |name_| is ignored.
  virtual bool GetStoragePath(base::FilePath *path);

 private:
  friend class DefaultProfileTest;
  FRIEND_TEST(DefaultProfileTest, GetStoragePath);
  FRIEND_TEST(DefaultProfileTest, LoadManagerDefaultProperties);
  FRIEND_TEST(DefaultProfileTest, LoadManagerProperties);
  FRIEND_TEST(DefaultProfileTest, Save);

  static const char kStorageId[];
  static const char kStorageArpGateway[];
  static const char kStorageCheckPortalList[];
  static const char kStorageHostName[];
  static const char kStorageIgnoredDNSSearchPaths[];
  static const char kStorageLinkMonitorTechnologies[];
  static const char kStorageName[];
  static const char kStorageNoAutoConnectTechnologies[];
  static const char kStorageOfflineMode[];
  static const char kStoragePortalCheckInterval[];
  static const char kStoragePortalURL[];
  static const char kStorageConnectionIdSalt[];

  const base::FilePath storage_path_;
  const std::string profile_id_;
  const Manager::Properties &props_;
  std::default_random_engine random_engine_;

  DISALLOW_COPY_AND_ASSIGN(DefaultProfile);
};

}  // namespace shill

#endif  // SHILL_DEFAULT_PROFILE_H_
