// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_ADDRESS_MANAGER_H_
#define PATCHPANEL_ADDRESS_MANAGER_H_

#include <map>
#include <memory>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <brillo/brillo_export.h>

#include "patchpanel/mac_address_generator.h"
#include "patchpanel/subnet.h"
#include "patchpanel/subnet_pool.h"

namespace patchpanel {

// Responsible for address provisioning for guest networks.
class BRILLO_EXPORT AddressManager {
 public:
  enum class Guest {
    // ARC++ or ARCVM management interface.
    ARC,
    // ARC++ or ARCVM virtual networks connected to shill Devices.
    ARC_NET,
    /// Crostini VM root namespace.
    VM_TERMINA,
    // Crostini plugin VMs.
    VM_PLUGIN,
    // Crostini VM user containers.
    CONTAINER,
    // Other network namespaces hosting minijailed host processes.
    MINIJAIL_NETNS,
  };

  AddressManager();
  virtual ~AddressManager() = default;

  // Generates a MAC address guaranteed to be unique for the lifetime of this
  // object.
  // If |index| is provided, a MAC address will be returned that is stable
  // across all invocations and instantions.
  // Virtual for testing only.
  virtual MacAddress GenerateMacAddress(uint8_t index = kAnySubnetIndex);

  // Allocates a subnet from the specified guest network pool if available.
  // Returns nullptr if the guest was configured or no more subnets are
  // available for allocation.
  // |index| is used to acquire a particular subnet from the pool, if supported
  // for |guest|, it is 1-based, so 0 indicates no preference.
  std::unique_ptr<Subnet> AllocateIPv4Subnet(Guest guest,
                                             uint32_t index = kAnySubnetIndex);

 private:
  MacAddressGenerator mac_addrs_;
  std::map<Guest, std::unique_ptr<SubnetPool>> pools_;

  base::WeakPtrFactory<AddressManager> weak_ptr_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(AddressManager);
};

}  // namespace patchpanel

#endif  // PATCHPANEL_ADDRESS_MANAGER_H_
