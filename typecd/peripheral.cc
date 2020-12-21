// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/peripheral.h"

#include <base/logging.h>

#include "typecd/utils.h"

namespace typecd {

Peripheral::Peripheral(const base::FilePath& syspath)
    : id_header_vdo_(0), cert_stat_vdo_(0), product_vdo_(0), syspath_(syspath) {
  UpdatePDIdentityVDOs();
}

void Peripheral::UpdatePDIdentityVDOs() {
  // If the Product VDO is non-zero, we can be assured that it's been parsed
  // already, so we can avoid parsing it again.
  if (GetProductVDO() != 0) {
    LOG(INFO)
        << "PD identity VDOs already registered, skipping re-registration.";
    return;
  }
  // Create the various sysfs file paths for PD Identity.
  auto cert_stat = syspath_.Append("identity").Append("cert_stat");
  auto product = syspath_.Append("identity").Append("product");
  auto id_header = syspath_.Append("identity").Append("id_header");
  auto product_type1 = syspath_.Append("identity").Append("product_type_vdo1");
  auto product_type2 = syspath_.Append("identity").Append("product_type_vdo2");
  auto product_type3 = syspath_.Append("identity").Append("product_type_vdo3");

  uint32_t product_vdo;
  uint32_t cert_stat_vdo;
  uint32_t id_header_vdo;
  uint32_t product_type_vdo1;
  uint32_t product_type_vdo2;
  uint32_t product_type_vdo3;

  if (!ReadHexFromPath(product, &product_vdo))
    return;
  LOG(INFO) << "Peripheral Product VDO: " << std::hex << product_vdo;

  if (!ReadHexFromPath(cert_stat, &cert_stat_vdo))
    return;
  LOG(INFO) << "Peripheral Cert stat VDO: " << std::hex << cert_stat_vdo;

  if (!ReadHexFromPath(id_header, &id_header_vdo))
    return;
  LOG(INFO) << "Peripheral Id Header VDO: " << std::hex << id_header_vdo;

  if (!ReadHexFromPath(product_type1, &product_type_vdo1))
    return;
  LOG(INFO) << "Peripheral Product Type VDO 1: " << std::hex
            << product_type_vdo1;

  if (!ReadHexFromPath(product_type2, &product_type_vdo2))
    return;
  LOG(INFO) << "Peripheral Product Type VDO 2: " << std::hex
            << product_type_vdo2;

  if (!ReadHexFromPath(product_type3, &product_type_vdo3))
    return;
  LOG(INFO) << "Peripheral Product Type VDO 3: " << std::hex
            << product_type_vdo3;

  SetIdHeaderVDO(id_header_vdo);
  SetProductVDO(product_vdo);
  SetCertStatVDO(cert_stat_vdo);
  SetProductTypeVDO1(product_type_vdo1);
  SetProductTypeVDO2(product_type_vdo2);
  SetProductTypeVDO3(product_type_vdo3);
}

}  // namespace typecd
