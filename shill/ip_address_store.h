// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_IP_ADDRESS_STORE_H_
#define SHILL_IP_ADDRESS_STORE_H_

#include <set>

#include "shill/net/ip_address.h"

namespace shill {

struct IPAddressLTIgnorePrefix {
  bool operator()(const IPAddress& lhs, const IPAddress& rhs) const;
};

// Stores a set of IP addresses used by ConnectionHealthChecker to check
// connectivity when there is a chance that the service has run out-of-credits.
// The IP addresses are populated (using DNS queries) opportunistically and
// must be persistent so that they can be used in an out-of-credit scenario
// (when DNS queries would also fail).
// To make the store persistent across Device resets (e.g. suspend-resume), it
// is owned by the Manager.
// Currently, this is a thin wrapper around an STL container.
class IPAddressStore {
 public:
  using IPAddresses = std::set<IPAddress, IPAddressLTIgnorePrefix>;

  IPAddressStore();
  IPAddressStore(const IPAddressStore&) = delete;
  IPAddressStore& operator=(const IPAddressStore&) = delete;

  ~IPAddressStore();

  // Add a new IP address if it does not already exist.
  void AddUnique(const IPAddress& ip);
  void Remove(const IPAddress& ip);
  void Clear();
  bool Contains(const IPAddress& ip) const;
  size_t Count() const;
  bool Empty() const;
  const IPAddresses& GetIPAddresses() const { return ip_addresses_; }

 protected:
  friend class IPAddressStoreTest;

 private:
  IPAddresses ip_addresses_;
};

}  // namespace shill

#endif  // SHILL_IP_ADDRESS_STORE_H_
