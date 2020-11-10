// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WEBSERVER_WEBSERVD_PERMISSION_BROKER_FIREWALL_H_
#define WEBSERVER_WEBSERVD_PERMISSION_BROKER_FIREWALL_H_

#include "webservd/firewall_interface.h"

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/memory/weak_ptr.h>

#include "permission_broker/dbus-proxies.h"

namespace webservd {

class PermissionBrokerFirewall : public FirewallInterface {
 public:
  PermissionBrokerFirewall();
  PermissionBrokerFirewall(const PermissionBrokerFirewall&) = delete;
  PermissionBrokerFirewall& operator=(const PermissionBrokerFirewall&) = delete;

  ~PermissionBrokerFirewall() override;

  // Interface overrides.
  void WaitForServiceAsync(scoped_refptr<dbus::Bus> bus,
                           const base::Closure& callback) override;
  void PunchTcpHoleAsync(
      uint16_t port,
      const std::string& interface_name,
      const base::Callback<void(bool)>& success_cb,
      const base::Callback<void(brillo::Error*)>& failure_cb) override;

 private:
  // Callbacks to register to see when permission_broker starts or
  // restarts.
  void OnPermissionBrokerAvailable(bool available);
  void OnPermissionBrokerNameOwnerChanged(const std::string& old_owner,
                                          const std::string& new_owner);

  std::unique_ptr<org::chromium::PermissionBrokerProxy> proxy_;

  // Callback to use when firewall service starts or restarts.
  base::Closure service_started_cb_;

  // File descriptors for the two ends of the pipe used for communicating with
  // remote firewall server (permission_broker), where the remote firewall
  // server will use the read end of the pipe to detect when this process exits.
  int lifeline_read_fd_{-1};
  int lifeline_write_fd_{-1};

  base::WeakPtrFactory<PermissionBrokerFirewall> weak_ptr_factory_{this};
};

}  // namespace webservd

#endif  // WEBSERVER_WEBSERVD_PERMISSION_BROKER_FIREWALL_H_
