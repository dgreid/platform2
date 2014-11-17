// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/udev.h"

#include <libudev.h>

#include <utility>

#include <base/logging.h>

#include "power_manager/powerd/system/tagged_device.h"
#include "power_manager/powerd/system/udev_subsystem_observer.h"
#include "power_manager/powerd/system/udev_tagged_device_observer.h"

namespace power_manager {
namespace system {

namespace {

static const char kPowerdUdevTag[] = "powerd";
static const char kPowerdTagsVar[] = "POWERD_TAGS";

UdevAction StrToAction(const char* action_str) {
  if (!action_str)
    return UDEV_ACTION_UNKNOWN;
  else if (strcmp(action_str, "add") == 0)
    return UDEV_ACTION_ADD;
  else if (strcmp(action_str, "remove") == 0)
    return UDEV_ACTION_REMOVE;
  else if (strcmp(action_str, "change") == 0)
    return UDEV_ACTION_CHANGE;
  else if (strcmp(action_str, "online") == 0)
    return UDEV_ACTION_ONLINE;
  else if (strcmp(action_str, "offline") == 0)
    return UDEV_ACTION_OFFLINE;
  else
    return UDEV_ACTION_UNKNOWN;
}

};  // namespace

Udev::Udev() : udev_(NULL), udev_monitor_(NULL) {}

Udev::~Udev() {
  if (udev_monitor_)
    udev_monitor_unref(udev_monitor_);
  if (udev_)
    udev_unref(udev_);
}

bool Udev::Init() {
  udev_ = udev_new();
  if (!udev_) {
    PLOG(ERROR) << "udev_new() failed";
    return false;
  }

  udev_monitor_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!udev_monitor_) {
    PLOG(ERROR) << "udev_monitor_new_from_netlink() failed";
    return false;
  }

  if (udev_monitor_filter_add_match_tag(udev_monitor_, kPowerdUdevTag))
    LOG(ERROR) << "udev_monitor_filter_add_match_tag failed";
  if (udev_monitor_filter_update(udev_monitor_))
    LOG(ERROR) << "udev_monitor_filter_update failed";

  udev_monitor_enable_receiving(udev_monitor_);

  int fd = udev_monitor_get_fd(udev_monitor_);
  if (!base::MessageLoopForIO::current()->WatchFileDescriptor(
          fd, true, base::MessageLoopForIO::WATCH_READ, &watcher_, this)) {
    LOG(ERROR) << "Unable to watch FD " << fd;
    return false;
  }

  LOG(INFO) << "Watching FD " << fd << " for udev events";

  EnumerateTaggedDevices();

  return true;
}

void Udev::AddSubsystemObserver(const std::string& subsystem,
                                UdevSubsystemObserver* observer) {
  CHECK(udev_) << "Object uninitialized";
  DCHECK(observer);
  SubsystemObserverMap::iterator it = subsystem_observers_.find(subsystem);
  if (it == subsystem_observers_.end()) {
    it = subsystem_observers_.insert(std::make_pair(
        subsystem, make_linked_ptr(new ObserverList<UdevSubsystemObserver>)))
        .first;
  }
  it->second->AddObserver(observer);
}

void Udev::RemoveSubsystemObserver(const std::string& subsystem,
                                   UdevSubsystemObserver* observer) {
  DCHECK(observer);
  SubsystemObserverMap::iterator it = subsystem_observers_.find(subsystem);
  if (it != subsystem_observers_.end())
    it->second->RemoveObserver(observer);
}

void Udev::AddTaggedDeviceObserver(UdevTaggedDeviceObserver* observer) {
  tagged_device_observers_.AddObserver(observer);
}

void Udev::RemoveTaggedDeviceObserver(UdevTaggedDeviceObserver* observer) {
  tagged_device_observers_.RemoveObserver(observer);
}

std::vector<TaggedDevice> Udev::GetTaggedDevices() {
  std::vector<TaggedDevice> devices;
  devices.reserve(tagged_devices_.size());
  for (const std::pair<std::string, TaggedDevice>& pair : tagged_devices_)
    devices.push_back(pair.second);
  return devices;
}

bool Udev::GetSysattr(const std::string& syspath,
                      const std::string& sysattr,
                      std::string* value) {
  DCHECK(udev_);
  DCHECK(value);
  value->clear();

  struct udev_device* device =
      udev_device_new_from_syspath(udev_, syspath.c_str());
  if (!device) {
    LOG(WARNING) << "Failed to open udev device: " << syspath;
    return false;
  }
  const char* value_cstr =
      udev_device_get_sysattr_value(device, sysattr.c_str());
  if (value_cstr)
    *value = value_cstr;
  udev_device_unref(device);
  return value_cstr != NULL;
}

bool Udev::SetSysattr(const std::string& syspath,
                      const std::string& sysattr,
                      const std::string& value) {
  DCHECK(udev_);

  struct udev_device* device =
      udev_device_new_from_syspath(udev_, syspath.c_str());
  if (!device) {
    LOG(WARNING) << "Failed to open udev device: " << syspath;
    return false;
  }
  // udev can modify this value, hence we copy it first.
  scoped_ptr<char, base::FreeDeleter> value_mutable(strdup(value.c_str()));
  int rv = udev_device_set_sysattr_value(device, sysattr.c_str(),
                                         value_mutable.get());
  udev_device_unref(device);
  if (rv != 0) {
    LOG(WARNING) << "Failed to set sysattr '" << sysattr << "' on device "
                 << syspath << ": " << strerror(-rv);
    return false;
  }
  return true;
}

bool Udev::FindParentWithSysattr(const std::string& syspath,
                                 const std::string& sysattr,
                                 const std::string& stop_at_devtype,
                                 std::string* parent_syspath) {
  DCHECK(udev_);

  struct udev_device* device =
      udev_device_new_from_syspath(udev_, syspath.c_str());
  if (!device) {
    LOG(WARNING) << "Failed to open udev device: " << syspath;
    return false;
  }
  struct udev_device* parent = device;
  while (parent) {
    const char* value = udev_device_get_sysattr_value(parent, sysattr.c_str());
    const char* devtype = udev_device_get_devtype(parent);
    if (value)
      break;
    // Go up one level unless the devtype matches stop_at_devtype.
    if (devtype && strcmp(stop_at_devtype.c_str(), devtype) == 0)
        parent = nullptr;
    else
        parent = udev_device_get_parent(parent);
  }
  if (parent)
    *parent_syspath = udev_device_get_syspath(parent);
  udev_device_unref(device);
  return parent != NULL;
}

void Udev::OnFileCanReadWithoutBlocking(int fd) {
  struct udev_device* dev = udev_monitor_receive_device(udev_monitor_);
  if (!dev)
    return;

  const char* subsystem = udev_device_get_subsystem(dev);
  const char* sysname = udev_device_get_sysname(dev);
  const char* action_str = udev_device_get_action(dev);
  UdevAction action = StrToAction(action_str);

  VLOG(1) << "Received event: subsystem=" << subsystem
          << " sysname=" << sysname << " action=" << action_str;

  HandleSubsystemEvent(action, dev);
  HandleTaggedDevice(action, dev);

  udev_device_unref(dev);
}

void Udev::OnFileCanWriteWithoutBlocking(int fd) {
  NOTREACHED() << "Unexpected non-blocking write notification for FD " << fd;
}

void Udev::HandleSubsystemEvent(UdevAction action,
                                struct udev_device* dev) {
  const char* subsystem = udev_device_get_subsystem(dev);
  const char* sysname = udev_device_get_sysname(dev);

  if (!subsystem)
    return;

  SubsystemObserverMap::iterator it = subsystem_observers_.find(subsystem);
  if (it != subsystem_observers_.end()) {
    ObserverList<UdevSubsystemObserver>* observers = it->second.get();
    FOR_EACH_OBSERVER(UdevSubsystemObserver, *observers,
        OnUdevEvent(subsystem, sysname ? sysname : "", action));
  }
}

void Udev::HandleTaggedDevice(UdevAction action,
                              struct udev_device* dev) {
  if (!udev_device_has_tag(dev, kPowerdUdevTag))
    return;

  const char* syspath = udev_device_get_syspath(dev);
  const char* tags = udev_device_get_property_value(dev, kPowerdTagsVar);

  switch (action) {
    case UDEV_ACTION_ADD:
    case UDEV_ACTION_CHANGE:
      TaggedDeviceChanged(syspath, tags ? tags : "");
      break;

    case UDEV_ACTION_REMOVE:
      TaggedDeviceRemoved(syspath);
      break;

    default:
      break;
  }
}

void Udev::TaggedDeviceChanged(const std::string& syspath,
                               const std::string& tags) {
  // Replace existing device with same syspath.
  tagged_devices_[syspath] = TaggedDevice(syspath, tags);
  const TaggedDevice& device = tagged_devices_[syspath];

  VLOG(1) << "Tagged device changed: syspath=" << syspath << ", tags: "
          << (tags.empty() ? "(none)" : tags);

  FOR_EACH_OBSERVER(UdevTaggedDeviceObserver, tagged_device_observers_,
                    OnTaggedDeviceChanged(device));
}

void Udev::TaggedDeviceRemoved(const std::string& syspath) {
  TaggedDevice device = tagged_devices_[syspath];
  tagged_devices_.erase(syspath);

  VLOG(1) << "Tagged device removed: syspath=" << syspath;

  FOR_EACH_OBSERVER(UdevTaggedDeviceObserver, tagged_device_observers_,
                    OnTaggedDeviceRemoved(device));
}

bool Udev::EnumerateTaggedDevices() {
  DCHECK(udev_);

  VLOG(1) << "Enumerating existing tagged devices";

  struct udev_enumerate* enumerate = udev_enumerate_new(udev_);
  if (!enumerate) {
    LOG(ERROR) << "udev_enumerate_new failed";
    return false;
  }
  if (udev_enumerate_add_match_tag(enumerate, kPowerdUdevTag) != 0) {
    LOG(ERROR) << "udev_enumerate_add_match_tag failed";
    udev_enumerate_unref(enumerate);
    return false;
  }
  if (udev_enumerate_scan_devices(enumerate) != 0) {
    LOG(ERROR) << "udev_enumerate_scan_devices failed";
    udev_enumerate_unref(enumerate);
    return false;
  }

  tagged_devices_.clear();

  struct udev_list_entry* entry = NULL;
  udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
    const char* syspath = udev_list_entry_get_name(entry);
    struct udev_device* device = udev_device_new_from_syspath(udev_, syspath);
    if (!device) {
      LOG(ERROR) << "Enumerated device does not exist: " << syspath;
      continue;
    }
    const char* tags_cstr =
        udev_device_get_property_value(device, kPowerdTagsVar);
    const std::string tags = tags_cstr ? tags_cstr : "";
    VLOG(1) << "Pre-existing tagged device: syspath=" << syspath << ", tags: "
            << (tags.empty() ? "(none)" : tags);
    tagged_devices_[syspath] = TaggedDevice(syspath, tags);
    udev_device_unref(device);
  }

  udev_enumerate_unref(enumerate);
  return true;
}

}  // namespace system
}  // namespace power_manager
