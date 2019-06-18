// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_VM_INTERFACE_H_
#define VM_TOOLS_CONCIERGE_VM_INTERFACE_H_

#include <stdint.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <vm_concierge/proto_bindings/concierge_service.pb.h>

#include "vm_tools/concierge/usb_control.h"

namespace vm_tools {
namespace concierge {

// Represents a single instance of a virtual machine.
class VmInterface {
 public:
  // The current status of the VM.
  enum class Status {
    STARTING,
    RUNNING,
    STOPPED,
  };

  // Information about a virtual machine.
  struct Info {
    // The IPv4 address in network-byte order.
    uint32_t ipv4_address;

    // The pid of the main crosvm process for the VM.
    pid_t pid;

    // The vsock context id for the VM, if one exists.  Must be set to 0 if
    // there is no vsock context id.
    uint32_t cid;

    // The handle for the 9P server managed by seneschal on behalf of this VM
    // if one exists, 0 otherwise.
    uint32_t seneschal_server_handle;

    // The current status of the VM.
    Status status;
  };

  // Classes that implement this interface *MUST* exit as quickly as possible
  // once their destructor is called.
  virtual ~VmInterface() = default;

  // Shuts down the VM. Returns true if the VM was successfully shut down and
  // false otherwise.
  virtual bool Shutdown() = 0;

  // Information about the VM.
  virtual Info GetInfo() = 0;

  // Attach an usb device at host bus:addr, with vid, pid and an opened fd.
  virtual bool AttachUsbDevice(uint8_t bus,
                               uint8_t addr,
                               uint16_t vid,
                               uint16_t pid,
                               int fd,
                               UsbControlResponse* response) = 0;

  // Detach the usb device at guest port.
  virtual bool DetachUsbDevice(uint8_t port, UsbControlResponse* response) = 0;

  // List all usb devices attached to guest.
  virtual bool ListUsbDevice(std::vector<UsbDevice>* devices) = 0;

  // Handle the device going to suspend.
  virtual void HandleSuspendImminent() = 0;

  // Handle the device resuming from a suspend.
  virtual void HandleSuspendDone() = 0;

  // Update resolv.conf data.
  virtual bool SetResolvConfig(
      const std::vector<std::string>& nameservers,
      const std::vector<std::string>& search_domains) = 0;

  // Set the guest time to the current time as given by gettimeofday.
  virtual bool SetTime(std::string* failure_reason) = 0;

  // Get enterprise reporting information. Also sets the
  // response fields for success and failure_reason.
  virtual bool GetVmEnterpriseReportingInfo(
      GetVmEnterpriseReportingInfoResponse* response) = 0;

  // Notes that TremplinStartedSignal has been received for the VM.
  virtual void SetTremplinStarted() = 0;

  // Notes that guest agent is running in the VM.
  virtual void VmToolsStateChanged(bool running) = 0;

  // Initiate a disk resize operation for the VM.
  // |new_size| is the requested size in bytes.
  virtual vm_tools::concierge::DiskImageStatus ResizeDisk(
      uint64_t new_size, std::string* failure_reason) = 0;

  // Get the status of the most recent ResizeDisk operation.
  virtual vm_tools::concierge::DiskImageStatus GetDiskResizeStatus(
      std::string* failure_reason) = 0;
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_VM_INTERFACE_H_
