// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <net/if.h>

#include <fuzzer/FuzzedDataProvider.h>
#include <set>

#include "base/logging.h"

#include "patchpanel/firewall.h"

using patchpanel::ModifyPortRuleRequest;
using Protocol = patchpanel::ModifyPortRuleRequest::Protocol;

namespace patchpanel {

class FakeFirewall : public Firewall {
 public:
  FakeFirewall() = default;
  ~FakeFirewall() = default;

 private:
  // The fake's implementation always succeeds.
  int RunInMinijail(const std::vector<std::string>& argv) override { return 0; }

  DISALLOW_COPY_AND_ASSIGN(FakeFirewall);
};

}  // namespace patchpanel

struct Environment {
  Environment() { logging::SetMinLogLevel(logging::LOG_FATAL); }
};

void FuzzAcceptRules(patchpanel::FakeFirewall* fake_firewall,
                     const uint8_t* data,
                     size_t size) {
  FuzzedDataProvider data_provider(data, size);
  while (data_provider.remaining_bytes() > 0) {
    ModifyPortRuleRequest::Protocol proto = data_provider.ConsumeBool()
                                                ? ModifyPortRuleRequest::TCP
                                                : ModifyPortRuleRequest::UDP;
    uint16_t port = data_provider.ConsumeIntegral<uint16_t>();
    std::string iface = data_provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
    if (data_provider.ConsumeBool()) {
      fake_firewall->AddAcceptRules(proto, port, iface);
    } else {
      fake_firewall->DeleteAcceptRules(proto, port, iface);
    }
  }
}

void FuzzForwardRules(patchpanel::FakeFirewall* fake_firewall,
                      const uint8_t* data,
                      size_t size) {
  FuzzedDataProvider data_provider(data, size);
  while (data_provider.remaining_bytes() > 0) {
    ModifyPortRuleRequest::Protocol proto = data_provider.ConsumeBool()
                                                ? ModifyPortRuleRequest::TCP
                                                : ModifyPortRuleRequest::UDP;
    uint16_t forwarded_port = data_provider.ConsumeIntegral<uint16_t>();
    uint16_t dst_port = data_provider.ConsumeIntegral<uint16_t>();
    struct in_addr input_ip_addr = {
        .s_addr = data_provider.ConsumeIntegral<uint32_t>()};
    struct in_addr dst_ip_addr = {
        .s_addr = data_provider.ConsumeIntegral<uint32_t>()};
    char input_buffer[INET_ADDRSTRLEN];
    char dst_buffer[INET_ADDRSTRLEN];
    memset(input_buffer, 0, INET_ADDRSTRLEN);
    memset(dst_buffer, 0, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &input_ip_addr, input_buffer, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &dst_ip_addr, dst_buffer, INET_ADDRSTRLEN);
    std::string input_ip = input_buffer;
    std::string dst_ip = dst_buffer;
    std::string iface = data_provider.ConsumeRandomLengthString(IFNAMSIZ - 1);
    if (data_provider.ConsumeBool()) {
      fake_firewall->AddIpv4ForwardRule(proto, input_ip, forwarded_port, iface,
                                        dst_ip, dst_port);
    } else {
      fake_firewall->DeleteIpv4ForwardRule(proto, input_ip, forwarded_port,
                                           iface, dst_ip, dst_port);
    }
  }
}

void FuzzLoopbackLockdownRules(patchpanel::FakeFirewall* fake_firewall,
                               const uint8_t* data,
                               size_t size) {
  FuzzedDataProvider data_provider(data, size);
  while (data_provider.remaining_bytes() > 0) {
    ModifyPortRuleRequest::Protocol proto = data_provider.ConsumeBool()
                                                ? ModifyPortRuleRequest::TCP
                                                : ModifyPortRuleRequest::UDP;
    uint16_t port = data_provider.ConsumeIntegral<uint16_t>();
    if (data_provider.ConsumeBool()) {
      fake_firewall->AddLoopbackLockdownRules(proto, port);
    } else {
      fake_firewall->DeleteLoopbackLockdownRules(proto, port);
    }
  }
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;

  patchpanel::FakeFirewall fake_firewall;

  FuzzAcceptRules(&fake_firewall, data, size);
  FuzzForwardRules(&fake_firewall, data, size);
  FuzzLoopbackLockdownRules(&fake_firewall, data, size);

  return 0;
}
