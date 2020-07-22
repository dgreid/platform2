// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SHARED_DATA_H_
#define VM_TOOLS_CONCIERGE_SHARED_DATA_H_

#include <string>
#include <tuple>

#include <base/optional.h>
#include <base/system/sys_info.h>

#include "vm_tools/concierge/service.h"

namespace vm_tools {
namespace concierge {

// Maximum number of extra disks to be mounted inside the VM.
constexpr int kMaxExtraDisks = 10;

// Cryptohome root base path.
constexpr char kCryptohomeRoot[] = "/run/daemon-store";

// crosvm directory name.
constexpr char kCrosvmDir[] = "crosvm";

// Plugin VM directory name.
constexpr char kPluginVmDir[] = "pvm";

// Path to the runtime directory used by VMs.
constexpr char kRuntimeDir[] = "/run/vm";

// Only allow hex digits in the cryptohome id.
constexpr char kValidCryptoHomeCharacters[] = "abcdefABCDEF0123456789";

// Gets the path to the file given the name, user id, location, and extension.
base::Optional<base::FilePath> GetFilePathFromName(
    const std::string& cryptohome_id,
    const std::string& vm_name,
    StorageLocation storage_location,
    const std::string& extension,
    bool create_parent_dir);

bool GetPluginDirectory(const base::FilePath& prefix,
                        const std::string& extension,
                        const std::string& vm_id,
                        bool create,
                        base::FilePath* path_out);

bool GetPluginIsoDirectory(const std::string& vm_id,
                           const std::string& cryptohome_id,
                           bool create,
                           base::FilePath* path_out);

template <class StartXXRequest>
base::Optional<std::tuple<StartXXRequest, StartVmResponse>>
Service::StartVmHelper(dbus::MethodCall* method_call,
                       dbus::MessageReader* reader,
                       dbus::MessageWriter* writer,
                       bool allow_zero_cpus) {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  StartXXRequest request;
  StartVmResponse response;
  // We change to a success status later if necessary.
  response.set_status(VM_STATUS_FAILURE);

  if (!reader->PopArrayOfBytesAsProto(&request)) {
    LOG(ERROR) << "Unable to parse StartVmRequest from message";
    response.set_failure_reason("Unable to parse protobuf");
    writer->AppendProtoAsArrayOfBytes(response);
    return base::nullopt;
  }

  // Check the CPU count.
  if ((request.cpus() == 0 && !allow_zero_cpus) ||
      request.cpus() > base::SysInfo::NumberOfProcessors()) {
    LOG(ERROR) << "Invalid number of CPUs: " << request.cpus();
    response.set_failure_reason("Invalid CPU count");
    writer->AppendProtoAsArrayOfBytes(response);
    return base::nullopt;
  }

  // Make sure the VM has a name.
  if (request.name().empty()) {
    LOG(ERROR) << "Ignoring request with empty name";
    response.set_failure_reason("Missing VM name");
    writer->AppendProtoAsArrayOfBytes(response);
    return base::nullopt;
  }

  auto iter = FindVm(request.owner_id(), request.name());
  if (iter != vms_.end()) {
    LOG(INFO) << "VM with requested name is already running";

    VmInterface::Info vm = iter->second->GetInfo();

    VmInfo* vm_info = response.mutable_vm_info();
    vm_info->set_ipv4_address(vm.ipv4_address);
    vm_info->set_pid(vm.pid);
    vm_info->set_cid(vm.cid);
    vm_info->set_seneschal_server_handle(vm.seneschal_server_handle);
    switch (vm.status) {
      case VmInterface::Status::STARTING: {
        response.set_status(VM_STATUS_STARTING);
        break;
      }
      case VmInterface::Status::RUNNING: {
        response.set_status(VM_STATUS_RUNNING);
        break;
      }
      default: {
        response.set_status(VM_STATUS_UNKNOWN);
        break;
      }
    }
    response.set_success(true);

    writer->AppendProtoAsArrayOfBytes(response);
    return base::nullopt;
  }

  return std::make_tuple(request, response);
}

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_SHARED_DATA_H_
