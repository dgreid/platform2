// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_DNS_CLIENT_FACTORY_H_
#define SHILL_MOCK_DNS_CLIENT_FACTORY_H_

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/no_destructor.h>
#include <gmock/gmock.h>

#include "shill/dns_client_factory.h"
#include "shill/event_dispatcher.h"
#include "shill/net/ip_address.h"

namespace shill {

class MockDnsClientFactory : public DnsClientFactory {
 public:
  ~MockDnsClientFactory() override;

  // This is a singleton. Use MockDnsClientFactory::GetInstance()->Foo().
  static MockDnsClientFactory* GetInstance();

  MOCK_METHOD(std::unique_ptr<DnsClient>,
              CreateDnsClient,
              (IPAddress::Family,
               const std::string&,
               const std::vector<std::string>&,
               int,
               EventDispatcher*,
               const DnsClient::ClientCallback&),
              (override));

 protected:
  MockDnsClientFactory();
  MockDnsClientFactory(const MockDnsClientFactory&) = delete;
  MockDnsClientFactory& operator=(const MockDnsClientFactory&) = delete;

 private:
  friend class base::NoDestructor<MockDnsClientFactory>;
};

}  // namespace shill

#endif  // SHILL_MOCK_DNS_CLIENT_FACTORY_H_
