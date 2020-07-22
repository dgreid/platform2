// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/files/file_util.h>

#include "vm_tools/concierge/arc_vm.h"
#include "vm_tools/concierge/service.h"
#include "vm_tools/concierge/shared_data.h"

namespace vm_tools {
namespace concierge {

namespace {

// Android data directory.
constexpr const char kAndroidDataDir[] = "/run/arcvm/android-data/data";

// File extension for pstore backend file
constexpr char kPstoreExtension[] = ".pstore";

}  // namespace

std::unique_ptr<dbus::Response> Service::StartArcVm(
    dbus::MethodCall* method_call) {
  LOG(INFO) << "Received StartArcVm request";
  std::unique_ptr<dbus::Response> dbus_response(
      dbus::Response::FromMethodCall(method_call));
  dbus::MessageReader reader(method_call);
  dbus::MessageWriter writer(dbus_response.get());
  StartArcVmRequest request;
  StartVmResponse response;
  auto helper_result = StartVmHelper<StartArcVmRequest>(
      method_call, &reader, &writer, true /* allow_zero_cpus */);
  if (!helper_result) {
    return dbus_response;
  }
  std::tie(request, response) = *helper_result;

  if (request.disks_size() > kMaxExtraDisks) {
    LOG(ERROR) << "Rejecting request with " << request.disks_size()
               << " extra disks";

    response.set_failure_reason("Too many extra disks");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  const base::FilePath kernel(request.vm().kernel());
  const base::FilePath rootfs(request.vm().rootfs());
  const base::FilePath fstab(request.fstab());

  if (!base::PathExists(kernel)) {
    LOG(ERROR) << "Missing VM kernel path: " << kernel.value();

    response.set_failure_reason("Kernel path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!base::PathExists(rootfs)) {
    LOG(ERROR) << "Missing VM rootfs path: " << rootfs.value();

    response.set_failure_reason("Rootfs path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  if (!base::PathExists(fstab)) {
    LOG(ERROR) << "Missing VM fstab path: " << fstab.value();

    response.set_failure_reason("Fstab path does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::vector<ArcVm::Disk> disks;
  for (const auto& disk : request.disks()) {
    if (!base::PathExists(base::FilePath(disk.path()))) {
      LOG(ERROR) << "Missing disk path: " << disk.path();
      response.set_failure_reason("One or more disk paths do not exist");
      writer.AppendProtoAsArrayOfBytes(response);
      return dbus_response;
    }
    disks.push_back(ArcVm::Disk{
        .path = base::FilePath(disk.path()),
        .writable = disk.writable(),
    });
  }

  // Create the runtime directory.
  base::FilePath runtime_dir;
  if (!base::CreateTemporaryDirInDir(base::FilePath(kRuntimeDir), "vm.",
                                     &runtime_dir)) {
    PLOG(ERROR) << "Unable to create runtime directory for VM";

    response.set_failure_reason(
        "Internal error: unable to create runtime directory");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Allocate resources for the VM.
  uint32_t vsock_cid = vsock_cid_pool_.Allocate();
  if (vsock_cid == 0) {
    LOG(ERROR) << "Unable to allocate vsock context id";

    response.set_failure_reason("Unable to allocate vsock cid");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  std::unique_ptr<patchpanel::Client> network_client =
      patchpanel::Client::New();
  if (!network_client) {
    LOG(ERROR) << "Unable to open networking service client";

    response.set_failure_reason("Unable to open network service client");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // Map the chronos user (1000) and the chronos-access group (1001) to the
  // AID_EXTERNAL_STORAGE user and group (1077).
  uint32_t seneschal_server_port = next_seneschal_server_port_++;
  std::unique_ptr<SeneschalServerProxy> server_proxy =
      SeneschalServerProxy::CreateVsockProxy(seneschal_service_proxy_,
                                             seneschal_server_port, vsock_cid,
                                             {{1000, 1077}}, {{1001, 1077}});
  if (!server_proxy) {
    LOG(ERROR) << "Unable to start shared directory server";

    response.set_failure_reason("Unable to start shared directory server");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  uint32_t seneschal_server_handle = server_proxy->handle();

  // Build the plugin params.
  std::vector<std::string> params(
      std::make_move_iterator(request.mutable_params()->begin()),
      std::make_move_iterator(request.mutable_params()->end()));
  params.emplace_back(base::StringPrintf("androidboot.seneschal_server_port=%d",
                                         seneschal_server_port));

  // Start the VM and build the response.
  ArcVmFeatures features;
  features.rootfs_writable = request.rootfs_writable();

  const auto pstore_path = GetFilePathFromName(
      request.owner_id(), request.name(), STORAGE_CRYPTOHOME_ROOT,
      kPstoreExtension, true /* create_parent_dir */);
  if (!pstore_path) {
    LOG(ERROR) << "Failed to get pstore path";

    response.set_failure_reason("Failed to get pstore path");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }
  const uint32_t pstore_size = 1024 * 1024;

  base::FilePath data_dir = base::FilePath(kAndroidDataDir);
  if (!base::PathExists(data_dir)) {
    LOG(WARNING) << "Android data directory does not exist";

    response.set_failure_reason("Android data directory does not exist");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  auto vm = ArcVm::Create(
      std::move(kernel), std::move(rootfs), std::move(fstab), request.cpus(),
      std::move(*pstore_path), pstore_size, std::move(disks), vsock_cid,
      std::move(data_dir), std::move(network_client), std::move(server_proxy),
      std::move(runtime_dir), features, std::move(params));
  if (!vm) {
    LOG(ERROR) << "Unable to start VM";

    response.set_failure_reason("Unable to start VM");
    writer.AppendProtoAsArrayOfBytes(response);
    return dbus_response;
  }

  // ARCVM is ready.
  LOG(INFO) << "Started VM with pid " << vm->pid();

  VmInfo* vm_info = response.mutable_vm_info();
  response.set_success(true);
  response.set_status(VM_STATUS_RUNNING);
  vm_info->set_ipv4_address(vm->IPv4Address());
  vm_info->set_pid(vm->pid());
  vm_info->set_cid(vsock_cid);
  vm_info->set_seneschal_server_handle(seneschal_server_handle);
  writer.AppendProtoAsArrayOfBytes(response);

  VmId vm_id(request.owner_id(), request.name());
  SendVmStartedSignal(vm_id, *vm_info, response.status());

  vms_[vm_id] = std::move(vm);
  return dbus_response;
}

}  // namespace concierge
}  // namespace vm_tools
