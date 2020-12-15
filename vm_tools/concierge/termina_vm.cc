// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/termina_vm.h"

#include <arpa/inet.h>
#include <linux/capability.h>
#include <signal.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <unistd.h>

#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/guid.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/system/sys_info.h>
#include <base/time/time.h>
#include <google/protobuf/repeated_field.h>
#include <grpcpp/grpcpp.h>
#include <chromeos/constants/vm_tools.h>

#include "vm_tools/concierge/tap_device_builder.h"
#include "vm_tools/concierge/vm_builder.h"
#include "vm_tools/concierge/vm_util.h"

using std::string;

namespace vm_tools {
namespace concierge {
namespace {

// Features to enable.
constexpr StartTerminaRequest_Feature kEnabledTerminaFeatures[] = {
    StartTerminaRequest_Feature_START_LXD,
    StartTerminaRequest_Feature_RESET_LXD_ON_LAUNCH,
};

// Name of the control socket used for controlling crosvm.
constexpr char kCrosvmSocket[] = "crosvm.sock";

// Path to the wayland socket.
constexpr char kWaylandSocket[] = "/run/chrome/wayland-0";

// How long to wait before timing out on shutdown RPCs.
constexpr int64_t kShutdownTimeoutSeconds = 30;

// How long to wait before timing out on StartTermina RPCs.
constexpr int64_t kStartTerminaTimeoutSeconds = 150;

// How long to wait before timing out on regular RPCs.
constexpr int64_t kDefaultTimeoutSeconds = 10;

// How long to wait before timing out on child process exits.
constexpr base::TimeDelta kChildExitTimeout = base::TimeDelta::FromSeconds(10);

// Offset in a subnet of the gateway/host.
constexpr size_t kHostAddressOffset = 0;

// Offset in a subnet of the client/guest.
constexpr size_t kGuestAddressOffset = 1;

// The CPU cgroup where all the Termina crosvm processes should belong to.
constexpr char kTerminaCpuCgroup[] = "/sys/fs/cgroup/cpu/vms/termina";

// The maximum GPU shader cache disk usage, interpreted by Mesa. For details
// see MESA_GLSL_CACHE_MAX_SIZE at https://docs.mesa3d.org/envvars.html.
constexpr char kGpuCacheSizeString[] = "50M";

// Special value to represent an invalid disk index for `crosvm disk`
// operations.
constexpr int kInvalidDiskIndex = -1;

std::unique_ptr<patchpanel::Subnet> MakeSubnet(
    const patchpanel::IPv4Subnet& subnet) {
  return std::make_unique<patchpanel::Subnet>(
      subnet.base_addr(), subnet.prefix_len(), base::DoNothing());
}

}  // namespace

TerminaVm::TerminaVm(
    uint32_t vsock_cid,
    std::unique_ptr<patchpanel::Client> network_client,
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
    base::FilePath runtime_dir,
    base::FilePath log_path,
    base::FilePath gpu_cache_path,
    std::string stateful_device,
    uint64_t stateful_size,
    VmFeatures features,
    bool is_termina)
    : VmBaseImpl(std::move(network_client),
                 vsock_cid,
                 std::move(seneschal_server_proxy),
                 kCrosvmSocket,
                 std::move(runtime_dir)),
      features_(features),
      stateful_device_(stateful_device),
      stateful_size_(stateful_size),
      stateful_resize_type_(DiskResizeType::NONE),
      log_path_(std::move(log_path)),
      gpu_cache_path_(std::move(gpu_cache_path)),
      is_termina_(is_termina) {}

// For testing.
TerminaVm::TerminaVm(
    std::unique_ptr<patchpanel::Subnet> subnet,
    uint32_t vsock_cid,
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
    base::FilePath runtime_dir,
    base::FilePath log_path,
    base::FilePath gpu_cache_path,
    std::string stateful_device,
    uint64_t stateful_size,
    VmFeatures features,
    bool is_termina)
    : VmBaseImpl(nullptr /* network_client */,
                 vsock_cid,
                 std::move(seneschal_server_proxy),
                 "" /* cros_vm_socket */,
                 std::move(runtime_dir)),
      subnet_(std::move(subnet)),
      features_(features),
      stateful_device_(stateful_device),
      stateful_size_(stateful_size),
      stateful_resize_type_(DiskResizeType::NONE),
      log_path_(std::move(log_path)),
      gpu_cache_path_(std::move(gpu_cache_path)),
      is_termina_(is_termina) {
  CHECK(subnet_);
}

TerminaVm::~TerminaVm() {
  Shutdown();
}

std::unique_ptr<TerminaVm> TerminaVm::Create(
    uint32_t vsock_cid,
    std::unique_ptr<patchpanel::Client> network_client,
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
    base::FilePath runtime_dir,
    base::FilePath log_path,
    base::FilePath gpu_cache_path,
    std::string stateful_device,
    uint64_t stateful_size,
    VmFeatures features,
    bool is_termina,
    VmBuilder vm_builder) {
  auto vm = base::WrapUnique(new TerminaVm(
      vsock_cid, std::move(network_client), std::move(seneschal_server_proxy),
      std::move(runtime_dir), std::move(log_path), std::move(gpu_cache_path),
      std::move(stateful_device), std::move(stateful_size), features,
      is_termina));

  if (!vm->Start(std::move(vm_builder)))
    vm.reset();

  return vm;
}

std::string TerminaVm::GetVmSocketPath() const {
  return runtime_dir_.GetPath().Append(kCrosvmSocket).value();
}

std::string TerminaVm::GetCrosVmSerial(std::string hardware,
                                       std::string console_type) const {
  std::string common_params =
      "hardware=" + hardware + ",num=1," + console_type + "=true";
  if (log_path_.empty()) {
    return common_params + ",type=syslog";
  }
  return common_params + ",type=unix,path=" + log_path_.value();
}

bool TerminaVm::Start(VmBuilder vm_builder) {
  // Get the network interface.
  patchpanel::IPv4Subnet container_subnet;
  if (!network_client_->NotifyTerminaVmStartup(vsock_cid_, &network_device_,
                                               &container_subnet)) {
    LOG(ERROR) << "No network devices available";
    return false;
  }
  subnet_ = MakeSubnet(network_device_.ipv4_subnet());
  container_subnet_ = MakeSubnet(container_subnet);

  // Open the tap device.
  base::ScopedFD tap_fd = OpenTapDevice(
      network_device_.ifname(), true /*vnet_hdr*/, nullptr /*ifname_out*/);
  if (!tap_fd.is_valid()) {
    LOG(ERROR) << "Unable to open and configure TAP device "
               << network_device_.ifname();
    return false;
  }

  vm_builder.AppendTapFd(std::move(tap_fd))
      .AppendWaylandSocket(kWaylandSocket)
      .SetVsockCid(vsock_cid_)
      .SetSocketPath(GetVmSocketPath())
      .SetMemory(GetVmMemoryMiB())
      .AppendSerialDevice(GetCrosVmSerial("serial", "earlycon"))
      .AppendSerialDevice(GetCrosVmSerial("virtio-console", "console"))
      .SetSyslogTag(base::StringPrintf("VM(%u)", vsock_cid_));

  if (USE_CROSVM_WL_DMABUF)
    vm_builder.EnableWaylandDmaBuf(true /* enable */);

  if (features_.gpu) {
    std::string gpu_arg = "--gpu";
    if (!gpu_cache_path_.empty()) {
      gpu_arg += "=cache-path=" + gpu_cache_path_.value();
      gpu_arg += ",cache-size=";
      gpu_arg += kGpuCacheSizeString;
    }
    vm_builder.EnableGpu(true /* enable */, gpu_arg);
  }

  if (features_.software_tpm)
    vm_builder.EnableSoftwareTpm(true /* enable */);

  if (features_.audio_capture) {
    vm_builder.AppendAudioDevice("backend=cras,capture=true");
  } else {
    vm_builder.AppendAudioDevice("backend=cras");
  }

  // Change the process group before exec so that crosvm sending SIGKILL to the
  // whole process group doesn't kill us as well. The function also changes the
  // cpu cgroup for Termina crosvm processes.
  process_.SetPreExecCallback(base::Bind(
      &SetUpCrosvmProcess, base::FilePath(kTerminaCpuCgroup).Append("tasks")));

  if (!StartProcess(vm_builder.BuildVmArgs()))
    return false;

  // Create a stub for talking to the maitre'd instance inside the VM.
  stub_ = std::make_unique<vm_tools::Maitred::Stub>(grpc::CreateChannel(
      base::StringPrintf("vsock:%u:%u", vsock_cid_, vm_tools::kMaitredPort),
      grpc::InsecureChannelCredentials()));

  return true;
}

bool TerminaVm::Shutdown() {
  // Notify arc-patchpanel that the VM is down.
  // This should run before the process existence check below since we still
  // want to release the network resources on crash.
  // Note the client will only be null during testing.
  if (network_client_ &&
      !network_client_->NotifyTerminaVmShutdown(vsock_cid_)) {
    LOG(WARNING) << "Unable to notify networking services";
  }

  // Do a check here to make sure the process is still around.  It may have
  // crashed and we don't want to be waiting around for an RPC response that's
  // never going to come.  kill with a signal value of 0 is explicitly
  // documented as a way to check for the existence of a process.
  if (!CheckProcessExists(process_.pid())) {
    // The process is already gone.
    process_.Release();
    return true;
  }

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kShutdownTimeoutSeconds, GPR_TIMESPAN)));

  vm_tools::EmptyMessage empty;
  grpc::Status status = stub_->Shutdown(&ctx, empty, &empty);

  // brillo::ProcessImpl doesn't provide a timed wait function and while the
  // Shutdown RPC may have been successful we can't really trust crosvm to
  // actually exit.  This may result in an untimed wait() blocking indefinitely.
  // Instead, do a timed wait here and only return success if the process
  // _actually_ exited as reported by the kernel, which is really the only
  // thing we can trust here.
  if (status.ok() && WaitForChild(process_.pid(), kChildExitTimeout)) {
    process_.Release();
    return true;
  }

  LOG(WARNING) << "Shutdown RPC failed for VM " << vsock_cid_ << " with error "
               << "code " << status.error_code() << ": "
               << status.error_message();

  // Try to shut it down via the crosvm socket.
  RunCrosvmCommand("stop");

  // We can't actually trust the exit codes that crosvm gives us so just see if
  // it exited.
  if (WaitForChild(process_.pid(), kChildExitTimeout)) {
    process_.Release();
    return true;
  }

  LOG(WARNING) << "Failed to stop VM " << vsock_cid_ << " via crosvm socket";

  // Kill the process with SIGTERM.
  if (process_.Kill(SIGTERM, kChildExitTimeout.InSeconds())) {
    return true;
  }

  LOG(WARNING) << "Failed to kill VM " << vsock_cid_ << " with SIGTERM";

  // Kill it with fire.
  if (process_.Kill(SIGKILL, kChildExitTimeout.InSeconds())) {
    return true;
  }

  LOG(ERROR) << "Failed to kill VM " << vsock_cid_ << " with SIGKILL";
  return false;
}

bool TerminaVm::ConfigureNetwork(const std::vector<string>& nameservers,
                                 const std::vector<string>& search_domains) {
  LOG(INFO) << "Configuring network for VM " << vsock_cid_;

  vm_tools::NetworkConfigRequest request;
  vm_tools::EmptyMessage response;

  vm_tools::IPv4Config* config = request.mutable_ipv4_config();
  config->set_address(IPv4Address());
  config->set_gateway(GatewayAddress());
  config->set_netmask(Netmask());

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->ConfigureNetwork(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to configure network for VM " << vsock_cid_ << ": "
               << status.error_message();
    return false;
  }

  return SetResolvConfig(nameservers, search_domains);
}

bool TerminaVm::ConfigureContainerGuest(const std::string& vm_token,
                                        std::string* out_error) {
  LOG(INFO) << "Configuring container guest for for VM " << vsock_cid_;

  vm_tools::ConfigureContainerGuestRequest request;
  vm_tools::EmptyMessage response;

  request.set_container_token(vm_token);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status =
      stub_->ConfigureContainerGuest(&ctx, request, &response);
  if (!status.ok()) {
    *out_error = status.error_message();
    return false;
  }

  return true;
}

void TerminaVm::RunCrosvmCommand(string command) {
  vm_tools::concierge::RunCrosvmCommand(std::move(command), GetVmSocketPath());
}

bool TerminaVm::Mount(string source,
                      string target,
                      string fstype,
                      uint64_t mountflags,
                      string options) {
  LOG(INFO) << "Mounting " << source << " on " << target << " inside VM "
            << vsock_cid_;

  vm_tools::MountRequest request;
  vm_tools::MountResponse response;

  request.mutable_source()->swap(source);
  request.mutable_target()->swap(target);
  request.mutable_fstype()->swap(fstype);
  request.set_mountflags(mountflags);
  request.mutable_options()->swap(options);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->Mount(&ctx, request, &response);
  if (!status.ok() || response.error() != 0) {
    LOG(ERROR) << "Failed to mount " << request.source() << " on "
               << request.target() << " inside VM " << vsock_cid_ << ": "
               << (status.ok() ? strerror(response.error())
                               : status.error_message());
    return false;
  }

  return true;
}

bool TerminaVm::StartTermina(std::string lxd_subnet,
                             bool allow_privileged_containers,
                             std::string* out_error,
                             vm_tools::StartTerminaResponse* response) {
  DCHECK(out_error);
  DCHECK(response);

  // We record the kernel version early to ensure that no container has
  // been started and the VM can still be trusted.
  RecordKernelVersionForEnterpriseReporting();

  vm_tools::StartTerminaRequest request;

  request.set_tremplin_ipv4_address(GatewayAddress());
  request.mutable_lxd_ipv4_subnet()->swap(lxd_subnet);
  request.set_stateful_device(StatefulDevice());
  request.set_allow_privileged_containers(allow_privileged_containers);
  for (const auto feature : kEnabledTerminaFeatures) {
    request.add_feature(feature);
  }

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kStartTerminaTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->StartTermina(&ctx, request, response);

  if (!status.ok()) {
    LOG(ERROR) << "Failed to start Termina: " << status.error_message();
    out_error->assign(status.error_message());
    return false;
  }

  return true;
}

void TerminaVm::RecordKernelVersionForEnterpriseReporting() {
  grpc::ClientContext ctx_get_kernel_version;
  ctx_get_kernel_version.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kStartTerminaTimeoutSeconds, GPR_TIMESPAN)));
  vm_tools::EmptyMessage empty;
  vm_tools::GetKernelVersionResponse grpc_response;
  grpc::Status get_kernel_version_status =
      stub_->GetKernelVersion(&ctx_get_kernel_version, empty, &grpc_response);
  if (!get_kernel_version_status.ok()) {
    LOG(WARNING) << "Failed to retrieve kernel version for VM " << vsock_cid_
                 << ": " << get_kernel_version_status.error_message();
  } else {
    kernel_version_ =
        grpc_response.kernel_release() + " " + grpc_response.kernel_version();
  }
}

bool TerminaVm::AttachUsbDevice(uint8_t bus,
                                uint8_t addr,
                                uint16_t vid,
                                uint16_t pid,
                                int fd,
                                UsbControlResponse* response) {
  return vm_tools::concierge::AttachUsbDevice(GetVmSocketPath(), bus, addr, vid,
                                              pid, fd, response);
}

bool TerminaVm::DetachUsbDevice(uint8_t port, UsbControlResponse* response) {
  return vm_tools::concierge::DetachUsbDevice(GetVmSocketPath(), port,
                                              response);
}

bool TerminaVm::ListUsbDevice(std::vector<UsbDevice>* device) {
  return vm_tools::concierge::ListUsbDevice(GetVmSocketPath(), device);
}

void TerminaVm::HandleSuspendImminent() {
  LOG(INFO) << "Preparing to suspend";

  vm_tools::EmptyMessage request;
  vm_tools::EmptyMessage response;

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->PrepareToSuspend(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to prepare for suspending" << status.error_message();
  }

  RunCrosvmCommand("suspend");
}

void TerminaVm::HandleSuspendDone() {
  RunCrosvmCommand("resume");
}

bool TerminaVm::Mount9P(uint32_t port, string target) {
  LOG(INFO) << "Mounting 9P file system from port " << port << " on " << target;

  vm_tools::Mount9PRequest request;
  vm_tools::MountResponse response;

  request.set_port(port);
  request.set_target(std::move(target));

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->Mount9P(&ctx, request, &response);
  if (!status.ok() || response.error() != 0) {
    LOG(ERROR) << "Failed to mount 9P server on " << request.target()
               << " inside VM " << vsock_cid_ << ": "
               << (status.ok() ? strerror(response.error())
                               : status.error_message());
    return false;
  }

  return true;
}

bool TerminaVm::MountExternalDisk(string source, std::string target_dir) {
  const string target = "/mnt/external/" + target_dir;

  LOG(INFO) << "Mounting an external disk on " << target;

  vm_tools::MountRequest request;
  vm_tools::MountResponse response;

  request.set_source(std::move(source));
  request.set_target(std::move(target));
  request.set_fstype("btrfs");
  request.set_options("");
  request.set_create_target(true);
  request.set_permissions(0777);
  request.set_mkfs_if_needed(true);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->Mount(&ctx, request, &response);
  if (!status.ok() || response.error() != 0) {
    LOG(ERROR) << "Failed to mount an external disk " << request.source()
               << " on " << request.target() << " inside VM " << vsock_cid_
               << ": "
               << (status.ok() ? strerror(response.error())
                               : status.error_message());
    return false;
  }

  return true;
}

bool TerminaVm::SetResolvConfig(const std::vector<string>& nameservers,
                                const std::vector<string>& search_domains) {
  LOG(INFO) << "Setting resolv config for VM " << vsock_cid_;

  vm_tools::SetResolvConfigRequest request;
  vm_tools::EmptyMessage response;

  vm_tools::ResolvConfig* resolv_config = request.mutable_resolv_config();

  google::protobuf::RepeatedPtrField<string> request_nameservers(
      nameservers.begin(), nameservers.end());
  resolv_config->mutable_nameservers()->Swap(&request_nameservers);

  google::protobuf::RepeatedPtrField<string> request_search_domains(
      search_domains.begin(), search_domains.end());
  resolv_config->mutable_search_domains()->Swap(&request_search_domains);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->SetResolvConfig(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to set resolv config for VM " << vsock_cid_ << ": "
               << status.error_message();
    return false;
  }

  return true;
}

void TerminaVm::HostNetworkChanged() {
  LOG(INFO) << "Sending OnHostNetworkChanged for VM " << vsock_cid_;

  vm_tools::EmptyMessage request;
  vm_tools::EmptyMessage response;

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->OnHostNetworkChanged(&ctx, request, &response);
  if (!status.ok()) {
    LOG(WARNING) << "Failed to send OnHostNetworkChanged for VM " << vsock_cid_
                 << ": " << status.error_message();
  }
}

bool TerminaVm::SetTime(string* failure_reason) {
  DCHECK(failure_reason);

  base::Time now = base::Time::Now();
  struct timeval current = now.ToTimeVal();

  vm_tools::SetTimeRequest request;
  vm_tools::EmptyMessage response;

  google::protobuf::Timestamp* timestamp = request.mutable_time();
  timestamp->set_seconds(current.tv_sec);
  timestamp->set_nanos(current.tv_usec * 1000);

  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  grpc::Status status = stub_->SetTime(&ctx, request, &response);
  if (!status.ok()) {
    LOG(ERROR) << "Failed to set guest time on VM " << vsock_cid_ << ":"
               << status.error_message();

    *failure_reason = status.error_message();
    return false;
  }
  return true;
}

bool TerminaVm::GetVmEnterpriseReportingInfo(
    GetVmEnterpriseReportingInfoResponse* response) {
  LOG(INFO) << "Get enterprise reporting info";
  if (kernel_version_.empty()) {
    response->set_success(false);
    response->set_failure_reason(
        "Kernel version could not be recorded at startup.");
    return false;
  }

  response->set_success(true);
  response->set_vm_kernel_version(kernel_version_);
  return true;
}

// static
bool TerminaVm::SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state) {
  return VmBaseImpl::SetVmCpuRestriction(cpu_restriction_state,
                                         kTerminaCpuCgroup);
}

// Extract the disk index of a virtio-blk device name.
// |name| should match "/dev/vdX", where X is in the range 'a' to 'z'.
// Returns the zero-based index of the disk (e.g. 'a' = 0, 'b' = 1, etc.).
static int DiskIndexFromName(const std::string& name) {
  // TODO(dverkamp): handle more than 26 disks? (e.g. /dev/vdaa)
  if (name.length() != 8) {
    return kInvalidDiskIndex;
  }

  int disk_letter = name[7];
  if (disk_letter < 'a' || disk_letter > 'z') {
    return kInvalidDiskIndex;
  }

  return disk_letter - 'a';
}

bool TerminaVm::ResizeDiskImage(uint64_t new_size) {
  auto disk_index = DiskIndexFromName(stateful_device_);
  if (disk_index == kInvalidDiskIndex) {
    LOG(ERROR) << "Could not determine disk index from stateful device name "
               << stateful_device_;
    return false;
  }
  return CrosvmDiskResize(GetVmSocketPath(), disk_index, new_size);
}

bool TerminaVm::ResizeFilesystem(uint64_t new_size) {
  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  vm_tools::ResizeFilesystemRequest request;
  vm_tools::ResizeFilesystemResponse response;
  request.set_size(new_size);
  grpc::Status status = stub_->ResizeFilesystem(&ctx, request, &response);
  return status.ok();
}

vm_tools::concierge::DiskImageStatus TerminaVm::ResizeDisk(
    uint64_t new_size, std::string* failure_reason) {
  if (stateful_resize_type_ != DiskResizeType::NONE) {
    LOG(ERROR) << "Attempted resize while resize is already in progress";
    *failure_reason = "Resize already in progress";
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
    return last_stateful_resize_status_;
  }

  LOG(INFO) << "TerminaVm resize request: current size = " << stateful_size_
            << " new size = " << new_size;

  if (new_size == stateful_size_) {
    LOG(INFO) << "Disk is already requested size";
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_RESIZED;
    return last_stateful_resize_status_;
  }

  stateful_target_size_ = new_size;

  if (new_size > stateful_size_) {
    LOG(INFO) << "Expanding disk";
    // Expand disk image first, then expand filesystem.
    if (!ResizeDiskImage(new_size)) {
      LOG(ERROR) << "ResizeDiskImage failed";
      *failure_reason = "ResizeDiskImage failed";
      last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
      return last_stateful_resize_status_;
    }

    if (!ResizeFilesystem(new_size)) {
      LOG(ERROR) << "ResizeFilesystem failed";
      *failure_reason = "ResizeFilesystem failed";
      last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
      return last_stateful_resize_status_;
    }

    LOG(INFO) << "ResizeFilesystem in progress";
    stateful_resize_type_ = DiskResizeType::EXPAND;
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_IN_PROGRESS;
    return last_stateful_resize_status_;
  } else {
    DCHECK(new_size < stateful_size_);

    LOG(INFO) << "Shrinking disk";

    // Shrink filesystem first, then shrink disk image.
    if (!ResizeFilesystem(new_size)) {
      LOG(ERROR) << "ResizeFilesystem failed";
      *failure_reason = "ResizeFilesystem failed";
      last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
      return last_stateful_resize_status_;
    }

    LOG(INFO) << "ResizeFilesystem in progress";
    stateful_resize_type_ = DiskResizeType::SHRINK;
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_IN_PROGRESS;
    return last_stateful_resize_status_;
  }
}

vm_tools::concierge::DiskImageStatus TerminaVm::GetDiskResizeStatus(
    std::string* failure_reason) {
  if (stateful_resize_type_ == DiskResizeType::NONE) {
    return last_stateful_resize_status_;
  }

  // If a resize is in progress, then we must be waiting on filesystem resize to
  // complete. Check its status and update our state to match.
  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  vm_tools::EmptyMessage request;
  vm_tools::GetResizeStatusResponse response;

  grpc::Status status = stub_->GetResizeStatus(&ctx, request, &response);

  if (!status.ok()) {
    stateful_resize_type_ = DiskResizeType::NONE;
    LOG(ERROR) << "GetResizeStatus RPC failed";
    *failure_reason = "GetResizeStatus RPC failed";
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
    return last_stateful_resize_status_;
  }

  if (response.resize_in_progress()) {
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_IN_PROGRESS;
    return last_stateful_resize_status_;
  }

  if (response.current_size() != stateful_target_size_) {
    stateful_resize_type_ = DiskResizeType::NONE;
    LOG(ERROR) << "Unexpected size after filesystem resize: got "
               << response.current_size() << ", expected "
               << stateful_target_size_;
    *failure_reason = "Unexpected size after filesystem resize";
    last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
    return last_stateful_resize_status_;
  }

  stateful_size_ = response.current_size();

  if (stateful_resize_type_ == DiskResizeType::SHRINK) {
    LOG(INFO) << "Filesystem shrink complete; shrinking disk image";
    if (!ResizeDiskImage(response.current_size())) {
      LOG(ERROR) << "ResizeDiskImage failed";
      *failure_reason = "ResizeDiskImage failed";
      last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_FAILED;
      return last_stateful_resize_status_;
    }
  } else {
    LOG(INFO) << "Filesystem expansion complete";
  }

  LOG(INFO) << "Disk resize successful";
  stateful_resize_type_ = DiskResizeType::NONE;
  last_stateful_resize_status_ = DiskImageStatus::DISK_STATUS_RESIZED;
  return last_stateful_resize_status_;
}

uint64_t TerminaVm::GetMinDiskSize() {
  grpc::ClientContext ctx;
  ctx.set_deadline(gpr_time_add(
      gpr_now(GPR_CLOCK_MONOTONIC),
      gpr_time_from_seconds(kDefaultTimeoutSeconds, GPR_TIMESPAN)));

  vm_tools::EmptyMessage request;
  vm_tools::GetResizeBoundsResponse response;

  grpc::Status status = stub_->GetResizeBounds(&ctx, request, &response);

  if (!status.ok()) {
    LOG(ERROR) << "GetResizeBounds RPC failed";
    return 0;
  }

  return response.minimum_size();
}

uint32_t TerminaVm::GatewayAddress() const {
  return subnet_->AddressAtOffset(kHostAddressOffset);
}

uint32_t TerminaVm::IPv4Address() const {
  return subnet_->AddressAtOffset(kGuestAddressOffset);
}

uint32_t TerminaVm::Netmask() const {
  return subnet_->Netmask();
}

uint32_t TerminaVm::ContainerNetmask() const {
  if (container_subnet_)
    return container_subnet_->Netmask();

  return INADDR_ANY;
}

size_t TerminaVm::ContainerPrefixLength() const {
  if (container_subnet_)
    return container_subnet_->PrefixLength();

  return 0;
}

uint32_t TerminaVm::ContainerSubnet() const {
  if (container_subnet_)
    return container_subnet_->AddressAtOffset(0);

  return INADDR_ANY;
}

VmInterface::Info TerminaVm::GetInfo() {
  VmInterface::Info info = {
      .ipv4_address = IPv4Address(),
      .pid = pid(),
      .cid = cid(),
      .seneschal_server_handle = seneschal_server_handle(),
      .status = IsTremplinStarted() ? VmInterface::Status::RUNNING
                                    : VmInterface::Status::STARTING,
      .type = is_termina_ ? VmInfo::TERMINA : VmInfo::UNKNOWN,
  };

  return info;
}

void TerminaVm::set_kernel_version_for_testing(std::string kernel_version) {
  kernel_version_ = kernel_version;
}

void TerminaVm::set_stub_for_testing(
    std::unique_ptr<vm_tools::Maitred::Stub> stub) {
  stub_ = std::move(stub);
}

std::unique_ptr<TerminaVm> TerminaVm::CreateForTesting(
    std::unique_ptr<patchpanel::Subnet> subnet,
    uint32_t vsock_cid,
    base::FilePath runtime_dir,
    base::FilePath log_path,
    base::FilePath gpu_cache_path,
    std::string stateful_device,
    uint64_t stateful_size,
    std::string kernel_version,
    std::unique_ptr<vm_tools::Maitred::Stub> stub,
    bool is_termina,
    VmBuilder vm_builder) {
  VmFeatures features{
      .gpu = false,
      .software_tpm = false,
      .audio_capture = false,
  };
  auto vm = base::WrapUnique(
      new TerminaVm(std::move(subnet), vsock_cid, nullptr,
                    std::move(runtime_dir), std::move(log_path),
                    std::move(gpu_cache_path), std::move(stateful_device),
                    std::move(stateful_size), features, is_termina));
  vm->set_kernel_version_for_testing(kernel_version);
  vm->set_stub_for_testing(std::move(stub));

  return vm;
}

}  // namespace concierge
}  // namespace vm_tools
