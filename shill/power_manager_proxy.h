// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_POWER_MANAGER_PROXY_H_
#define SHILL_POWER_MANAGER_PROXY_H_

// An implementation of PowerManagerProxyInterface.  It connects to the dbus and
// listens for events from the power manager.  When they occur, the delegate's
// member functions are called.

// Do not instantiate this class directly.  use
// ProxyFactory::CreatePowerManagerProxy instead.

#include <stdint.h>

#include <string>
#include <vector>

#include <base/compiler_specific.h>

#include "shill/dbus_proxies/power_manager.h"
#include "shill/power_manager_proxy_interface.h"
#include "shill/proxy_factory.h"

namespace shill {

class PowerManagerProxy : public PowerManagerProxyInterface {
 public:
  ~PowerManagerProxy() override;

  // Inherited from PowerManagerProxyInterface.
  bool RegisterSuspendDelay(base::TimeDelta timeout,
                            const std::string &description,
                            int *delay_id_out) override;
  bool UnregisterSuspendDelay(int delay_id) override;
  bool ReportSuspendReadiness(int delay_id, int suspend_id) override;
  bool RegisterDarkSuspendDelay(base::TimeDelta timeout,
                                const std::string &description,
                                int *delay_id_out) override;
  bool UnregisterDarkSuspendDelay(int delay_id) override;
  bool ReportDarkSuspendReadiness(int delay_id, int suspend_id) override;
  bool RecordDarkResumeWakeReason(const std::string &wake_reason) override;

 private:
  // Only this factory method can create a PowerManagerProxy.
  friend PowerManagerProxyInterface *ProxyFactory::CreatePowerManagerProxy(
      PowerManagerProxyDelegate *delegate);

  class Proxy : public org::chromium::PowerManager_proxy,
                public DBus::ObjectProxy {
   public:
    Proxy(PowerManagerProxyDelegate *delegate,
          DBus::Connection *connection);
    ~Proxy() override;

   private:
    // Signal callbacks inherited from org::chromium::PowerManager_proxy.
    void SuspendImminent(const std::vector<uint8_t> &serialized_proto) override;
    void SuspendDone(const std::vector<uint8_t> &serialized_proto) override;
    void DarkSuspendImminent(
        const std::vector<uint8_t> &serialized_proto) override;

    PowerManagerProxyDelegate *const delegate_;

    DISALLOW_COPY_AND_ASSIGN(Proxy);
  };

  // Constructs a PowerManager DBus object proxy with signals dispatched to
  // |delegate|.
  PowerManagerProxy(PowerManagerProxyDelegate *delegate,
                    DBus::Connection *connection);

  bool RegisterSuspendDelayInternal(bool is_dark,
                                    base::TimeDelta timeout,
                                    const std::string &description,
                                    int *delay_id_out);
  bool UnregisterSuspendDelayInternal(bool is_dark, int delay_id);
  bool ReportSuspendReadinessInternal(bool is_dark,
                                      int delay_id,
                                      int suspend_id);

  Proxy proxy_;

  DISALLOW_COPY_AND_ASSIGN(PowerManagerProxy);
};

}  // namespace shill

#endif  // SHILL_POWER_MANAGER_PROXY_H_
