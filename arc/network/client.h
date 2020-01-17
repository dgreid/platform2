// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_CLIENT_H_
#define ARC_NETWORK_CLIENT_H_

#include <memory>
#include <utility>
#include <vector>

#include "base/files/scoped_file.h"
#include <brillo/brillo_export.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

namespace patchpanel {

// Simple wrapper around patchpanel DBus API. All public functions are
// blocking DBus calls to patchpaneld.
class BRILLO_EXPORT Client {
 public:
  static std::unique_ptr<Client> New();

  Client(const scoped_refptr<dbus::Bus>& bus, dbus::ObjectProxy* proxy)
      : bus_(std::move(bus)), proxy_(proxy) {}
  ~Client();

  bool NotifyArcStartup(pid_t pid);
  bool NotifyArcShutdown();

  std::vector<patchpanel::Device> NotifyArcVmStartup(uint32_t cid);
  bool NotifyArcVmShutdown(uint32_t cid);

  bool NotifyTerminaVmStartup(uint32_t cid,
                              patchpanel::Device* device,
                              patchpanel::IPv4Subnet* container_subnet);
  bool NotifyTerminaVmShutdown(uint32_t cid);

  bool NotifyPluginVmStartup(uint64_t vm_id,
                             int subnet_index,
                             patchpanel::Device* device);
  bool NotifyPluginVmShutdown(uint64_t vm_id);

  // Reset the VPN routing intent mark on a socket to the default policy for
  // the current uid. This is in general incorrect to call this method for
  // a socket that is already connected.
  bool DefaultVpnRouting(int socket);

  // Mark a socket to be always routed through a VPN if there is one.
  // Must be called before the socket is connected.
  bool RouteOnVpn(int socket);

  // Mark a socket to be always routed through the physical network.
  // Must be called before the socket is connected.
  bool BypassVpn(int socket);

  // Sends a ConnectNamespaceRequest for the given namespace pid. Returns a
  // pair with a valid ScopedFD and the ConnectNamespaceResponse proto message
  // received if the request succeeded. Closing the ScopedFD will teardown the
  // veth and routing setup and free the allocated IPv4 subnet.
  std::pair<base::ScopedFD, patchpanel::ConnectNamespaceResponse>
  ConnectNamespace(pid_t pid,
                   const std::string& outbound_ifname,
                   bool forward_user_traffic);

 private:
  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* proxy_ = nullptr;  // owned by bus_

  bool SendSetVpnIntentRequest(int socket,
                               SetVpnIntentRequest::VpnRoutingPolicy policy);

  DISALLOW_COPY_AND_ASSIGN(Client);
};

}  // namespace patchpanel

#endif  // ARC_NETWORK_CLIENT_H_
