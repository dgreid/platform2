// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/logging.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <stddef.h>
#include <stdint.h>

#include "vpn-manager/service_manager.h"

namespace vpn_manager {

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

namespace {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  FuzzedDataProvider data_provider(data, size);
  struct sockaddr_storage addr;
  const int max_ipstring_len =
      40;  // ipv6 is 4 hex * 8 groups, 7 colons = 39 + 1 random byte so that
           // it's sometimes beyond max size
  bool converted;
  std::string addr_string;

  converted = ServiceManager::ConvertIPStringToSockAddr(
      data_provider.ConsumeRandomLengthString(max_ipstring_len), &addr);

  if (converted)
    ServiceManager::ConvertSockAddrToIPString(addr, &addr_string);

  return 0;
}
}  // namespace
}  // namespace vpn_manager
