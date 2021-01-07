// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/cros_ec_util.h"

#include <string>

#include <base/strings/string_split.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <re2/re2.h>

namespace {

constexpr char kECInventoryFeatureRegex[] = R"((\d+)\ +:\ +[\S\ ]+)";
constexpr int kAPModeEntryFeatureNumber = 42;
constexpr uint32_t kTypeCControlWaitMs = 20;

bool CheckInventoryForModeEntry(const std::string& inventory) {
  for (const auto& line : base::SplitString(
           inventory, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    int feature;
    if (!RE2::FullMatch(line, kECInventoryFeatureRegex, &feature)) {
      continue;
    }

    if (feature == kAPModeEntryFeatureNumber)
      return true;
  }

  return false;
}

}  // namespace

namespace typecd {

CrosECUtil::CrosECUtil(scoped_refptr<dbus::Bus> bus)
    : debugd_proxy_(std::make_unique<org::chromium::debugdProxy>(bus)) {}

bool CrosECUtil::ModeEntrySupported() {
  std::string inventory;
  brillo::ErrorPtr error;

  if (!debugd_proxy_->EcGetInventory(&inventory, &error)) {
    LOG(ERROR) << "Failed to call D-Bus GetInventory: " << error->GetMessage();
    return false;
  }

  return CheckInventoryForModeEntry(inventory);
}

bool CrosECUtil::EnterMode(int port, TypeCMode mode) {
  brillo::ErrorPtr error;
  std::string result;
  int retries = 5;

  while (retries--) {
    if (debugd_proxy_->EcTypeCEnterMode(port, mode, &result, &error))
      return true;

    LOG(INFO) << "Enter mode attempts remaining: " << retries;
    base::PlatformThread::Sleep(
        base::TimeDelta::FromMilliseconds(kTypeCControlWaitMs));
  }

  LOG(ERROR) << "Failed to call D-Bus TypeCEnterMode: " << error->GetMessage();

  return false;
}

}  // namespace typecd
