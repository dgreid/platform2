// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_PERIPHERAL_H_
#define TYPECD_PERIPHERAL_H_

#include <map>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>

#include "typecd/alt_mode.h"

namespace typecd {

// This is a base class which can represent the components connected to a Type C
// Port. These components (Partner and Cable) have common properties like PD
// identity, so it is worthwhile to abstract those into a common base class
// which they can then derive from.
class Peripheral {
 public:
  explicit Peripheral(const base::FilePath& syspath);

  // Setters and Getters for PD identity information.
  void SetIdHeaderVDO(uint32_t id_header_vdo) {
    id_header_vdo_ = id_header_vdo;
  }
  void SetCertStatVDO(uint32_t cert_stat_vdo) {
    cert_stat_vdo_ = cert_stat_vdo;
  }
  void SetProductVDO(uint32_t product_vdo) { product_vdo_ = product_vdo; }

  uint32_t GetIdHeaderVDO() { return id_header_vdo_; }
  uint32_t GetCertStateVDO() { return cert_stat_vdo_; }
  uint32_t GetProductVDO() { return product_vdo_; }

 private:
  friend class PartnerTest;
  FRIEND_TEST(PartnerTest, TestAltModeManualAddition);
  FRIEND_TEST(PartnerTest, TestPDIdentityScan);

  // Get the PD Identity VDOs from sysfs. This function should be called during
  // Peripheral creation. We mark this as void, as Peripheral registration
  // should not fail if we are unable to grab the VDOs.
  //
  // This is also an ideal location to report PD identity values to metrics,
  // since these values are not expected to change for the duration of the
  // peripheral lifetime.
  //
  // TODO(b/152251292): When is the right time to report PD identity metrics?
  // There is some raciness, whereby the EC might not have parsed peripheral PD
  // identity information yet. So, the values may still be 0, and will only be
  // filled in when the EC has obtained this info and made it accessible via a
  // host command.
  //
  // OTOH, some devices don't have any PD identity info, so they will always
  // report 0.
  //
  // Should we:
  // - Only register peripherals in the kernel when the PD identity information
  // is valid? <or>
  // - Update the PD Identity information after peripheral creation, and only
  // report them when we have some confirmed heuristic (when we've received
  // number of peripheral alternate modes supported? by then the PD contract has
  // been established).
  void UpdatePDIdentityVDOs();

  // PD Identity Data objects; expected to be read from the peripheral sysfs.
  uint32_t id_header_vdo_;
  uint32_t cert_stat_vdo_;
  uint32_t product_vdo_;
  // Sysfs path used to access peripheral PD information.
  base::FilePath syspath_;

  DISALLOW_COPY_AND_ASSIGN(Peripheral);
};

}  // namespace typecd

#endif  // TYPECD_PERIPHERAL_H_
