// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/partner.h"

#include <base/logging.h>
#include <re2/re2.h>

#include "typecd/utils.h"

namespace {

constexpr char kPartnerAltModeRegex[] = R"(port(\d+)-partner.(\d+))";

}

namespace typecd {

Partner::Partner(const base::FilePath& syspath)
    : id_header_vdo_(0), cert_stat_vdo_(0), product_vdo_(0), syspath_(syspath) {
  UpdatePDIdentityVDOs();
}

void Partner::UpdatePDIdentityVDOs() {
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

  uint32_t product_vdo;
  uint32_t cert_stat_vdo;
  uint32_t id_header_vdo;

  if (!ReadHexFromPath(product, &product_vdo))
    return;
  LOG(INFO) << "Partner Product VDO: " << product_vdo;

  if (!ReadHexFromPath(cert_stat, &cert_stat_vdo))
    return;
  LOG(INFO) << "Partner Cert stat VDO: " << cert_stat_vdo;

  if (!ReadHexFromPath(id_header, &id_header_vdo))
    return;
  LOG(INFO) << "Partner Id Header VDO: " << id_header_vdo;

  SetIdHeaderVDO(id_header_vdo);
  SetProductVDO(product_vdo);
  SetCertStatVDO(cert_stat_vdo);
}

bool Partner::AddAltMode(const base::FilePath& mode_syspath) {
  int port, index;
  if (!RE2::FullMatch(mode_syspath.BaseName().value(), kPartnerAltModeRegex,
                      &port, &index)) {
    LOG(ERROR) << "Couldn't parse alt mode index from syspath " << mode_syspath;
    return false;
  }

  if (IsAltModePresent(index)) {
    LOG(ERROR) << "Alt mode already registered for syspath " << mode_syspath;
    return false;
  }

  auto alt_mode = AltMode::CreateAltMode(mode_syspath);
  if (!alt_mode) {
    LOG(ERROR) << "Error creating alt mode for syspath " << mode_syspath;
    return false;
  }

  alt_modes_.emplace(index, std::move(alt_mode));

  LOG(INFO) << "Added alt mode for port " << port << " index " << index;

  return true;
}

void Partner::RemoveAltMode(const base::FilePath& mode_syspath) {
  int port, index;
  if (!RE2::FullMatch(mode_syspath.BaseName().value(), kPartnerAltModeRegex,
                      &port, &index)) {
    LOG(ERROR) << "Couldn't parse alt mode index from syspath " << mode_syspath;
    return;
  }

  auto it = alt_modes_.find(index);
  if (it == alt_modes_.end()) {
    LOG(INFO) << "Trying to delete non-existent alt mode " << index;
    return;
  }

  alt_modes_.erase(it);

  LOG(INFO) << "Removed alt mode for port " << port << " index " << index;
}

bool Partner::IsAltModePresent(int index) {
  auto it = alt_modes_.find(index);
  if (it != alt_modes_.end()) {
    LOG(ERROR) << "Alt mode already registered at index " << index;
    return true;
  }

  return false;
}

void Partner::UpdateAltModesFromSysfs() {
  NOTIMPLEMENTED();
}

}  // namespace typecd
