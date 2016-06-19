// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORKD_MANAGER_H_
#define ARC_NETWORKD_MANAGER_H_

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <brillo/daemons/dbus_daemon.h>

#include "arc-networkd/arc_ip_config.h"
#include "arc-networkd/multicast_forwarder.h"
#include "arc-networkd/neighbor_finder.h"
#include "arc-networkd/router_finder.h"
#include "arc-networkd/shill_client.h"

namespace arc_networkd {

// Main class that runs the mainloop and responds to LAN interface changes.
class Manager final : public brillo::DBusDaemon {
 public:
  struct Options {
    std::string int_ifname;
    std::string con_ifname;
    pid_t con_netns;
  };

  explicit Manager(const Options& opt);

 protected:
  int OnInit() override;

 private:
  // Called once, after the dbus connection is established.
  void InitialSetup();

  // Callback from ShillClient, invoked whenever the default network
  // interface changes or goes away.
  void OnDefaultInterfaceChanged(const std::string& ifname);

  // Callback from RouterFinder.  May be triggered multiple times, e.g.
  // if the route disappears or changes.
  void OnRouteFound(const struct in6_addr& prefix,
                    int prefix_len,
                    const struct in6_addr& router);

  // Callback from NeighborFinder to indicate whether an IPv6 address
  // collision was found or not found.
  void OnNeighborCheckResult(bool found);

  // Persistent objects.
  std::unique_ptr<ShillClient> shill_client_;
  std::unique_ptr<ArcIpConfig> arc_ip_config_;
  int con_init_tries_;

  std::string int_ifname_;
  std::string lan_ifname_;
  std::string con_ifname_;

  pid_t con_netns_;
  struct in6_addr random_address_;
  int random_address_prefix_len_;
  int random_address_tries_;

  // These get nuked every time the connection changes.  Deletion of
  // the object immediately stops all callbacks and activity on the
  // old interface.
  std::unique_ptr<MulticastForwarder> mdns_forwarder_;
  std::unique_ptr<MulticastForwarder> ssdp_forwarder_;
  std::unique_ptr<RouterFinder> router_finder_;
  std::unique_ptr<NeighborFinder> neighbor_finder_;

  base::WeakPtrFactory<Manager> weak_factory_{this};
};

}  // namespace arc_networkd

#endif  // ARC_NETWORKD_MANAGER_H_
