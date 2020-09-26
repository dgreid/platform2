// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/vm_base_impl.h"

#include <base/files/file_path.h>
#include <base/files/file_util.h>

#include "vm_tools/concierge/vm_util.h"

namespace vm_tools {
namespace concierge {

VmBaseImpl::VmBaseImpl(
    std::unique_ptr<patchpanel::Client> network_client,
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
    base::FilePath runtime_dir)
    : VmBaseImpl(std::move(network_client),
                 0 /* vsock_cid */,
                 std::move(seneschal_server_proxy),
                 "",
                 std::move(runtime_dir)) {}

VmBaseImpl::VmBaseImpl(
    std::unique_ptr<patchpanel::Client> network_client,
    uint32_t vsock_cid,
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
    std::string cros_vm_socket,
    base::FilePath runtime_dir)
    : network_client_(std::move(network_client)),
      seneschal_server_proxy_(std::move(seneschal_server_proxy)),
      vsock_cid_(vsock_cid),
      cros_vm_socket_(std::move(cros_vm_socket)) {
  // Take ownership of the runtime directory.
  CHECK(base::DirectoryExists(runtime_dir));
  CHECK(runtime_dir_.Set(runtime_dir));
}

bool VmBaseImpl::AttachUsbDevice(uint8_t bus,
                                 uint8_t addr,
                                 uint16_t vid,
                                 uint16_t pid,
                                 int fd,
                                 UsbControlResponse* response) {
  return vm_tools::concierge::AttachUsbDevice(GetVmSocketPath(), bus, addr, vid,
                                              pid, fd, response);
}

bool VmBaseImpl::DetachUsbDevice(uint8_t port, UsbControlResponse* response) {
  return vm_tools::concierge::DetachUsbDevice(GetVmSocketPath(), port,
                                              response);
}

bool VmBaseImpl::ListUsbDevice(std::vector<UsbDevice>* devices) {
  return vm_tools::concierge::ListUsbDevice(GetVmSocketPath(), devices);
}

// static
bool VmBaseImpl::SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state,
                                     const char* CpuCgroup) {
  int cpu_shares = 1024;  // TODO(sonnyrao): Adjust |cpu_shares|.
  switch (cpu_restriction_state) {
    case CPU_RESTRICTION_FOREGROUND:
      break;
    case CPU_RESTRICTION_BACKGROUND:
      cpu_shares = 64;
      break;
    default:
      NOTREACHED();
  }
  return UpdateCpuShares(base::FilePath(CpuCgroup), cpu_shares);
}

bool VmBaseImpl::StartProcess(base::StringPairs args) {
  for (std::pair<std::string, std::string>& arg : args) {
    process_.AddArg(std::move(arg.first));
    if (!arg.second.empty())
      process_.AddArg(std::move(arg.second));
  }

  if (!process_.Start()) {
    PLOG(ERROR) << "Failed to start VM process";
    return false;
  }

  return true;
}

std::string VmBaseImpl::GetVmSocketPath() const {
  return runtime_dir_.GetPath().Append(cros_vm_socket_).value();
}

void VmBaseImpl::RunCrosvmCommand(const std::string& command) const {
  vm_tools::concierge::RunCrosvmCommand(std::move(command), GetVmSocketPath());
}

uint32_t VmBaseImpl::seneschal_server_handle() const {
  if (seneschal_server_proxy_)
    return seneschal_server_proxy_->handle();

  return 0;
}

void VmBaseImpl::HandleSuspendImminent() {
  RunCrosvmCommand("suspend");
}

void VmBaseImpl::HandleSuspendDone() {
  RunCrosvmCommand("resume");
}

}  // namespace concierge
}  // namespace vm_tools
