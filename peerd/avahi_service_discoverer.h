// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PEERD_AVAHI_SERVICE_DISCOVERER_H_
#define PEERD_AVAHI_SERVICE_DISCOVERER_H_

#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <base/memory/ref_counted.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>

#include "peerd/service.h"
#include "peerd/typedefs.h"

namespace dbus {

class ObjectProxy;
class Signal;

}  // namespace dbus

namespace peerd {

class PeerManagerInterface;

// Avahi allows consumers to discover services in two phases: browsing and
// resolution.  Service discovery is done by requesting Avahi to create
// AvahiServiceBrowser objects for a particular service type (e.g.
// "_privet._tcp").  Inside our own process, we need to keep DBus proxies
// for those objects around.
//
// Then, we start getting signals for service creation/deletion from Avahi
// through our AvahiServiceBrowser objects.  Avahi identifies services by a
// combination of:
//  1) The interface we've discovered that service on
//  2) The unique name of the service
//  3) The type of the service
//  4) The domain the service was discovered on.
//
//  For each instance of a service, we then need to ask Avahi to create an
//  AvahiServiceResolver to read the TXT record and signal changes to the TXT
//  record.  Again, we need to keep local DBus proxies for those remote
//  objects.
//
//  When we get a signal that a service instance is gone, we remove the
//  resolver from Avahi.  When we have no peers advertising a particular
//  service type via root _serbus records, we remove the service browser for
//  that type.
class AvahiServiceDiscoverer final {
 public:
  AvahiServiceDiscoverer(const scoped_refptr<dbus::Bus>& bus,
                         dbus::ObjectProxy* avahi_proxy,
                         PeerManagerInterface* peer_manager_);
  ~AvahiServiceDiscoverer();

  void RegisterAsync(const CompletionAction& completion_callback);

 private:
  using avahi_if_t = int32_t;
  using avahi_proto_t = int32_t;  // Either IPv4 or IPv6.
  using TxtList = std::vector<std::vector<uint8_t>>;  // DBus type aay.
  // A resolver corrresponds to a particular name/type/domain/interface tuple,
  // but we organize them by type for book keeping reasons, and so this is just
  // a tuple<interface, name, domain>.
  using resolv_key_t = std::tuple<avahi_if_t, std::string, std::string>;
  using ResolversForType = std::map<resolv_key_t, dbus::ObjectProxy*>;
  // A map of service types to the resolvers for that type.
  using ResolverMap = std::map<std::string, ResolversForType>;

  static bool txt_list2service_info(const TxtList& txt_list,
                                    Service::ServiceInfo* info);

  // Creates a new AvahiServiceBrowser, hooks up signals, returns it.
  // |cb| is called asynchronously with the success or failure of
  // signal registration.
  dbus::ObjectProxy* BrowseServices(const std::string& service_type,
                                    const CompletionAction& cb);

  // Updates internal datastructures to reflect that the root serbus
  // service for a peer has changed.  This may cause us to stop browsing
  // for one or more service types if no other peers claim to support
  // those service types.
  void OnPeerServicesChanged(const std::string& peer_id,
                             std::set<std::string> new_service_types);
  void OnPeerServicesRemoved(const std::string& peer_id);

  // Listen to changes in TXT records for a service.
  void RegisterResolver(avahi_if_t interface,
                        const std::string& name,
                        const std::string& type,
                        const std::string& domain);
  // Stop listening to TXT record changes.
  void RemoveResolver(avahi_if_t interface,
                      const std::string& name,
                      const std::string& type,
                      const std::string& domain);

  // Logic to respond to new services being discovered/removed.
  void HandleItemNew(avahi_if_t interface,
                     avahi_proto_t protocol,
                     const std::string& name,
                     const std::string& type,
                     const std::string& domain,
                     uint32_t flags);
  void HandleItemRemove(avahi_if_t interface,
                        avahi_proto_t protocol,
                        const std::string& name,
                        const std::string& type,
                        const std::string& domain,
                        uint32_t flags);
  // Signals that Avahi has had some serious trouble.
  void HandleFailure(const std::string& service_type,
                     const std::string& message);
  // For notifications from our AvahiServiceResolver(s).
  void HandleFound(dbus::Signal* signal);
  // And in case we encounter failure in a Resolver...
  void HandleResolverFailure(avahi_if_t interface,
                             const std::string& name,
                             const std::string& type,
                             const std::string& domain,
                             dbus::Signal* signal);

  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* avahi_proxy_;
  PeerManagerInterface* peer_manager_;
  avahi_proto_t protocol_;  // We support one protocol per discoverer (IPv4).
  dbus::ObjectProxy* serbus_browser_;
  // A map from service type to the browser for that service type.
  std::map<std::string, dbus::ObjectProxy*> browsers_;
  // A map from root serbus record names to the corresponding unique peer id.
  std::map<resolv_key_t, std::string> serbus_record_to_peer_id_;
  // A map from service type to list of peer ids of peers advertising that
  // service.
  std::map<std::string, std::set<std::string>> peers_for_service_;
  ResolverMap resolvers_;
  // Should be last member to invalidate weak pointers in child objects
  // and avoid callbacks while partially destroyed.
  base::WeakPtrFactory<AvahiServiceDiscoverer> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(AvahiServiceDiscoverer);
};

}  // namespace peerd

#endif  // PEERD_AVAHI_SERVICE_DISCOVERER_H_
