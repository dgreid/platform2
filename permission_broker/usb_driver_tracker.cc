// Copyright 2016 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/usb_driver_tracker.h"

#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>

#include "permission_broker/udev_scopers.h"

namespace permission_broker {

struct UsbDriverTracker::UsbInterfaces {
  std::string path;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> controller;
  std::vector<uint8_t> ifaces;
  base::ScopedFD fd;
};

UsbDriverTracker::UsbDriverTracker() = default;

UsbDriverTracker::~UsbDriverTracker() {
  // Re-attach all delegated USB interfaces
  for (auto& elem : dev_fds_) {
    auto entry = std::move(elem.second);
    ReAttachPathToKernel(entry.path, entry.ifaces);
  }
}

void UsbDriverTracker::HandleClosedFd(int fd) {
  auto iter = dev_fds_.find(fd);
  if (iter != dev_fds_.end()) {
    auto entry = std::move(iter->second);
    // Re-attaching the kernel driver to the USB interface.
    ReAttachPathToKernel(entry.path, entry.ifaces);

    // We are done with the lifeline_fd.
    dev_fds_.erase(iter);
  } else {
    LOG(WARNING) << "Untracked USB lifeline_fd " << fd;
  }
}

bool UsbDriverTracker::ReAttachPathToKernel(
    const std::string& path, const std::vector<uint8_t>& ifaces) {
  int fd = HANDLE_EINTR(open(path.c_str(), O_RDWR));
  if (fd < 0) {
    LOG(WARNING) << "Cannot open " << path;
    return false;
  }

  for (uint8_t iface_num : ifaces) {
    struct usbdevfs_ioctl dio;
    dio.ifno = iface_num;
    dio.ioctl_code = USBDEVFS_CONNECT;
    dio.data = nullptr;

    int res = ioctl(fd, USBDEVFS_IOCTL, &dio);
    if (res < 0) {
      LOG(WARNING) << "Kernel USB driver connection for " << path
                   << " on interface " << iface_num << " failed " << errno;
    } else {
      LOG(INFO) << "Kernel USB driver attached on " << path << " interface "
                << static_cast<int>(iface_num);
    }
  }
  IGNORE_EINTR(close(fd));

  return true;
}

bool UsbDriverTracker::DetachPathFromKernel(int fd,
                                            int lifeline_fd,
                                            const std::string& path) {
  // Use the USB device node major/minor to find the udev entry.
  struct stat st;
  if (fstat(fd, &st) || !S_ISCHR(st.st_mode)) {
    LOG(WARNING) << "Cannot stat " << path << " device id";
    return false;
  }

  ScopedUdevPtr udev(udev_new());
  ScopedUdevDevicePtr device(
      udev_device_new_from_devnum(udev.get(), 'c', st.st_rdev));
  if (!device.get()) {
    return false;
  }

  ScopedUdevEnumeratePtr enumerate(udev_enumerate_new(udev.get()));
  udev_enumerate_add_match_parent(enumerate.get(), device.get());
  udev_enumerate_scan_devices(enumerate.get());

  // Try to find our USB interface nodes, by iterating through all devices
  // and extracting our children devices.
  bool detached = false;
  std::vector<uint8_t> ifaces;
  struct udev_list_entry* entry;
  udev_list_entry_foreach(entry,
                          udev_enumerate_get_list_entry(enumerate.get())) {
    const char* entry_path = udev_list_entry_get_name(entry);
    ScopedUdevDevicePtr child(
        udev_device_new_from_syspath(udev.get(), entry_path));

    const char* child_type = udev_device_get_devtype(child.get());
    if (!child_type || strcmp(child_type, "usb_interface") != 0) {
      continue;
    }

    const char* driver = udev_device_get_driver(child.get());
    if (driver) {
      // A kernel driver is using this interface, try to detach it.
      const char* iface =
          udev_device_get_sysattr_value(child.get(), "bInterfaceNumber");
      unsigned iface_num;
      if (!iface || !base::StringToUint(iface, &iface_num)) {
        detached = false;
        continue;
      }

      struct usbdevfs_ioctl dio;
      dio.ifno = iface_num;
      dio.ioctl_code = USBDEVFS_DISCONNECT;
      dio.data = nullptr;

      int res = ioctl(fd, USBDEVFS_IOCTL, &dio);
      if (res < 0) {
        LOG(WARNING) << "Kernel USB driver disconnection for " << path
                     << " on interface " << iface_num << " failed " << errno;
      } else {
        detached = true;
        ifaces.push_back(iface_num);
        LOG(INFO) << "USB driver '" << driver << "' detached on " << path
                  << " interface " << iface_num;
      }
    }
  }

  if (detached && lifeline_fd != kInvalidLifelineFD) {
    WatchLifelineFd(lifeline_fd, std::move(path), std::move(ifaces));
  }

  return detached;
}

void UsbDriverTracker::WatchLifelineFd(int lifeline_fd,
                                       std::string path,
                                       std::vector<uint8_t> ifaces) {
  base::ScopedFD lifeline(HANDLE_EINTR(dup(lifeline_fd)));
  int dup_fd = lifeline.get();
  auto controller = base::FileDescriptorWatcher::WatchReadable(
      dup_fd, base::BindRepeating(&UsbDriverTracker::HandleClosedFd,
                                  weak_ptr_factory_.GetWeakPtr(), dup_fd));
  if (!controller) {
    LOG(ERROR) << "Unable to watch lifeline_fd " << dup_fd;
    return;
  }
  VLOG(1) << "Watching lifeline_fd " << dup_fd;
  dev_fds_.emplace(dup_fd,
                   UsbInterfaces{std::move(path), std::move(controller),
                                 std::move(ifaces), std::move(lifeline)});
}

}  // namespace permission_broker
