// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_PROXY_DBUS_SERVICE_H_
#define CRYPTOHOME_PROXY_DBUS_SERVICE_H_

#include <memory>
#include <sysexits.h>

#include <brillo/daemons/dbus_daemon.h>

#include "cryptohome/proxy/dbus_proxy_service.h"

namespace cryptohome {

class CryptohomeProxyDaemon : public brillo::DBusDaemon {
 public:
  CryptohomeProxyDaemon() : DBusDaemon() {}
  CryptohomeProxyDaemon(const CryptohomeProxyDaemon&) = delete;
  CryptohomeProxyDaemon& operator=(const CryptohomeProxyDaemon&) = delete;

 protected:
  int OnInit() override {
    brillo::DBusDaemon::OnInit();
    proxy_service_.reset(new CryptohomeProxyService(bus_));
    proxy_service_->OnInit();
    return EX_OK;
  }

 private:
  std::unique_ptr<CryptohomeProxyService> proxy_service_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_PROXY_DBUS_SERVICE_H_
