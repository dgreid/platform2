// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/disk_monitor.h"

#include <inttypes.h>
#include <string.h>
#include <sys/mount.h>
#include <time.h>

#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/string_piece.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/time/time.h>
#include <brillo/udev/udev.h>
#include <brillo/udev/udev_device.h>
#include <brillo/udev/udev_enumerate.h>
#include <brillo/udev/udev_monitor.h>

#include "cros-disks/device_ejector.h"
#include "cros-disks/quote.h"
#include "cros-disks/udev_device.h"

namespace cros_disks {
namespace {

const char kBlockSubsystem[] = "block";
const char kMmcSubsystem[] = "mmc";
const char kScsiSubsystem[] = "scsi";
const char kScsiDevice[] = "scsi_device";
const char kUdevAddAction[] = "add";
const char kUdevChangeAction[] = "change";
const char kUdevRemoveAction[] = "remove";
const char kPropertyDiskEjectRequest[] = "DISK_EJECT_REQUEST";
const char kPropertyDiskMediaChange[] = "DISK_MEDIA_CHANGE";

// An EnumerateBlockDevices callback that appends a Disk object, created from
// |dev|, to |disks| if |dev| should not be ignored by cros-disks. Always
// returns true to continue the enumeration in EnumerateBlockDevices.
bool AppendDiskIfNotIgnored(std::vector<Disk>* disks,
                            std::unique_ptr<brillo::UdevDevice> dev) {
  DCHECK(disks);
  DCHECK(dev);

  UdevDevice device(std::move(dev));
  if (!device.IsIgnored())
    disks->push_back(device.ToDisk());

  return true;  // Continue the enumeration.
}

// An EnumerateBlockDevices callback that checks if |dev| matches |path|. If
// it's a match, sets |match| to true and |disk| (if not NULL) to a Disk object
// created from |dev|, and returns false to stop the enumeration in
// EnumerateBlockDevices. Otherwise, sets |match| to false, leaves |disk|
// unchanged, and returns true to continue the enumeration in
// EnumerateBlockDevices.
bool MatchDiskByPath(const std::string& path,
                     bool* match,
                     Disk* disk,
                     std::unique_ptr<brillo::UdevDevice> dev) {
  DCHECK(match);
  DCHECK(dev);

  const char* sys_path = dev->GetSysPath();
  const char* dev_path = dev->GetDevicePath();
  const char* dev_file = dev->GetDeviceNode();
  *match = (sys_path && path == sys_path) || (dev_path && path == dev_path) ||
           (dev_file && path == dev_file);
  if (!*match)
    return true;  // Not a match. Continue the enumeration.

  if (disk)
    *disk = UdevDevice(std::move(dev)).ToDisk();

  return false;  // Match. Stop enumeration.
}

// Logs a device with its properties.
void LogUdevDevice(const brillo::UdevDevice& dev) {
  if (!VLOG_IS_ON(1))
    return;

  // Some device events (eg USB drive removal) result in devnode being null.
  // This is gracefully handled by quote() without crashing.
  VLOG(1) << "   node: " << quote(dev.GetDeviceNode());
  VLOG(1) << "   subsystem: " << quote(dev.GetSubsystem());
  VLOG(1) << "   devtype: " << quote(dev.GetDeviceType());
  VLOG(1) << "   devpath: " << quote(dev.GetDevicePath());
  VLOG(1) << "   sysname: " << quote(dev.GetSysName());
  VLOG(1) << "   syspath: " << quote(dev.GetSysPath());

  if (!VLOG_IS_ON(2))
    return;

  // Log all properties.
  for (std::unique_ptr<brillo::UdevListEntry> prop =
           dev.GetPropertiesListEntry();
       prop; prop = prop->GetNext()) {
    VLOG(2) << "   " << prop->GetName() << ": " << quote(prop->GetValue());
  }
}

}  // namespace

DiskMonitor::DiskMonitor() : udev_(brillo::Udev::Create()) {
  CHECK(udev_) << "Failed to initialize udev";
  udev_monitor_ = udev_->CreateMonitorFromNetlink("udev");
  CHECK(udev_monitor_) << "Failed to create a udev monitor";
  udev_monitor_->FilterAddMatchSubsystemDeviceType(kBlockSubsystem, nullptr);
  udev_monitor_->FilterAddMatchSubsystemDeviceType(kMmcSubsystem, nullptr);
  udev_monitor_->FilterAddMatchSubsystemDeviceType(kScsiSubsystem, kScsiDevice);
  CHECK(udev_monitor_->EnableReceiving());
}

DiskMonitor::~DiskMonitor() = default;

int DiskMonitor::udev_monitor_fd() const {
  return udev_monitor_->GetFileDescriptor();
}

bool DiskMonitor::Initialize() {
  // Since there are no udev add events for the devices that already exist
  // when the disk manager starts, emulate udev add events for these devices
  // to correctly populate |disks_detected_|.
  EnumerateBlockDevices(base::BindRepeating(
      &DiskMonitor::EmulateAddBlockDeviceEvent, base::Unretained(this)));
  return true;
}

bool DiskMonitor::EmulateAddBlockDeviceEvent(
    std::unique_ptr<brillo::UdevDevice> dev) {
  DeviceEventList events;
  LOG(INFO) << "Emulating action 'add' on device " << quote(dev->GetSysName());
  LogUdevDevice(*dev);
  ProcessBlockDeviceEvents(std::move(dev), kUdevAddAction, &events);
  return true;  // Continue the enumeration.
}

std::vector<Disk> DiskMonitor::EnumerateDisks() const {
  std::vector<Disk> disks;
  EnumerateBlockDevices(
      base::BindRepeating(&AppendDiskIfNotIgnored, base::Unretained(&disks)));
  return disks;
}

void DiskMonitor::EnumerateBlockDevices(
    base::RepeatingCallback<bool(std::unique_ptr<brillo::UdevDevice> dev)>
        callback) const {
  std::unique_ptr<brillo::UdevEnumerate> enumerate = udev_->CreateEnumerate();
  enumerate->AddMatchSubsystem(kBlockSubsystem);
  enumerate->ScanDevices();

  for (std::unique_ptr<brillo::UdevListEntry> entry = enumerate->GetListEntry();
       entry; entry = entry->GetNext()) {
    std::unique_ptr<brillo::UdevDevice> dev =
        udev_->CreateDeviceFromSysPath(entry->GetName());
    if (!dev)
      continue;

    VLOG(1) << "Found device " << quote(dev->GetSysName());
    LogUdevDevice(*dev);

    bool continue_enumeration = callback.Run(std::move(dev));
    if (!continue_enumeration)
      break;
  }
}

void DiskMonitor::ProcessBlockDeviceEvents(
    std::unique_ptr<brillo::UdevDevice> dev,
    const char* action,
    DeviceEventList* events) {
  brillo::UdevDevice* raw_dev = dev.get();
  UdevDevice device(std::move(dev));
  if (device.IsIgnored())
    return;

  bool disk_added = false;
  bool disk_removed = false;
  bool child_disk_removed = false;
  if (strcmp(action, kUdevAddAction) == 0) {
    disk_added = true;
  } else if (strcmp(action, kUdevRemoveAction) == 0) {
    disk_removed = true;
  } else if (strcmp(action, kUdevChangeAction) == 0) {
    // For removable devices like CD-ROM, an eject request event
    // is treated as disk removal, while a media change event with
    // media available is treated as disk insertion.
    if (device.IsPropertyTrue(kPropertyDiskEjectRequest)) {
      disk_removed = true;
    } else if (device.IsPropertyTrue(kPropertyDiskMediaChange)) {
      if (device.IsMediaAvailable()) {
        disk_added = true;
      } else {
        child_disk_removed = true;
      }
    }
  }

  std::string device_path = device.NativePath();
  if (disk_added) {
    if (device.IsAutoMountable()) {
      if (base::Contains(disks_detected_, device_path)) {
        // Disk already exists, so remove it and then add it again.
        events->push_back(DeviceEvent(DeviceEvent::kDiskRemoved, device_path));
      } else {
        disks_detected_[device_path] = {};

        // Add the disk as a child of its parent if the parent is already
        // added to |disks_detected_|.
        std::unique_ptr<brillo::UdevDevice> parent = raw_dev->GetParent();
        if (parent) {
          std::string parent_device_path =
              UdevDevice(std::move(parent)).NativePath();
          if (base::Contains(disks_detected_, parent_device_path)) {
            disks_detected_[parent_device_path].insert(device_path);
          }
        }
      }
      events->push_back(DeviceEvent(DeviceEvent::kDiskAdded, device_path));
    }
  } else if (disk_removed) {
    disks_detected_.erase(device_path);
    events->push_back(DeviceEvent(DeviceEvent::kDiskRemoved, device_path));
  } else if (child_disk_removed) {
    bool no_child_disks_found = true;
    if (base::Contains(disks_detected_, device_path)) {
      auto& child_disks = disks_detected_[device_path];
      no_child_disks_found = child_disks.empty();
      for (const auto& child_disk : child_disks) {
        events->push_back(DeviceEvent(DeviceEvent::kDiskRemoved, child_disk));
      }
    }
    // When the device contains a full-disk partition, there are no child disks.
    // Remove the device instead.
    if (no_child_disks_found)
      events->push_back(DeviceEvent(DeviceEvent::kDiskRemoved, device_path));
  }
}

void DiskMonitor::ProcessMmcOrScsiDeviceEvents(
    std::unique_ptr<brillo::UdevDevice> dev,
    const char* action,
    DeviceEventList* events) {
  UdevDevice device(std::move(dev));
  if (device.IsMobileBroadbandDevice())
    return;

  std::string device_path = device.NativePath();
  if (strcmp(action, kUdevAddAction) == 0) {
    if (base::Contains(devices_detected_, device_path)) {
      events->push_back(DeviceEvent(DeviceEvent::kDeviceScanned, device_path));
    } else {
      devices_detected_.insert(device_path);
      events->push_back(DeviceEvent(DeviceEvent::kDeviceAdded, device_path));
    }
  } else if (strcmp(action, kUdevRemoveAction) == 0) {
    if (base::Contains(devices_detected_, device_path)) {
      devices_detected_.erase(device_path);
      events->push_back(DeviceEvent(DeviceEvent::kDeviceRemoved, device_path));
    }
  }
}

bool DiskMonitor::GetDeviceEvents(DeviceEventList* events) {
  CHECK(events) << "Invalid device event list";

  std::unique_ptr<brillo::UdevDevice> dev = udev_monitor_->ReceiveDevice();
  if (!dev) {
    LOG(WARNING) << "Ignore device event with no associated udev device.";
    return false;
  }

  const char* sys_path = dev->GetSysPath();
  const char* subsystem = dev->GetSubsystem();
  const char* action = dev->GetAction();

  LOG(INFO) << "Got action " << quote(action) << " on device "
            << quote(dev->GetSysName());
  LogUdevDevice(*dev);

  if (!sys_path || !subsystem || !action) {
    return false;
  }

  // |udev_monitor_| only monitors block, mmc, and scsi device changes, so
  // subsystem is either "block", "mmc", or "scsi".
  if (strcmp(subsystem, kBlockSubsystem) == 0) {
    ProcessBlockDeviceEvents(std::move(dev), action, events);
  } else {
    // strcmp(subsystem, kMmcSubsystem) == 0 ||
    // strcmp(subsystem, kScsiSubsystem) == 0
    ProcessMmcOrScsiDeviceEvents(std::move(dev), action, events);
  }

  return true;
}

bool DiskMonitor::GetDiskByDevicePath(const base::FilePath& device_path,
                                      Disk* disk) const {
  if (device_path.empty())
    return false;

  bool disk_found = false;
  EnumerateBlockDevices(base::BindRepeating(
      &MatchDiskByPath, device_path.value(), base::Unretained(&disk_found),
      base::Unretained(disk)));
  return disk_found;
}

bool DiskMonitor::IsPathRecognized(const base::FilePath& path) const {
  // The following paths are handled:
  //     /sys/...
  //     /devices/...
  //     /dev/...
  return base::StartsWith(path.value(), "/sys/",
                          base::CompareCase::SENSITIVE) ||
         base::StartsWith(path.value(), "/devices/",
                          base::CompareCase::SENSITIVE) ||
         base::StartsWith(path.value(), "/dev/", base::CompareCase::SENSITIVE);
}

}  // namespace cros_disks
