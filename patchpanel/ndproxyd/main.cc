// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <unistd.h>

#include <base/bind.h>
#include <base/command_line.h>
#include <base/files/scoped_file.h>
#include <base/macros.h>
#include <brillo/daemons/daemon.h>

#include "patchpanel/datapath.h"
#include "patchpanel/firewall.h"
#include "patchpanel/minijailed_process_runner.h"
#include "patchpanel/ndproxy.h"

void OnSocketReadReady(patchpanel::NDProxy* proxy, int fd) {
  proxy->ReadAndProcessOneFrame(fd);
}

void OnGuestIpDiscovery(patchpanel::Datapath* datapath,
                        const std::string& ifname,
                        const std::string& ip6addr) {
  if (!datapath->AddIPv6HostRoute(ifname, ip6addr, 128)) {
    LOG(WARNING) << "Failed to setup the IPv6 route for interface " << ifname;
  }
}

// Stand-alone daemon to proxy ND frames between a pair of interfaces
// Usage: ndproxyd $physical_ifname $guest_ifname
int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  base::CommandLine::StringVector args = cl->GetArgs();
  if (args.size() < 2) {
    LOG(ERROR) << "Missing command line arguments; exiting";
    return EXIT_FAILURE;
  }

  brillo::Daemon daemon;

  patchpanel::MinijailedProcessRunner runner;
  patchpanel::Firewall firewall;
  char accept_ra_sysctl_cmd[40] = {0};
  snprintf(accept_ra_sysctl_cmd, sizeof(accept_ra_sysctl_cmd),
           "net.ipv6.conf.%s.accept_ra", args[0].c_str());
  if (runner.sysctl_w(accept_ra_sysctl_cmd, "2") != 0) {
    LOG(ERROR) << "Failed to enable " << accept_ra_sysctl_cmd << ".";
    return EXIT_FAILURE;
  }
  if (runner.sysctl_w("net.ipv6.conf.all.forwarding", "1") != 0) {
    LOG(ERROR) << "Failed to enable net.ipv6.conf.all.forwarding.";
    return EXIT_FAILURE;
  }
  patchpanel::Datapath datapath(&runner, &firewall);

  patchpanel::NDProxy proxy;
  if (!proxy.Init()) {
    PLOG(ERROR) << "Failed to initialize NDProxy internal state";
    return EXIT_FAILURE;
  }

  // Crostini depends on another daemon (LXD) creating the guest bridge
  // interface. This can take a few seconds, so retry if necessary.
  bool added_interfaces = false;
  for (int i = 0; i < 10; i++) {
    if (proxy.AddInterfacePair(args[0], args[1])) {
      added_interfaces = true;
      break;
    }
    usleep(1000 * 1000 /* 1 second */);
  }
  if (!added_interfaces) {
    LOG(ERROR) << "Network interfaces " << args[0] << " and " << args[1]
               << " could not be added; do they exist?";
    return EXIT_FAILURE;
  }

  proxy.RegisterOnGuestIpDiscoveryHandler(
      base::Bind(&OnGuestIpDiscovery, &datapath));

  base::ScopedFD fd = patchpanel::NDProxy::PreparePacketSocket();
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to initialize data socket";
    return EXIT_FAILURE;
  }

  std::unique_ptr<base::FileDescriptorWatcher::Controller> watcher =
      base::FileDescriptorWatcher::WatchReadable(
          fd.get(), base::Bind(&OnSocketReadReady, &proxy, fd.get()));

  return daemon.Run();
}
