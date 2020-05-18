// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/udev_monitor.h"

#include <base/logging.h>
#include <brillo/udev/udev_enumerate.h>

namespace {

constexpr char kTypeCSubsystem[] = "typec";

}  // namespace

namespace typecd {

bool UdevMonitor::InitUdev() {
  udev_ = brillo::Udev::Create();
  if (!udev_) {
    LOG(ERROR) << "Couldn't initialize udev object.";
    return false;
  }

  return true;
}

bool UdevMonitor::ScanDevices() {
  DCHECK(udev_);

  auto enumerate = udev_->CreateEnumerate();
  if (!enumerate->AddMatchSubsystem(kTypeCSubsystem)) {
    PLOG(ERROR) << "Couldn't add typec to enumerator match.";
    return false;
  }

  enumerate->ScanDevices();

  auto entry = enumerate->GetListEntry();
  if (!entry) {
    LOG(INFO) << "No devices found.\n";
    return true;
  }

  while (entry != nullptr) {
    HandleDeviceAdded(std::string(entry->GetName()));
    entry = entry->GetNext();
  }

  return true;
}

bool UdevMonitor::HandleDeviceAdded(const std::string& path) {
  LOG(INFO) << "Found device: " << path;

  // TODO(b/152251292): Actually handle this.
  NOTIMPLEMENTED();
  return true;
}

}  // namespace typecd
