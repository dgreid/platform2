// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_PARTNER_H_
#define TYPECD_PARTNER_H_

#include <map>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>

#include "typecd/alt_mode.h"

namespace typecd {

// A partner represents a device/peripheral which is connected to the host. This
// class is used to maintain the state associated with the partner.
class Partner {
 public:
  explicit Partner(const base::FilePath& syspath);

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

  // Check if a particular alt mode index (as specified by the Type C connector
  // class framework) is registered.
  bool IsAltModePresent(int index);

  bool AddAltMode(const base::FilePath& mode_syspath);
  void RemoveAltMode(const base::FilePath& mode_syspath);

  // Update the AltMode information based on Type C connector class sysfs.
  // A udev event is generated when a new partner altmode is registered; parse
  // the data at the "known" locations in sysfs and populate the class data
  // structures accordingly.
  //
  // Previously added altmodes should be unaffected by this function.
  void UpdateAltModesFromSysfs();

 private:
  friend class PartnerTest;
  FRIEND_TEST(PartnerTest, TestAltModeManualAddition);
  FRIEND_TEST(PartnerTest, TestPDIdentityScan);

  // Get the PD Identity VDOs from sysfs. This function should be called during
  // Partner creation. We mark this as void, as Partner registration should not
  // fail if we are unable to grab the VDOs.
  //
  // This is also an ideal location to report PD identity values to metrics,
  // since these values are not expected to change for the duration of the
  // partner lifetime.
  //
  // TODO(b/152251292): When is the right time to report PD identity metrics?
  // There is some raciness, whereby the EC might not have parsed partner PD
  // identity information yet. So, the values may still be 0, and will only be
  // filled in when the EC has obtained this info and made it accessible via a
  // host command.
  //
  // OTOH, some devices don't have any PD identity info, so they will always
  // report 0.
  //
  // Should we:
  // - Only register partners in the kernel when the PD identity information is
  // valid? <or>
  // - Update the PD Identity information after partner creation, and only
  // report them when
  //   we have some confirmed heuristic (when we've received number of partner
  //   alternate modes supported? by then the PD contract has been established).
  void UpdatePDIdentityVDOs();

  // A map representing all the alternate modes supported by the partner.
  // The key is the index of the alternate mode as determined by the connector
  // class sysfs directories that represent them. For example, and alternate
  // mode which has the directory
  // "/sys/class/typec/port1-partner/port1-partner.0" will use an key of "0".
  std::map<int, std::unique_ptr<AltMode>> alt_modes_;

  // PD Identity Data objects; expected to be read from the partner sysfs.
  uint32_t id_header_vdo_;
  uint32_t cert_stat_vdo_;
  uint32_t product_vdo_;
  // Sysfs path used to access partner PD information.
  base::FilePath syspath_;

  DISALLOW_COPY_AND_ASSIGN(Partner);
};

}  // namespace typecd

#endif  // TYPECD_PARTNER_H_
