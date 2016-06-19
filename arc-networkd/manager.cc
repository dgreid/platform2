// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <stdint.h>

#include <base/bind.h>
#include <base/logging.h>
#include <base/message_loop/message_loop.h>
#include <base/time/time.h>

#include "arc-networkd/manager.h"

namespace {

const char kMdnsMcastAddress[] = "224.0.0.251";
const uint16_t kMdnsPort = 5353;
const char kSsdpMcastAddress[] = "239.255.255.250";
const uint16_t kSsdpPort = 1900;

const int kMaxRandomAddressTries = 3;

const int kContainerRetryDelaySeconds = 5;
const int kMaxContainerRetries = 60;

}  // namespace

namespace arc_networkd {

Manager::Manager(const Options& opt) :
    con_init_tries_(0),
    int_ifname_(opt.int_ifname),
    con_ifname_(opt.con_ifname),
    con_netns_(opt.con_netns) {}

int Manager::OnInit() {
  arc_ip_config_.reset(new ArcIpConfig(int_ifname_, con_ifname_, con_netns_));
  CHECK(arc_ip_config_->Init());

  // This needs to execute after DBusDaemon::OnInit().
  base::MessageLoopForIO::current()->PostTask(
      FROM_HERE,
      base::Bind(&Manager::InitialSetup, weak_factory_.GetWeakPtr()));

  return DBusDaemon::OnInit();
}

void Manager::InitialSetup() {
  if (!arc_ip_config_->ContainerInit()) {
    if (++con_init_tries_ >= kMaxContainerRetries) {
      LOG(FATAL) << "Container failed to come up";
    } else {
      base::MessageLoopForIO::current()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&Manager::InitialSetup, weak_factory_.GetWeakPtr()),
          base::TimeDelta::FromSeconds(kContainerRetryDelaySeconds));
    }
    return;
  }

  shill_client_.reset(new ShillClient(std::move(bus_)));
  shill_client_->RegisterDefaultInterfaceChangedHandler(
      base::Bind(&Manager::OnDefaultInterfaceChanged,
                 weak_factory_.GetWeakPtr()));
}

void Manager::OnDefaultInterfaceChanged(const std::string& ifname) {
  arc_ip_config_->Clear();
  neighbor_finder_.reset();

  lan_ifname_ = ifname;
  if (ifname.empty()) {
    LOG(INFO) << "Unbinding services";
    mdns_forwarder_.reset();
    ssdp_forwarder_.reset();
    router_finder_.reset();
  } else {
    LOG(INFO) << "Binding to interface " << ifname;
    mdns_forwarder_.reset(new MulticastForwarder());
    ssdp_forwarder_.reset(new MulticastForwarder());
    router_finder_.reset(new RouterFinder());

    mdns_forwarder_->Start(int_ifname_,
                           ifname,
                           kMdnsMcastAddress,
                           kMdnsPort,
                           /* allow_stateless */ true);
    ssdp_forwarder_->Start(int_ifname_,
                           ifname,
                           kSsdpMcastAddress,
                           kSsdpPort,
                           /* allow_stateless */ false);

    router_finder_->Start(ifname,
        base::Bind(&Manager::OnRouteFound, weak_factory_.GetWeakPtr()));
  }
}

void Manager::OnRouteFound(const struct in6_addr& prefix,
                           int prefix_len,
                           const struct in6_addr& router) {
  if (prefix_len == 64) {
    char buf[64];
    LOG(INFO) << "Found IPv6 network "
              << inet_ntop(AF_INET6, &prefix, buf, sizeof(buf))
              << "/" << prefix_len
              << " route "
              << inet_ntop(AF_INET6, &router, buf, sizeof(buf));

    memcpy(&random_address_, &prefix, sizeof(random_address_));
    random_address_prefix_len_ = prefix_len;
    random_address_tries_ = 0;

    ArcIpConfig::GenerateRandom(&random_address_,
                                random_address_prefix_len_);

    neighbor_finder_.reset(new NeighborFinder());
    neighbor_finder_->Check(lan_ifname_, random_address_,
                            base::Bind(&Manager::OnNeighborCheckResult,
                                       weak_factory_.GetWeakPtr()));
  } else {
    LOG(INFO) << "No IPv6 connectivity available";
  }
}

void Manager::OnNeighborCheckResult(bool found) {
  if (found) {
    if (++random_address_tries_ >= kMaxRandomAddressTries) {
      LOG(WARNING) << "Too many IP collisions, giving up.";
      return;
    }

    LOG(INFO) << "Detected IP collision, retrying with a new address";
    ArcIpConfig::GenerateRandom(&random_address_,
                                random_address_prefix_len_);
    neighbor_finder_->Check(lan_ifname_, random_address_,
                            base::Bind(&Manager::OnNeighborCheckResult,
                                       weak_factory_.GetWeakPtr()));
  } else {
    struct in6_addr router;

    if (!ArcIpConfig::GetV6Address(int_ifname_, &router)) {
      LOG(ERROR) << "Error reading link local address for "
                 << int_ifname_;
      return;
    }

    char buf[64];
    LOG(INFO) << "Setting IPv6 address "
              << inet_ntop(AF_INET6, &random_address_, buf, sizeof(buf))
              << "/128 route "
              << inet_ntop(AF_INET6, &router, buf, sizeof(buf));
    arc_ip_config_->Set(random_address_, 128, router, lan_ifname_);
  }
}

}  // namespace arc_networkd
