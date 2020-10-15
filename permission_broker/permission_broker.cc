// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "permission_broker/permission_broker.h"

#include <fcntl.h>
#include <linux/usb/ch9.h>
#include <linux/usbdevice_fs.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback.h>
#include <base/compiler_specific.h>
#include <base/logging.h>
#include <base/posix/eintr_wrapper.h>
#include <brillo/userdb_utils.h>
#include <chromeos/dbus/service_constants.h>

#include "permission_broker/allow_group_tty_device_rule.h"
#include "permission_broker/allow_hidraw_device_rule.h"
#include "permission_broker/allow_tty_device_rule.h"
#include "permission_broker/allow_usb_device_rule.h"
#include "permission_broker/deny_claimed_hidraw_device_rule.h"
#include "permission_broker/deny_claimed_usb_device_rule.h"
#include "permission_broker/deny_fwupdate_hidraw_device_rule.h"
#include "permission_broker/deny_group_tty_device_rule.h"
#include "permission_broker/deny_uninitialized_device_rule.h"
#include "permission_broker/deny_unsafe_hidraw_device_rule.h"
#include "permission_broker/deny_usb_device_class_rule.h"
#include "permission_broker/deny_usb_vendor_id_rule.h"
#include "permission_broker/libusb_wrapper.h"
#include "permission_broker/rule.h"
#include "permission_broker/usb_control.h"

using permission_broker::AllowGroupTtyDeviceRule;
using permission_broker::AllowHidrawDeviceRule;
using permission_broker::AllowTtyDeviceRule;
using permission_broker::AllowUsbDeviceRule;
using permission_broker::DenyClaimedHidrawDeviceRule;
using permission_broker::DenyClaimedUsbDeviceRule;
using permission_broker::DenyGroupTtyDeviceRule;
using permission_broker::DenyUninitializedDeviceRule;
using permission_broker::DenyUnsafeHidrawDeviceRule;
using permission_broker::DenyUsbDeviceClassRule;
using permission_broker::DenyUsbVendorIdRule;
using permission_broker::PermissionBroker;

namespace {
const uint16_t kLinuxFoundationUsbVendorId = 0x1d6b;

const char kErrorDomainPermissionBroker[] = "permission_broker";
const char kPermissionDeniedError[] = "permission_denied";
const char kOpenFailedError[] = "open_failed";

constexpr uint32_t kAllInterfacesMask = ~0U;
}  // namespace

namespace permission_broker {

PermissionBroker::PermissionBroker(scoped_refptr<dbus::Bus> bus,
                                   const std::string& udev_run_path,
                                   const base::TimeDelta& poll_interval)
    : org::chromium::PermissionBrokerAdaptor(this),
      rule_engine_(udev_run_path, poll_interval),
      dbus_object_(
          nullptr, bus, dbus::ObjectPath(kPermissionBrokerServicePath)),
      port_tracker_(),
      usb_control_(std::make_unique<UsbDeviceManager>()) {
  rule_engine_.AddRule(new AllowUsbDeviceRule());
  rule_engine_.AddRule(new AllowTtyDeviceRule());
  rule_engine_.AddRule(new DenyClaimedUsbDeviceRule());
  rule_engine_.AddRule(new DenyUninitializedDeviceRule());
  rule_engine_.AddRule(new DenyUsbDeviceClassRule(USB_CLASS_HUB));
  rule_engine_.AddRule(new DenyUsbDeviceClassRule(USB_CLASS_MASS_STORAGE));
  rule_engine_.AddRule(new DenyUsbVendorIdRule(kLinuxFoundationUsbVendorId));
  rule_engine_.AddRule(new AllowHidrawDeviceRule());
  rule_engine_.AddRule(new AllowGroupTtyDeviceRule("serial"));
  rule_engine_.AddRule(new DenyGroupTtyDeviceRule("modem"));
  rule_engine_.AddRule(new DenyGroupTtyDeviceRule("tty"));
  rule_engine_.AddRule(new DenyGroupTtyDeviceRule("uucp"));
  rule_engine_.AddRule(new DenyClaimedHidrawDeviceRule());
  rule_engine_.AddRule(new DenyUnsafeHidrawDeviceRule());
  rule_engine_.AddRule(new DenyFwUpdateHidrawDeviceRule());
}

PermissionBroker::~PermissionBroker() = default;

void PermissionBroker::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb) {
  RegisterWithDBusObject(&dbus_object_);
  dbus_object_.RegisterAsync(cb);
}

bool PermissionBroker::CheckPathAccess(const std::string& in_path) {
  Rule::Result result = rule_engine_.ProcessPath(in_path);
  return result == Rule::ALLOW || result == Rule::ALLOW_WITH_LOCKDOWN ||
         result == Rule::ALLOW_WITH_DETACH;
}

bool PermissionBroker::OpenPath(brillo::ErrorPtr* error,
                                const std::string& in_path,
                                brillo::dbus_utils::FileDescriptor* out_fd) {
  VLOG(1) << "Received OpenPath request";
  return OpenPathImpl(error, in_path, kAllInterfacesMask, kInvalidLifelineFD,
                      out_fd);
}

bool PermissionBroker::ClaimDevicePath(
    brillo::ErrorPtr* error,
    const std::string& in_path,
    uint32_t drop_privileges_mask,
    const base::ScopedFD& in_lifeline_fd,
    brillo::dbus_utils::FileDescriptor* out_fd) {
  VLOG(1) << "Received ClaimDevicePath request";
  return OpenPathImpl(error, in_path, drop_privileges_mask,
                      in_lifeline_fd.get(), out_fd);
}

bool PermissionBroker::RequestLoopbackTcpPortLockdown(
    uint16_t in_port, const base::ScopedFD& in_lifeline_fd) {
  return port_tracker_.LockDownLoopbackTcpPort(in_port, in_lifeline_fd.get());
}

bool PermissionBroker::RequestTcpPortAccess(
    uint16_t in_port,
    const std::string& in_interface,
    const base::ScopedFD& in_lifeline_fd) {
  return port_tracker_.AllowTcpPortAccess(in_port, in_interface,
                                          in_lifeline_fd.get());
}

bool PermissionBroker::RequestUdpPortAccess(
    uint16_t in_port,
    const std::string& in_interface,
    const base::ScopedFD& in_lifeline_fd) {
  return port_tracker_.AllowUdpPortAccess(in_port, in_interface,
                                          in_lifeline_fd.get());
}

bool PermissionBroker::ReleaseTcpPort(uint16_t in_port,
                                      const std::string& in_interface) {
  return port_tracker_.RevokeTcpPortAccess(in_port, in_interface);
}

bool PermissionBroker::ReleaseUdpPort(uint16_t in_port,
                                      const std::string& in_interface) {
  return port_tracker_.RevokeUdpPortAccess(in_port, in_interface);
}

bool PermissionBroker::ReleaseLoopbackTcpPort(uint16_t in_port) {
  return port_tracker_.ReleaseLoopbackTcpPort(in_port);
}

bool PermissionBroker::RequestTcpPortForward(uint16_t in_port,
                                             const std::string& in_interface,
                                             const std::string& dst_ip,
                                             uint16_t dst_port,
                                             const base::ScopedFD& dbus_fd) {
  return port_tracker_.StartTcpPortForwarding(in_port, in_interface, dst_ip,
                                              dst_port, dbus_fd.get());
}

bool PermissionBroker::RequestUdpPortForward(uint16_t in_port,
                                             const std::string& in_interface,
                                             const std::string& dst_ip,
                                             uint16_t dst_port,
                                             const base::ScopedFD& dbus_fd) {
  return port_tracker_.StartUdpPortForwarding(in_port, in_interface, dst_ip,
                                              dst_port, dbus_fd.get());
}

bool PermissionBroker::ReleaseTcpPortForward(uint16_t in_port,
                                             const std::string& in_interface) {
  return port_tracker_.StopTcpPortForwarding(in_port, in_interface);
}

bool PermissionBroker::ReleaseUdpPortForward(uint16_t in_port,
                                             const std::string& in_interface) {
  return port_tracker_.StopUdpPortForwarding(in_port, in_interface);
}

void PowerCycleUsbPortsResultCallback(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    bool result) {
  response->Return(result);
}

void PermissionBroker::PowerCycleUsbPorts(
    std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
    uint16_t in_vid,
    uint16_t in_pid,
    int64_t in_delay) {
  usb_control_.PowerCycleUsbPorts(base::Bind(&PowerCycleUsbPortsResultCallback,
                                             base::Passed(std::move(response))),
                                  in_vid, in_pid,
                                  base::TimeDelta::FromInternalValue(in_delay));
}

bool PermissionBroker::OpenPathImpl(
    brillo::ErrorPtr* error,
    const std::string& in_path,
    uint32_t drop_privileges_mask,
    int lifeline_fd,
    brillo::dbus_utils::FileDescriptor* out_fd) {
  Rule::Result rule_result = rule_engine_.ProcessPath(in_path);
  if (rule_result != Rule::ALLOW && rule_result != Rule::ALLOW_WITH_LOCKDOWN &&
      rule_result != Rule::ALLOW_WITH_DETACH) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, kErrorDomainPermissionBroker, kPermissionDeniedError,
        "Permission to open '%s' denied", in_path.c_str());
    return false;
  }

  base::ScopedFD fd(HANDLE_EINTR(open(in_path.c_str(), O_RDWR)));
  if (!fd.is_valid()) {
    brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
    brillo::Error::AddToPrintf(error, FROM_HERE, kErrorDomainPermissionBroker,
                               kOpenFailedError, "Failed to open path '%s'",
                               in_path.c_str());
    return false;
  }

  if (rule_result == Rule::ALLOW_WITH_DETACH) {
    if (!usb_driver_tracker_.DetachPathFromKernel(fd.get(), lifeline_fd,
                                                  in_path))
      return false;
  }

  // When the rule result is ALLOW_WITH_LOCKDOWN and the mask is
  // |kAllInterfacesMask| (allowing all interfaces), we still call the
  // USBDEVFS_DROP_PRIVILEGES ioctl.
  // This prevents the use of the USBDEVFS_DISCONNECT ioctl as well as
  // USBDEVFS_SETCONFIGURATION and USBDEVFS_RESET when these could be used to
  // detach a kernel driver by changing the device configuration. That's the
  // "drop privileges" part.
  if (rule_result == Rule::ALLOW_WITH_LOCKDOWN ||
      drop_privileges_mask != kAllInterfacesMask) {
    if (ioctl(fd.get(), USBDEVFS_DROP_PRIVILEGES, &drop_privileges_mask) < 0) {
      brillo::errors::system::AddSystemError(error, FROM_HERE, errno);
      brillo::Error::AddToPrintf(
          error, FROM_HERE, kErrorDomainPermissionBroker, kOpenFailedError,
          "USBDEVFS_DROP_PRIVILEGES ioctl failed on '%s'", in_path.c_str());
      return false;
    }
  }

  *out_fd = std::move(fd);
  return true;
}

}  // namespace permission_broker
