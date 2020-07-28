// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/partner.h"

#include <base/logging.h>
#include <re2/re2.h>

namespace {

constexpr char kPartnerAltModeRegex[] = R"(port(\d+)-partner.(\d+))";

}

namespace typecd {

Partner::Partner(const base::FilePath& syspath)
    : Peripheral(syspath), num_alt_modes_(-1) {}

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
    return true;
  }

  LOG(INFO) << "Alt mode not found at index " << index;
  return false;
}

void Partner::UpdateAltModesFromSysfs() {
  NOTIMPLEMENTED();
}

AltMode* Partner::GetAltMode(int index) {
  if (!IsAltModePresent(index))
    return nullptr;

  return alt_modes_.find(index)->second.get();
}

}  // namespace typecd
