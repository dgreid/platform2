// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERMISSION_BROKER_PERMISSION_BROKER_H_
#define PERMISSION_BROKER_PERMISSION_BROKER_H_

#include <dbus/dbus.h>

#include <memory>
#include <string>
#include <vector>

#include <base/macros.h>
#include <base/message_loop/message_loop.h>
#include <base/sequenced_task_runner.h>
#include <base/time/time.h>
#include <dbus/bus.h>

#include "permission_broker/dbus_adaptors/org.chromium.PermissionBroker.h"
#include "permission_broker/port_tracker.h"
#include "permission_broker/rule_engine.h"
#include "permission_broker/usb_control.h"
#include "permission_broker/usb_driver_tracker.h"

namespace permission_broker {

// The PermissionBroker encapsulates the execution of a chain of Rules which
// decide whether or not to grant access to a given path. The PermissionBroker
// is also responsible for providing a D-Bus interface to clients.
class PermissionBroker : public org::chromium::PermissionBrokerAdaptor,
                         public org::chromium::PermissionBrokerInterface {
 public:
  PermissionBroker(scoped_refptr<dbus::Bus> bus,
                   const std::string& udev_run_path,
                   const base::TimeDelta& poll_interval);
  ~PermissionBroker();

  // Register the D-Bus object and interfaces.
  void RegisterAsync(
      const brillo::dbus_utils::AsyncEventSequencer::CompletionAction& cb);

 private:
  // D-Bus methods.
  bool CheckPathAccess(const std::string& in_path) override;
  bool OpenPath(brillo::ErrorPtr* error,
                const std::string& in_path,
                brillo::dbus_utils::FileDescriptor* out_fd) override;
  bool OpenPathWithDroppedPrivileges(
      brillo::ErrorPtr* error,
      const std::string& in_path,
      uint32_t drop_privileges_mask,
      brillo::dbus_utils::FileDescriptor* out_fd) override;
  bool RequestTcpPortAccess(uint16_t in_port,
                            const std::string& in_interface,
                            const base::ScopedFD& dbus_fd) override;
  bool RequestUdpPortAccess(uint16_t in_port,
                            const std::string& in_interface,
                            const base::ScopedFD& dbus_fd) override;
  bool RequestLoopbackTcpPortLockdown(
      uint16_t in_port, const base::ScopedFD& in_lifeline_fd) override;
  bool ReleaseTcpPort(uint16_t in_port,
                      const std::string& in_interface) override;
  bool ReleaseUdpPort(uint16_t in_port,
                      const std::string& in_interface) override;
  bool ReleaseLoopbackTcpPort(uint16_t in_port) override;
  bool RequestTcpPortForward(uint16_t in_port,
                             const std::string& in_interface,
                             const std::string& dst_ip,
                             uint16_t dst_port,
                             const base::ScopedFD& dbus_fd) override;
  bool RequestUdpPortForward(uint16_t in_port,
                             const std::string& in_interface,
                             const std::string& dst_ip,
                             uint16_t dst_port,
                             const base::ScopedFD& dbus_fd) override;
  bool ReleaseTcpPortForward(uint16_t in_port,
                             const std::string& in_interface) override;
  bool ReleaseUdpPortForward(uint16_t in_port,
                             const std::string& in_interface) override;
  void PowerCycleUsbPorts(
      std::unique_ptr<brillo::dbus_utils::DBusMethodResponse<bool>> response,
      uint16_t in_vid,
      uint16_t in_pid,
      int64_t in_delay) override;
  bool OpenPathImpl(brillo::ErrorPtr* error,
                    const std::string& in_path,
                    uint32_t drop_privileges_mask,
                    brillo::dbus_utils::FileDescriptor* out_fd);

  RuleEngine rule_engine_;
  brillo::dbus_utils::DBusObject dbus_object_;
  PortTracker port_tracker_;
  UsbControl usb_control_;
  UsbDriverTracker usb_driver_tracker_;

  DISALLOW_COPY_AND_ASSIGN(PermissionBroker);
};

}  // namespace permission_broker

#endif  // PERMISSION_BROKER_PERMISSION_BROKER_H_
