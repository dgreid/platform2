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

constexpr uint8_t kPDRevision30 = 0x3;
constexpr uint8_t kPDRevision20 = 0x2;

// This is a base class which can represent the components connected to a Type C
// Port. These components (Partner and Cable) have common properties like PD
// identity, so it is worthwhile to abstract those into a common base class
// which they can then derive from.
class Peripheral {
 public:
  explicit Peripheral(const base::FilePath& syspath);
  Peripheral(const Peripheral&) = delete;
  Peripheral& operator=(const Peripheral&) = delete;

  // Setters and Getters for PD identity information.
  void SetIdHeaderVDO(uint32_t id_header_vdo) {
    id_header_vdo_ = id_header_vdo;
  }
  void SetCertStatVDO(uint32_t cert_stat_vdo) {
    cert_stat_vdo_ = cert_stat_vdo;
  }
  void SetProductVDO(uint32_t product_vdo) { product_vdo_ = product_vdo; }

  void SetProductTypeVDO1(uint32_t product_type_vdo) {
    product_type_vdo1_ = product_type_vdo;
  }
  void SetProductTypeVDO2(uint32_t product_type_vdo) {
    product_type_vdo2_ = product_type_vdo;
  }
  void SetProductTypeVDO3(uint32_t product_type_vdo) {
    product_type_vdo3_ = product_type_vdo;
  }
  void SetPDRevision(uint8_t pd_revision) { pd_revision_ = pd_revision; }

  uint32_t GetIdHeaderVDO() { return id_header_vdo_; }
  uint32_t GetCertStateVDO() { return cert_stat_vdo_; }
  uint32_t GetProductVDO() { return product_vdo_; }

  uint32_t GetProductTypeVDO1() { return product_type_vdo1_; }
  uint32_t GetProductTypeVDO2() { return product_type_vdo2_; }
  uint32_t GetProductTypeVDO3() { return product_type_vdo3_; }
  uint8_t GetPDRevision() { return pd_revision_; }

 protected:
  base::FilePath GetSysPath() { return syspath_; }

  // Get the PD Identity VDOs from sysfs. This is called during Peripheral
  // creation and other times (e.g "change" udev events). We mark this as void
  // as Peripheral registration should not fail if we are unable to grab the
  // VDOs.
  void UpdatePDIdentityVDOs();

 private:
  friend class PartnerTest;
  FRIEND_TEST(PartnerTest, TestAltModeManualAddition);
  FRIEND_TEST(PartnerTest, TestPDIdentityScan);

  // PD Identity Data objects; expected to be read from the peripheral sysfs.
  uint32_t id_header_vdo_;
  uint32_t cert_stat_vdo_;
  uint32_t product_vdo_;
  uint32_t product_type_vdo1_;
  uint32_t product_type_vdo2_;
  uint32_t product_type_vdo3_;
  uint8_t pd_revision_;
  // Sysfs path used to access peripheral PD information.
  base::FilePath syspath_;
};

}  // namespace typecd

#endif  // TYPECD_PERIPHERAL_H_
