// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PEERD_AVAHI_SERVICE_PUBLISHER_H_
#define PEERD_AVAHI_SERVICE_PUBLISHER_H_

#include <map>
#include <stdint.h>
#include <string>
#include <vector>

#include <base/macros.h>
#include <chromeos/errors/error.h>
#include <gtest/gtest_prod.h>

#include "peerd/service.h"
#include "peerd/service_publisher_interface.h"

namespace dbus {
class ObjectPath;
}  // namespace dbus

namespace peerd {

namespace errors {
namespace avahi {

extern const char kRemovedUnknownService[];
extern const char kInvalidServiceId[];

}  // namespace avahi
}  // namespace errors

class AvahiServicePublisher : public ServicePublisherInterface {
 public:
  AvahiServicePublisher(const std::string& uuid,
                        const std::string& unique_prefix,
                        const scoped_refptr<dbus::Bus>& bus,
                        dbus::ObjectProxy* avahi_proxy,
                        const base::Closure& on_publish_failure);
  ~AvahiServicePublisher() override;
  base::WeakPtr<AvahiServicePublisher> GetWeakPtr();

  // See comments in ServicePublisherInterface.
  bool OnServiceUpdated(chromeos::ErrorPtr* error,
                        const Service& service) override;
  bool OnServiceRemoved(chromeos::ErrorPtr* error,
                        const std::string& service_id) override;

 private:
  using TxtRecord = std::vector<std::vector<uint8_t>>;
  // Transform a service_info to a mDNS compatible TXT record value.
  // Concretely, a TXT record consists of a list of strings in the format
  // "key=value".  Each string must be less than 256 bytes long, since they are
  // length/value encoded.  Keys may not contain '=' characters, but are
  // otherwise unconstrained.
  //
  // We need a DBus type of "aay", which is a vector<vector<uint8_t>> in our
  // bindings.
  static TxtRecord GetTxtRecord(const Service::ServiceInfo& service);

  bool UpdateGroup(chromeos::ErrorPtr* error,
                   const std::string& service_id,
                   const Service::ServiceInfo& service_info,
                   const Service::MDnsOptions& mdns_options);
  // Attempts to add the given |service_id|/|service_info| pair to the
  // given |group_proxy|.  Returns true on success, false otherwise.
  // Does no cleanup.
  bool AddServiceToGroup(chromeos::ErrorPtr* error,
                         const std::string& service_id,
                         const Service::ServiceInfo& service_info,
                         const Service::MDnsOptions& mdns_options,
                         dbus::ObjectProxy* group_proxy);
  // Removes all records corresponding to the provided |group_proxy| and
  // detaches from any related signals.
  bool FreeGroup(chromeos::ErrorPtr* error, dbus::ObjectProxy* group_proxy);
  // Update the master service listing to include the given |service_id|.
  bool UpdateRootService(chromeos::ErrorPtr* error);
  // We get notified when a service in the group encounters a name collision,
  // and other more innocuous events (like service publishing finishing).
  // We must react to name collisions and other failures however and pick a
  // new unique name prefix to register services under.
  void HandleGroupStateChanged(int32_t state, const std::string& error_message);


  const std::string uuid_;
  const std::string unique_prefix_;
  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* avahi_proxy_;
  std::map<std::string, dbus::ObjectProxy*> outstanding_groups_;
  base::Closure on_publish_failure_;

  // Should be last member to invalidate weak pointers in child objects
  // (like avahi_proxy_) and avoid callbacks while partially destroyed.
  base::WeakPtrFactory<AvahiServicePublisher> weak_ptr_factory_{this};
  FRIEND_TEST(AvahiServicePublisherTest, CanEncodeTxtRecords);
  DISALLOW_COPY_AND_ASSIGN(AvahiServicePublisher);
};

}  // namespace peerd

#endif  // PEERD_AVAHI_SERVICE_PUBLISHER_H_
