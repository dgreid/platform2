// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_DNS_CLIENT_FACTORY_H_
#define SHILL_DNS_CLIENT_FACTORY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/no_destructor.h>

#include "shill/dns_client.h"
#include "shill/event_dispatcher.h"
#include "shill/net/ip_address.h"

namespace shill {

class DnsClientFactory {
 public:
  virtual ~DnsClientFactory();

  // This is a singleton. Use DnsClientFactory::GetInstance()->Foo().
  static DnsClientFactory* GetInstance();

  virtual std::unique_ptr<DnsClient> CreateDnsClient(
      IPAddress::Family family,
      const std::string& interface_name,
      const std::vector<std::string>& dns_servers,
      int timeout_ms,
      EventDispatcher* dispatcher,
      const DnsClient::ClientCallback& callback);

 protected:
  DnsClientFactory();
  DnsClientFactory(const DnsClientFactory&) = delete;
  DnsClientFactory& operator=(const DnsClientFactory&) = delete;

 private:
  friend class base::NoDestructor<DnsClientFactory>;
};

}  // namespace shill

#endif  // SHILL_DNS_CLIENT_FACTORY_H_
