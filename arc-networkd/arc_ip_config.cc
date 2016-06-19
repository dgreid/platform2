// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc-networkd/arc_ip_config.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <linux/capability.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <random>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <brillo/minijail/minijail.h>
#include <brillo/process.h>

namespace {

const char kRoutingTableNames[] =
    "/opt/google/containers/android/rootfs/android-data"
    "/data/misc/net/rt_tables";
const int kInvalidTableId = -1;

// These match what is used in iptables.cc in firewalld.
const char kIpPath[] = "/bin/ip";
const char kIp6TablesPath[] = "/sbin/ip6tables";
const char kUnprivilegedUser[] = "nobody";
const uint64_t kIpTablesCapMask =
    CAP_TO_MASK(CAP_NET_ADMIN) | CAP_TO_MASK(CAP_NET_RAW);

}  // namespace

namespace arc_networkd {

ArcIpConfig::ArcIpConfig(const std::string& int_ifname,
                         const std::string& con_ifname,
                         pid_t con_netns) :
    int_ifname_(int_ifname),
    con_ifname_(con_ifname),
    con_netns_(con_netns),
    routing_table_id_(kInvalidTableId) {}

ArcIpConfig::~ArcIpConfig() {
  Clear();
}

int ArcIpConfig::ReadTableId(const std::string& table_name) {
  base::ScopedFILE f(fopen(kRoutingTableNames, "r"));
  if (!f.get()) {
    LOG(ERROR) << "Could not open " << kRoutingTableNames;
    return kInvalidTableId;
  }

  while (1) {
    char buf[64];

    if (fgets(buf, sizeof(buf), f.get()) == NULL)
      return kInvalidTableId;

    char* saveptr;
    if (!strtok_r(buf, " ", &saveptr))
      continue;

    int table_id = atoi(buf);

    char* p = strtok_r(NULL, " ", &saveptr);
    if (!p)
      continue;

    char* newline = strchr(p, '\n');
    if (newline)
      *newline = 0;

    if (table_name == p)
      return table_id;
  }
}

bool ArcIpConfig::Init() {
  if (!con_netns_)
    return true;

  std::string filename =
      base::StringPrintf("/proc/%d/ns/net", static_cast<int>(con_netns_));
  con_netns_fd_.reset(open(filename.c_str(), O_RDONLY));
  if (!con_netns_fd_.is_valid()) {
    LOG(ERROR) << "Could not open " << filename;
    return false;
  }

  self_netns_fd_.reset(open("/proc/self/ns/net", O_RDONLY));
  if (!self_netns_fd_.is_valid()) {
    LOG(ERROR) << "Could not open host netns";
    return false;
  }

  return true;
}

bool ArcIpConfig::ContainerInit() {
  if (!con_netns_)
    return true;

  PCHECK(setns(con_netns_fd_.get(), CLONE_NEWNET) == 0);
  base::ScopedFD fd(socket(AF_INET, SOCK_DGRAM, IPPROTO_IP));
  PCHECK(setns(self_netns_fd_.get(), CLONE_NEWNET) == 0);

  if (!fd.is_valid()) {
    LOG(ERROR) << "socket() failed";
    return false;
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, con_ifname_.c_str(), IFNAMSIZ);

  if (ioctl(fd.get(), SIOCGIFFLAGS, &ifr) < 0) {
    LOG(ERROR) << "SIOCGIFADDR failed";
    return false;
  }

  if (!(ifr.ifr_flags & IFF_UP))
    return false;

  routing_table_id_ = ReadTableId(con_ifname_);
  if (routing_table_id_ == kInvalidTableId) {
    LOG(ERROR) << "Could not look up routing table ID in "
               << kRoutingTableNames;
    return false;
  }

  return true;
}

// static
void ArcIpConfig::GenerateRandom(struct in6_addr* prefix,
                                 int prefix_len) {
  std::mt19937 rng;
  rng.seed(std::random_device()());
  std::uniform_int_distribution<std::mt19937::result_type> randbyte(0, 255);

  // TODO(cernekee): handle different prefix lengths
  CHECK_EQ(prefix_len, 64);

  for (int i = 8; i < 16; i++)
    prefix->s6_addr[i] = randbyte(rng);

  // Set the universal/local flag, similar to a RFC 4941 address.
  prefix->s6_addr[8] |= 0x40;
}

// static
bool ArcIpConfig::GetV6Address(const std::string& ifname,
                               struct in6_addr* address) {
  struct ifaddrs* ifap;
  struct ifaddrs* p;
  bool found = false;

  // Iterate through the linked list of all interface addresses to find
  // the first IPv6 address for |ifname|.
  if (getifaddrs(&ifap) < 0)
    return false;

  for (p = ifap; p; p = p->ifa_next) {
    if (p->ifa_name != ifname ||
        p->ifa_addr->sa_family != AF_INET6) {
      continue;
    }

    if (address) {
      struct sockaddr_in6* sa =
          reinterpret_cast<struct sockaddr_in6*>(p->ifa_addr);
      memcpy(address, &sa->sin6_addr, sizeof(*address));
    }
    found = true;
    break;
  }

  freeifaddrs(ifap);
  return found;
}

// Runs |argv| (a program name + argument list) with reduced privileges.
// Returns WEXITSTATUS on success, or -1 if the program could not be executed.
int ArcIpConfig::StartProcessInMinijail(const std::vector<std::string>& argv) {
  brillo::Minijail* m = brillo::Minijail::GetInstance();
  minijail* jail = m->New();

  m->DropRoot(jail, kUnprivilegedUser, kUnprivilegedUser);
  m->UseCapabilities(jail, kIpTablesCapMask);

  std::vector<char*> args;
  for (const auto& arg : argv) {
    args.push_back(const_cast<char*>(arg.c_str()));
  }
  args.push_back(nullptr);

  int status;
  bool ran = m->RunSyncAndDestroy(jail, args, &status);

  if (!ran)
    LOG(ERROR) << "Could not execute " << args.front();
  else if (status != 0)
    LOG(WARNING) << "Subprocess " << args.front() << " returned " << status;

  return ran ? status : -1;
}

bool ArcIpConfig::Set(const struct in6_addr& address,
                      int prefix_len,
                      const struct in6_addr& router_addr,
                      const std::string& lan_ifname) {
  Clear();

  if (!con_netns_fd_.is_valid() ||
      !self_netns_fd_.is_valid() ||
      routing_table_id_ == kInvalidTableId) {
    LOG(ERROR) << "Cannot set IPv6 address: no netns configured";
    return false;
  }

  char buf[INET6_ADDRSTRLEN];

  CHECK(inet_ntop(AF_INET6, &address, buf, sizeof(buf)));
  current_address_ = buf;
  current_address_full_ = current_address_;
  current_address_full_.append("/" + std::to_string(prefix_len));

  CHECK(inet_ntop(AF_INET6, &router_addr, buf, sizeof(buf)));
  current_router_ = buf;

  current_lan_ifname_ = lan_ifname;

  PCHECK(setns(con_netns_fd_.get(), CLONE_NEWNET) == 0);

  // These can fail if the interface disappears (e.g. hot-unplug).

  StartProcessInMinijail({
      kIpPath, "-6",
      "addr", "add", current_address_full_,
      "dev", con_ifname_
  });

  StartProcessInMinijail({
      kIpPath, "-6",
      "route", "add", current_router_,
      "dev", con_ifname_,
      "table", std::to_string(routing_table_id_)
  });

  StartProcessInMinijail({
      kIpPath, "-6",
      "route", "add", "default",
      "via", current_router_,
      "dev", con_ifname_,
      "table", std::to_string(routing_table_id_)
  });

  PCHECK(setns(self_netns_fd_.get(), CLONE_NEWNET) == 0);

  StartProcessInMinijail({
      kIpPath, "-6",
      "route", "add", current_address_full_,
      "dev", int_ifname_
  });

  StartProcessInMinijail({
      kIpPath, "-6",
      "neigh", "add", "proxy", current_address_,
      "dev", current_lan_ifname_
  });

  // These should never fail.

  CHECK_EQ(StartProcessInMinijail({
      kIp6TablesPath,
      "-A", "FORWARD",
      "-i", current_lan_ifname_,
      "-o", int_ifname_,
      "-j", "ACCEPT",
      "-w"
  }), 0);

  CHECK_EQ(StartProcessInMinijail({
      kIp6TablesPath,
      "-A", "FORWARD",
      "-i", int_ifname_,
      "-o", current_lan_ifname_,
      "-j", "ACCEPT",
      "-w"
  }), 0);

  is_configured_ = true;
  return true;
}

bool ArcIpConfig::Clear() {
  if (!is_configured_)
    return true;

  // These should never fail.

  CHECK_EQ(StartProcessInMinijail({
      kIp6TablesPath,
      "-D", "FORWARD",
      "-i", int_ifname_,
      "-o", current_lan_ifname_,
      "-j", "ACCEPT",
      "-w"
  }), 0);

  CHECK_EQ(StartProcessInMinijail({
      kIp6TablesPath,
      "-D", "FORWARD",
      "-i", current_lan_ifname_,
      "-o", int_ifname_,
      "-j", "ACCEPT",
      "-w"
  }), 0);

  // These can fail if the interface disappears (e.g. hot-unplug).

  StartProcessInMinijail({
      kIpPath, "-6",
      "neigh", "del", "proxy", current_address_,
      "dev", current_lan_ifname_
  });

  StartProcessInMinijail({
      kIpPath, "-6",
      "route", "del", current_address_full_,
      "dev", int_ifname_
  });

  PCHECK(setns(con_netns_fd_.get(), CLONE_NEWNET) == 0);

  StartProcessInMinijail({
      kIpPath, "-6",
      "route", "del", "default",
      "via", current_router_,
      "dev", con_ifname_,
      "table", std::to_string(routing_table_id_)
  });

  StartProcessInMinijail({
      kIpPath, "-6",
      "route", "del", current_router_,
      "dev", con_ifname_,
      "table", std::to_string(routing_table_id_)
  });

  StartProcessInMinijail({
      kIpPath, "-6",
      "addr", "del", current_address_full_,
      "dev", con_ifname_
  });

  PCHECK(setns(self_netns_fd_.get(), CLONE_NEWNET) == 0);

  is_configured_ = false;
  return true;
}

}  // namespace arc_networkd
