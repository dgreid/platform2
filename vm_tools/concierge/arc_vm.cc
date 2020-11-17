// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/arc_vm.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// Needs to be included after sys/socket.h
#include <linux/vm_sockets.h>

#include <utility>

#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <base/strings/string_split.h>
#include <base/system/sys_info.h>
#include <base/time/time.h>
#include <chromeos/constants/vm_tools.h>
#include <vboot/crossystem.h>

#include "vm_tools/concierge/tap_device_builder.h"
#include "vm_tools/concierge/vm_builder.h"
#include "vm_tools/concierge/vm_util.h"

namespace vm_tools {
namespace concierge {
namespace {

// Name of the control socket used for controlling crosvm.
constexpr char kCrosvmSocket[] = "arcvm.sock";

// Path to the wayland socket.
constexpr char kWaylandSocket[] = "/run/chrome/wayland-0";

// How long to wait before timing out on child process exits.
constexpr base::TimeDelta kChildExitTimeout = base::TimeDelta::FromSeconds(10);

// The CPU cgroup where all the ARCVM's crosvm processes should belong to.
constexpr char kArcvmCpuCgroup[] = "/sys/fs/cgroup/cpu/vms/arc";

// Port for arc-powerctl running on the guest side.
constexpr unsigned int kVSockPort = 4242;

// Path to the development configuration file (only visible in dev mode).
constexpr char kDevConfFilePath[] = "/usr/local/vms/etc/arcvm_dev.conf";

// Custom parameter key to override the kernel path
constexpr char kKeyToOverrideKernelPath[] = "KERNEL_PATH";

// Shared directories and their tags
constexpr char kOemEtcSharedDir[] = "/run/arcvm/host_generated/oem/etc";
constexpr char kOemEtcSharedDirTag[] = "oem_etc";

constexpr char kMediaSharedDir[] = "/run/arcvm/media";
constexpr char kMediaSharedDirTag[] = "media";

constexpr char kTestHarnessSharedDir[] = "/run/arcvm/testharness";
constexpr char kTestHarnessSharedDirTag[] = "testharness";

// For |kOemEtcSharedDir|, map host's chronos to guest's root, also arc-camera
// (603) to vendor_arc_camera (5003).
constexpr char kOemEtcUgidMap[] = "0 1000 1, 5000 600 50";

base::ScopedFD ConnectVSock(int cid) {
  DLOG(INFO) << "Creating VSOCK...";
  struct sockaddr_vm sa = {};
  sa.svm_family = AF_VSOCK;
  sa.svm_cid = cid;
  sa.svm_port = kVSockPort;

  base::ScopedFD fd(
      socket(AF_VSOCK, SOCK_STREAM | SOCK_CLOEXEC, 0 /* protocol */));
  if (!fd.is_valid()) {
    PLOG(ERROR) << "Failed to create VSOCK";
    return {};
  }

  DLOG(INFO) << "Connecting VSOCK";
  if (HANDLE_EINTR(connect(fd.get(),
                           reinterpret_cast<const struct sockaddr*>(&sa),
                           sizeof(sa))) == -1) {
    fd.reset();
    PLOG(ERROR) << "Failed to connect.";
    return {};
  }

  DLOG(INFO) << "VSOCK connected.";
  return fd;
}

bool ShutdownArcVm(int cid) {
  base::ScopedFD vsock(ConnectVSock(cid));
  if (!vsock.is_valid())
    return false;

  const std::string command("poweroff");
  if (HANDLE_EINTR(write(vsock.get(), command.c_str(), command.size())) !=
      command.size()) {
    PLOG(WARNING) << "Failed to write to ARCVM VSOCK";
    return false;
  }

  DLOG(INFO) << "Started shutting down ARCVM";
  return true;
}

}  // namespace

ArcVm::ArcVm(int32_t vsock_cid,
             std::unique_ptr<patchpanel::Client> network_client,
             std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
             base::FilePath runtime_dir,
             ArcVmFeatures features)
    : VmBaseImpl(std::move(network_client),
                 vsock_cid,
                 std::move(seneschal_server_proxy),
                 kCrosvmSocket,
                 std::move(runtime_dir)),
      features_(features) {}

ArcVm::~ArcVm() {
  Shutdown();
}

std::unique_ptr<ArcVm> ArcVm::Create(
    base::FilePath kernel,
    uint32_t vsock_cid,
    std::unique_ptr<patchpanel::Client> network_client,
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
    base::FilePath runtime_dir,
    ArcVmFeatures features,
    VmBuilder vm_builder) {
  auto vm = std::unique_ptr<ArcVm>(new ArcVm(
      vsock_cid, std::move(network_client), std::move(seneschal_server_proxy),
      std::move(runtime_dir), features));

  if (!vm->Start(std::move(kernel), std::move(vm_builder))) {
    vm.reset();
  }

  return vm;
}

std::string ArcVm::GetVmSocketPath() const {
  return runtime_dir_.GetPath().Append(kCrosvmSocket).value();
}

bool ArcVm::Start(base::FilePath kernel, VmBuilder vm_builder) {
  // Get the available network interfaces.
  network_devices_ = network_client_->NotifyArcVmStartup(vsock_cid_);
  if (network_devices_.empty()) {
    LOG(ERROR) << "No network devices available";
    return false;
  }

  // Open the tap device(s).
  bool no_tap_fd_added = true;
  for (const auto& dev : network_devices_) {
    auto fd =
        OpenTapDevice(dev.ifname(), true /*vnet_hdr*/, nullptr /*ifname_out*/);
    if (!fd.is_valid()) {
      LOG(ERROR) << "Unable to open and configure TAP device " << dev.ifname();
    } else {
      vm_builder.AppendTapFd(std::move(fd));
      no_tap_fd_added = false;
    }
  }

  if (no_tap_fd_added) {
    LOG(ERROR) << "No TAP devices available";
    return false;
  }

  if (USE_CROSVM_WL_DMABUF)
    vm_builder.EnableWaylandDmaBuf(true /* enable */);

  if (USE_CROSVM_VIRTIO_VIDEO) {
    vm_builder.EnableVideoDecoder(true /* enable */);
    vm_builder.EnableVideoEncoder(true /* enable */);
  }

  std::string oem_etc_shared_dir = base::StringPrintf(
      "%s:%s:type=fs:cache=always:uidmap=%s:gidmap=%s:timeout=3600:rewrite-"
      "security-xattrs=true",
      kOemEtcSharedDir, kOemEtcSharedDirTag, kOemEtcUgidMap, kOemEtcUgidMap);
  std::string shared_media = base::StringPrintf(
      "%s:%s:type=9p:cache=never:uidmap=%s:gidmap=%s:ascii_casefold=true",
      kMediaSharedDir, kMediaSharedDirTag, kAndroidUidMap, kAndroidGidMap);
  const base::FilePath testharness_dir(kTestHarnessSharedDir);
  std::string shared_testharness = CreateSharedDataParam(
      testharness_dir, kTestHarnessSharedDirTag, true, false);

  vm_builder.SetMemory(GetVmMemoryMiB())
      .SetVsockCid(vsock_cid_)
      .SetSocketPath(GetVmSocketPath())
      .AppendWaylandSocket(kWaylandSocket)
      .AppendWaylandSocket("/run/arcvm/mojo/mojo-proxy.sock,name=mojo")
      .SetSyslogTag(base::StringPrintf("ARCVM(%u)", vsock_cid_))
      .EnableGpu(true /* enable */)
      .AppendAudioDevice("backend=cras,capture=true")
      // Second AC97 for the audio path.
      .AppendAudioDevice("backend=cras,capture=true")
      .AppendSharedDir(oem_etc_shared_dir)
      .AppendSharedDir(shared_media)
      .AppendSharedDir(shared_testharness);

  auto args = vm_builder.BuildVmArgs();

  // Load any custom parameters from the development configuration file if the
  // feature is turned on (default) and path exists (dev mode only).
  const bool is_dev_mode = (VbGetSystemPropertyInt("cros_debug") == 1);
  if (is_dev_mode && use_dev_conf()) {
    const base::FilePath dev_conf(kDevConfFilePath);
    if (base::PathExists(dev_conf)) {
      std::string data;
      if (!base::ReadFileToString(dev_conf, &data)) {
        PLOG(ERROR) << "Failed to read file " << dev_conf.value();
        return false;
      }
      LoadCustomParameters(data, &args);
    }
  }

  // TODO(b/167430147): Temporarily re-enable for M87 teamfooding.  This does
  // not affect dev mode behavior which is handled by arcvm_dev.conf and chrome
  // switch.  Remove once pstore feedback report is available (b/149920835).
  if (!is_dev_mode) {
    args.emplace_back("--serial",
                      "type=syslog,hardware=virtio-console,num=1,"
                      "console=true");
  }

  // Finally list the path to the kernel.
  const std::string kernel_path =
      RemoveParametersWithKey(kKeyToOverrideKernelPath, kernel.value(), &args);
  args.emplace_back(kernel_path, "");

  // Change the process group before exec so that crosvm sending SIGKILL to the
  // whole process group doesn't kill us as well. The function also changes the
  // cpu cgroup for ARCVM's crosvm processes.
  process_.SetPreExecCallback(base::Bind(
      &SetUpCrosvmProcess, base::FilePath(kArcvmCpuCgroup).Append("tasks")));

  if (!StartProcess(std::move(args))) {
    LOG(ERROR) << "Failed to start VM process";
    // Release any network resources.
    network_client_->NotifyArcVmShutdown(vsock_cid_);
    return false;
  }

  return true;
}

bool ArcVm::Shutdown() {
  // Notify arc-patchpanel that ARCVM is down.
  // This should run before the process existence check below since we still
  // want to release the network resources on crash.
  if (!network_client_->NotifyArcVmShutdown(vsock_cid_)) {
    LOG(WARNING) << "Unable to notify networking services";
  }

  // Do a sanity check here to make sure the process is still around.  It may
  // have crashed and we don't want to be waiting around for an RPC response
  // that's never going to come.  kill with a signal value of 0 is explicitly
  // documented as a way to check for the existence of a process.
  if (!CheckProcessExists(process_.pid())) {
    LOG(INFO) << "ARCVM process is already gone. Do nothing";
    process_.Release();
    return true;
  }

  LOG(INFO) << "Shutting down ARCVM";

  // Ask arc-powerctl running on the guest to power off the VM.
  // TODO(yusukes): We should call ShutdownArcVm() only after the guest side
  // service is fully started. b/143711798
  if (ShutdownArcVm(vsock_cid_) &&
      WaitForChild(process_.pid(), kChildExitTimeout)) {
    LOG(INFO) << "ARCVM is shut down";
    process_.Release();
    return true;
  }

  LOG(WARNING) << "Failed to shut down ARCVM gracefully. Trying to turn it "
               << "down via the crosvm socket.";
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
    process_.Release();
    return true;
  }

  LOG(WARNING) << "Failed to kill VM " << vsock_cid_ << " with SIGTERM";

  // Kill it with fire.
  if (process_.Kill(SIGKILL, kChildExitTimeout.InSeconds())) {
    process_.Release();
    return true;
  }

  LOG(ERROR) << "Failed to kill VM " << vsock_cid_ << " with SIGKILL";
  return false;
}

bool ArcVm::AttachUsbDevice(uint8_t bus,
                            uint8_t addr,
                            uint16_t vid,
                            uint16_t pid,
                            int fd,
                            UsbControlResponse* response) {
  return vm_tools::concierge::AttachUsbDevice(GetVmSocketPath(), bus, addr, vid,
                                              pid, fd, response);
}

bool ArcVm::DetachUsbDevice(uint8_t port, UsbControlResponse* response) {
  return vm_tools::concierge::DetachUsbDevice(GetVmSocketPath(), port,
                                              response);
}

bool ArcVm::ListUsbDevice(std::vector<UsbDevice>* devices) {
  return vm_tools::concierge::ListUsbDevice(GetVmSocketPath(), devices);
}

void ArcVm::HandleSuspendImminent() {
  RunCrosvmCommand("suspend");
}

void ArcVm::HandleSuspendDone() {
  RunCrosvmCommand("resume");
}

// static
bool ArcVm::SetVmCpuRestriction(CpuRestrictionState cpu_restriction_state) {
  return VmBaseImpl::SetVmCpuRestriction(cpu_restriction_state,
                                         kArcvmCpuCgroup);
}

uint32_t ArcVm::IPv4Address() const {
  for (const auto& dev : network_devices_) {
    if (dev.ifname() == "arc0")
      return dev.ipv4_addr();
  }
  return 0;
}

VmInterface::Info ArcVm::GetInfo() {
  VmInterface::Info info = {
      .ipv4_address = IPv4Address(),
      .pid = pid(),
      .cid = cid(),
      .seneschal_server_handle = seneschal_server_handle(),
      .status = VmInterface::Status::RUNNING,
      .type = VmInfo::ARC_VM,
  };

  return info;
}

bool ArcVm::GetVmEnterpriseReportingInfo(
    GetVmEnterpriseReportingInfoResponse* response) {
  response->set_success(false);
  response->set_failure_reason("Not implemented");
  return false;
}

vm_tools::concierge::DiskImageStatus ArcVm::ResizeDisk(
    uint64_t new_size, std::string* failure_reason) {
  *failure_reason = "Not implemented";
  return DiskImageStatus::DISK_STATUS_FAILED;
}

vm_tools::concierge::DiskImageStatus ArcVm::GetDiskResizeStatus(
    std::string* failure_reason) {
  *failure_reason = "Not implemented";
  return DiskImageStatus::DISK_STATUS_FAILED;
}

}  // namespace concierge
}  // namespace vm_tools
