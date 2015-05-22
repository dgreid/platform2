// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PEERD_MANAGER_H_
#define PEERD_MANAGER_H_

#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

#include <base/macros.h>
#include <chromeos/any.h>
#include <chromeos/dbus/dbus_object.h>
#include <chromeos/dbus/dbus_service_watcher.h>
#include <chromeos/errors/error.h>

#include "peerd/avahi_client.h"
#include "peerd/org.chromium.peerd.Manager.h"
#include "peerd/peer_manager_interface.h"
#include "peerd/published_peer.h"
#include "peerd/technologies.h"
#include "peerd/typedefs.h"

namespace dbus {
class Bus;
}  // namespace dbus

namespace chromeos {
namespace dbus_utils {
class ExportedObjectManager;
}  // dbus_utils
}  // chromeos

namespace peerd {

namespace errors {
namespace manager {

extern const char kAlreadyExposed[];
extern const char kInvalidMonitoringOption[];
extern const char kInvalidMonitoringTechnology[];
extern const char kInvalidMonitoringToken[];
extern const char kNotOwner[];
extern const char kUnknownServiceId[];

}  // namespace manager
}  // namespace errors

// Manages global state of peerd.
class Manager : public org::chromium::peerd::ManagerInterface {
 public:
  Manager(chromeos::dbus_utils::ExportedObjectManager* object_manager,
          const std::string& initial_mdns_prefix);
  ~Manager() override = default;
  void RegisterAsync(const CompletionAction& completion_callback);

  // DBus handlers
  bool StartMonitoring(
      chromeos::ErrorPtr* error,
      const std::vector<std::string>& requested_technologies,
      const std::map<std::string, chromeos::Any>& options,
      std::string* monitoring_token) override;

  bool StopMonitoring(
      chromeos::ErrorPtr* error,
      const std::string& monitoring_token) override;

  bool ExposeService(
      chromeos::ErrorPtr* error,
      dbus::Message* message,
      const std::string& service_id,
      const std::map<std::string, std::string>& service_info,
      const std::map<std::string, chromeos::Any>& options) override;

  bool RemoveExposedService(chromeos::ErrorPtr* error,
                            dbus::Message* message,
                            const std::string& service_id) override;

  std::string Ping() override;

 private:
  // Used in unit tests to inject mocks.
  Manager(scoped_refptr<dbus::Bus> bus,
          std::unique_ptr<chromeos::dbus_utils::DBusObject> dbus_object,
          std::unique_ptr<PublishedPeer> self,
          std::unique_ptr<PeerManagerInterface> peer_manager,
          std::unique_ptr<AvahiClient> avahi_client,
          const std::string& initial_mdns_prefix);

  // Called from AvahiClient.
  void ShouldRefreshAvahiPublisher();

  // Crawls the map of monitoring requests and updates
  // |monitored_technologies_| to be consistent.  Calls StartMonitoring
  // and StopMonitoring on technologies as appropriate.
  void UpdateMonitoredTechnologies();

  void OnDBusServiceDeath(const std::string& service_id);

  scoped_refptr<dbus::Bus> bus_;
  org::chromium::peerd::ManagerAdaptor dbus_adaptor_{this};
  std::unique_ptr<chromeos::dbus_utils::DBusObject> dbus_object_;
  std::unique_ptr<PublishedPeer> self_;
  std::unique_ptr<PeerManagerInterface> peer_manager_;
  std::unique_ptr<AvahiClient> avahi_client_;
  using DBusServiceWatcher = chromeos::dbus_utils::DBusServiceWatcher;
  // A map of service ids to DBusServiceWatchers.
  std::map<std::string, std::unique_ptr<DBusServiceWatcher>> exposed_services_;
  std::map<std::string, technologies::TechnologySet> monitoring_requests_;
  size_t monitoring_tokens_issued_{0};

  FRIEND_TEST(ManagerTest, ShouldRejectSerbusServiceId);
  FRIEND_TEST(ManagerTest, ShouldRemoveServicesOnRemoteDeath);
  friend class ManagerDBusProxyTest;
  friend class ManagerTest;
  DISALLOW_COPY_AND_ASSIGN(Manager);
};

}  // namespace peerd

#endif  // PEERD_MANAGER_H_
