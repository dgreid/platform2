// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_NETWORK_CLIENT_H_
#define ARC_NETWORK_CLIENT_H_

#include <memory>
#include <utility>
#include <vector>

#include <brillo/brillo_export.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>
#include <patchpanel/proto_bindings/patchpanel_service.pb.h>

namespace patchpanel {

class BRILLO_EXPORT Client {
 public:
  static std::unique_ptr<Client> New();

  Client(scoped_refptr<dbus::Bus> bus, dbus::ObjectProxy* proxy)
      : bus_(std::move(bus)), proxy_(proxy) {}
  ~Client() = default;

  bool NotifyArcStartup(pid_t pid);
  bool NotifyArcShutdown();

  std::vector<patchpanel::Device> NotifyArcVmStartup(uint32_t cid);
  bool NotifyArcVmShutdown(uint32_t cid);

  bool NotifyTerminaVmStartup(uint32_t cid,
                              patchpanel::Device* device,
                              patchpanel::IPv4Subnet* container_subnet);
  bool NotifyTerminaVmShutdown(uint32_t cid);

 private:
  scoped_refptr<dbus::Bus> bus_;
  dbus::ObjectProxy* proxy_ = nullptr;  // owned by bus_

  DISALLOW_COPY_AND_ASSIGN(Client);
};

}  // namespace patchpanel

#endif  // ARC_NETWORK_CLIENT_H_
