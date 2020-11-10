// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_BROADCAST_FORWARDER_H_
#define PATCHPANEL_BROADCAST_FORWARDER_H_

#include <sys/socket.h>

#include <shill/net/rtnl_listener.h>
#include "patchpanel/shill_client.h"

#include <map>
#include <memory>
#include <string>

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/macros.h>

#include "patchpanel/net_util.h"

namespace patchpanel {

constexpr uint32_t kBcastAddr = Ipv4Addr(255, 255, 255, 255);

// Listens to broadcast messages sent by applications and forwards them between
// network interfaces of host and guest.
// BroadcastForwarder assumes that guest addresses, including broadcast and
// netmask, are constant.
class BroadcastForwarder {
 public:
  explicit BroadcastForwarder(const std::string& dev_ifname);
  BroadcastForwarder(const BroadcastForwarder&) = delete;
  BroadcastForwarder& operator=(const BroadcastForwarder&) = delete;

  virtual ~BroadcastForwarder() = default;

  bool AddGuest(const std::string& br_ifname);
  void RemoveGuest(const std::string& br_ifname);

 protected:
  // Socket is used to keep track of an fd and its watcher.
  // It also stores addresses corresponding to the interface it is bound to.
  struct Socket {
    Socket(base::ScopedFD fd,
           const base::Callback<void(int)>& callback,
           uint32_t addr,
           uint32_t broadaddr,
           uint32_t netmask = 0);
    base::ScopedFD fd;
    std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher;
    uint32_t addr;
    uint32_t broadaddr;
    uint32_t netmask;
  };

  // Bind will create a broadcast socket and return its fd.
  // This is used for sending broadcasts.
  static base::ScopedFD Bind(const std::string& ifname, uint16_t port);

  // BindRaw will create a broadcast socket that listens to all IP packets.
  // It filters the packets to only broadcast packets that is sent by
  // applications.
  // This is used to listen on broadcasts.
  static base::ScopedFD BindRaw(const std::string& ifname);

  // SendToNetwork sends |data| using a socket bound to |src_port| and
  // |dev_ifname_| using a temporary socket.
  bool SendToNetwork(uint16_t src_port,
                     const void* data,
                     ssize_t len,
                     const struct sockaddr_in& dst);

  // SendToGuests will forward the broadcast packet to all Chrome OS guests'
  // (ARC++, Crostini, etc) internal fd.
  bool SendToGuests(const void* ip_pkt,
                    ssize_t len,
                    const struct sockaddr_in& dst);

  // Callback from RTNetlink listener, invoked when the lan interface IPv4
  // address is changed.
  void AddrMsgHandler(const shill::RTNLMessage& msg);

  // Listens for RTMGRP_IPV4_IFADDR messages and invokes AddrMsgHandler.
  std::unique_ptr<shill::RTNLListener> addr_listener_;

  const std::string dev_ifname_;
  std::unique_ptr<Socket> dev_socket_;

  // Mapping from guest bridge interface name to its sockets.
  std::map<std::string, std::unique_ptr<Socket>> br_sockets_;

 private:
  void OnFileCanReadWithoutBlocking(int fd);

  base::WeakPtrFactory<BroadcastForwarder> weak_factory_{this};
};

}  // namespace patchpanel

#endif  // PATCHPANEL_BROADCAST_FORWARDER_H_
