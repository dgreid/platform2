// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/udev_monitor.h"

#include <base/logging.h>
#include <brillo/udev/udev_enumerate.h>
#include <re2/re2.h>

namespace {

constexpr char kPartnerRegex[] = R"(port(\d+)-partner)";
constexpr char kPortRegex[] = R"(port(\d+))";
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
    HandleDeviceAdded(base::FilePath(std::string(entry->GetName())));
    entry = entry->GetNext();
  }

  return true;
}

void UdevMonitor::AddObserver(Observer* obs) {
  observer_list_.AddObserver(obs);
}

void UdevMonitor::RemoveObserver(Observer* obs) {
  observer_list_.RemoveObserver(obs);
}

bool UdevMonitor::HandleDeviceAdded(const base::FilePath& path) {
  auto name = path.BaseName();

  LOG(INFO) << "Found device: " << path;

  for (Observer& observer : observer_list_) {
    if (RE2::FullMatch(name.value(), kPortRegex))
      observer.OnPortAddedOrRemoved(path, true);
    else if (RE2::FullMatch(name.value(), kPartnerRegex))
      observer.OnPartnerAddedOrRemoved(path, true);
  }

  return true;
}

}  // namespace typecd
