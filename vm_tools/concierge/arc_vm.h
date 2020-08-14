// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_ARC_VM_H_
#define VM_TOOLS_CONCIERGE_ARC_VM_H_

#include <stdint.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <chromeos/patchpanel/mac_address_generator.h>
#include <vm_concierge/proto_bindings/concierge_service.pb.h>

#include "vm_tools/concierge/seneschal_server_proxy.h"
#include "vm_tools/concierge/vm_base_impl.h"
#include "vm_tools/concierge/vsock_cid_pool.h"

namespace vm_tools {
namespace concierge {

struct ArcVmFeatures {
  // Whether the guest kernel root file system is writable.
  bool rootfs_writable;

  // Use development configuration directives in the started VM.
  bool use_dev_conf;
};

// Represents a single instance of a running termina VM.
class ArcVm final : public VmBaseImpl {
 public:
  // Describes a disk image to be mounted inside the VM.
  struct Disk {
    // Path to the disk image on the host.
    base::FilePath path;

    // Whether the disk should be writable by the VM.
    bool writable;
  };

  // Starts a new virtual machine.  Returns nullptr if the virtual machine
  // failed to start for any reason.
  static std::unique_ptr<ArcVm> Create(
      base::FilePath kernel,
      base::FilePath rootfs,
      base::FilePath fstab,
      uint32_t cpus,
      base::FilePath pstore_path,
      uint32_t pstore_size,
      std::vector<Disk> disks,
      uint32_t vsock_cid,
      base::FilePath data_dir,
      std::unique_ptr<patchpanel::Client> network_client,
      std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
      base::FilePath runtime_dir,
      ArcVmFeatures features,
      std::vector<std::string> params);
  ~ArcVm() override;

  // The pid of the child process.
  pid_t pid() { return process_.pid(); }

  // The VM's cid.
  uint32_t cid() const { return vsock_cid_; }

  // ArcVmFeatures settings.
  bool rootfs_writable() const { return features_.rootfs_writable; }
  bool use_dev_conf() const { return features_.use_dev_conf; }

  // The 9p server managed by seneschal that provides access to shared files for
  // this VM.  Returns 0 if there is no seneschal server associated with this
  // VM.
  uint32_t seneschal_server_handle() const {
    return seneschal_server_proxy_ ? seneschal_server_proxy_->handle() : 0;
  }

  // The IPv4 address of the VM in network byte order.
  uint32_t IPv4Address() const;

  // VmInterface overrides.
  bool Shutdown() override;
  VmInterface::Info GetInfo() override;
  // Currently only implemented for termina, returns "Not implemented".
  bool GetVmEnterpriseReportingInfo(
      GetVmEnterpriseReportingInfoResponse* response) override;
  bool AttachUsbDevice(uint8_t bus,
                       uint8_t addr,
                       uint16_t vid,
                       uint16_t pid,
                       int fd,
                       UsbControlResponse* response) override;
  bool DetachUsbDevice(uint8_t port, UsbControlResponse* response) override;
  bool ListUsbDevice(std::vector<UsbDevice>* devices) override;
  bool UsesExternalSuspendSignals() override { return true; }
  bool SetResolvConfig(
      const std::vector<std::string>& nameservers,
      const std::vector<std::string>& search_domains) override {
    return true;
  }
  // TODO(b/136143058): Implement SetTime calls.
  bool SetTime(std::string* failure_reason) override { return true; }
  void SetTremplinStarted() override { NOTREACHED(); }
  void VmToolsStateChanged(bool running) override { NOTREACHED(); }
  vm_tools::concierge::DiskImageStatus ResizeDisk(
      uint64_t new_size, std::string* failure_reason) override;
  vm_tools::concierge::DiskImageStatus GetDiskResizeStatus(
      std::string* failure_reason) override;

  // Adjusts the amount of CPU the ARCVM processes are allowed to use.
  static bool SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state);

 private:
  ArcVm(int32_t vsock_cid,
        std::unique_ptr<patchpanel::Client> network_client,
        std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
        base::FilePath runtime_dir,
        ArcVmFeatures features);

  void HandleSuspendImminent() override;
  void HandleSuspendDone() override;

  // Returns the path to the VM control socket.
  std::string GetVmSocketPath() const;

  // Starts the VM with the given kernel and root file system.
  bool Start(base::FilePath kernel,
             base::FilePath rootfs,
             base::FilePath fstab,
             uint32_t cpus,
             base::FilePath pstore_path,
             uint32_t pstore_size,
             std::vector<Disk> disks,
             base::FilePath data_dir,
             std::vector<std::string> params);

  // Virtual socket context id to be used when communicating with this VM.
  uint32_t vsock_cid_;

  std::vector<patchpanel::NetworkDevice> network_devices_;

  // Proxy to the server providing shared directory access for this VM.
  std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy_;

  // Flags passed to vmc start.
  ArcVmFeatures features_;

  DISALLOW_COPY_AND_ASSIGN(ArcVm);
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_ARC_VM_H_
