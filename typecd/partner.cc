// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/partner.h"

#include <string>

#include <base/files/file_enumerator.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

namespace {

constexpr char kPartnerAltModeRegex[] = R"(port(\d+)-partner.(\d+))";

}

namespace typecd {

Partner::Partner(const base::FilePath& syspath)
    : Peripheral(syspath), num_alt_modes_(-1) {
  // Search for all alt modes which were already registered prior to daemon
  // init.
  base::FileEnumerator iter(GetSysPath(), false,
                            base::FileEnumerator::DIRECTORIES);
  for (auto path = iter.Next(); !path.empty(); path = iter.Next())
    AddAltMode(path);

  SetNumAltModes(ParseNumAltModes());
}

bool Partner::AddAltMode(const base::FilePath& mode_syspath) {
  int port, index;
  if (!RE2::FullMatch(mode_syspath.BaseName().value(), kPartnerAltModeRegex,
                      &port, &index))
    return false;

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

void Partner::UpdatePDInfoFromSysfs() {
  if (GetNumAltModes() == -1)
    SetNumAltModes(ParseNumAltModes());
  UpdatePDIdentityVDOs();
}

int Partner::ParseNumAltModes() {
  auto path = GetSysPath().Append("number_of_alternate_modes");

  std::string val_str;
  if (!base::ReadFileToString(path, &val_str))
    return -1;

  base::TrimWhitespaceASCII(val_str, base::TRIM_TRAILING, &val_str);

  int num_altmodes;
  if (!base::StringToInt(val_str.c_str(), &num_altmodes)) {
    LOG(ERROR) << "Couldn't parse num_altmodes from string: " << val_str;
    return -1;
  }

  return num_altmodes;
}

AltMode* Partner::GetAltMode(int index) {
  if (!IsAltModePresent(index))
    return nullptr;

  return alt_modes_.find(index)->second.get();
}

bool Partner::DiscoveryComplete() {
  return num_alt_modes_ == alt_modes_.size();
}

}  // namespace typecd
