// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "patchpanel/multicast_forwarder.h"
#include "patchpanel/net_util.h"

namespace patchpanel {

constexpr const struct in_addr kLanIp = {.s_addr = Ipv4Addr(192, 168, 1, 1)};
constexpr const struct in_addr kGuestIp = {.s_addr = Ipv4Addr(100, 115, 92, 2)};

namespace {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  // Turn off logging.
  logging::SetMinLogLevel(logging::LOGGING_FATAL);

  // Copy the input data so that TranslateMdnsIp can mutate it.
  char* payload = new char[size];
  memcpy(payload, data, size);

  MulticastForwarder::TranslateMdnsIp(kLanIp, kGuestIp, payload, size);

  delete[] payload;
  return 0;
}

}  // namespace
}  // namespace patchpanel
