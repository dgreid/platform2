// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DBUS_MM1_PROXY_H_
#define SHILL_DBUS_MM1_PROXY_H_

#include <memory>
#include <string>
#include <vector>

#include "cellular/dbus-proxies.h"
#include "shill/cellular/mm1_proxy_interface.h"
#include "shill/key_value_store.h"

namespace shill {
namespace mm1 {

// A proxy to org.freedesktop.ModemManager1
class Mm1Proxy : public Mm1ProxyInterface {
 public:
  // Constructs a org.freedesktop.ModemManager1 DBus object
  // proxy at |path| owned by |service|.
  Mm1Proxy(const scoped_refptr<dbus::Bus>& bus, const std::string& service);
  ~Mm1Proxy() override;
  Mm1Proxy(const Mm1Proxy&) = delete;
  Mm1Proxy& operator=(const Mm1Proxy&) = delete;

  // Inherited methods from Mm1ProxyInterface.
  void ScanDevices(const ResultCallback& callback) override;
  void SetLogging(const std::string& level,
                  const ResultCallback& callback) override;
  void InhibitDevice(const std::string& uid,
                     bool inhibit,
                     const ResultCallback& callback) override;

  // Callbacks for various async calls that uses ResultCallback.
  void OnOperationSuccess(const ResultCallback& callback,
                          const std::string& operation);
  void OnOperationFailure(const ResultCallback& callback,
                          const std::string& operation,
                          brillo::Error* dbus_error);

  std::unique_ptr<org::freedesktop::ModemManager1Proxy> proxy_;

  base::WeakPtrFactory<Mm1Proxy> weak_factory_{this};
};

}  // namespace mm1
}  // namespace shill

#endif  // SHILL_DBUS_MM1_PROXY_H_
