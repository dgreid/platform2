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

  void AddAltMode(int index, uint16_t svid, uint32_t vdo);

  // Return a pointer to the |AltMode| struct, provided an |index|. It is
  // alright to return a raw pointer here, since the |AltMode| can be considered
  // present for the lifetime of the partner.
  AltMode* GetAltMode(int index);

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

  // The key represents the mode index reported by the Type C connector class.
  std::map<int, std::unique_ptr<AltMode>> alt_modes_;

  // PD Identity Data objects; expected to be read from the partner sysfs.
  uint32_t id_header_vdo_;
  uint32_t cert_stat_vdo_;
  uint32_t product_vdo_;
  // Sysfs path used to access partner PD information.
  base::FilePath syspath_;
};

}  // namespace typecd

#endif  // TYPECD_PARTNER_H_
