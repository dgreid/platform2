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

// Uid and gid mappings for the android data directory. This is a
// comma-separated list of 3 values: <start of range inside the user namespace>
// <start of range outside the user namespace> <count>. The values are taken
// from platform2/arc/container-bundle/pi/config.json.
constexpr char kAndroidUidMap[] =
    "0 655360 5000,5000 600 50,5050 660410 1994950";
constexpr char kAndroidGidMap[] =
    "0 655360 1065,1065 20119 1,1066 656426 3934,5000 600 50,5050 660410 "
    "1994950";

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

std::string CreateSharedDataParam(const base::FilePath& data_dir,
                                  const std::string& tag,
                                  bool enable_caches,
                                  bool ascii_casefold) {
  // TODO(b/169446394): Go back to using "never" when caching is disabled once
  // we can switch /data/media to use 9p.
  return base::StringPrintf(
      "%s:%s:type=fs:cache=%s:uidmap=%s:gidmap=%s:timeout=%d:rewrite-"
      "security-xattrs=true:ascii_casefold=%s:writeback=%s",
      data_dir.value().c_str(), tag.c_str(), enable_caches ? "always" : "auto",
      kAndroidUidMap, kAndroidGidMap, enable_caches ? 3600 : 1,
      ascii_casefold ? "true" : "false", enable_caches ? "true" : "false");
}

}  // namespace

ArcVm::ArcVm(int32_t vsock_cid,
             std::unique_ptr<patchpanel::Client> network_client,
             std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
             base::FilePath runtime_dir,
             ArcVmFeatures features)
    : VmBaseImpl(std::move(network_client)),
      vsock_cid_(vsock_cid),
      seneschal_server_proxy_(std::move(seneschal_server_proxy)),
      features_(features) {
  CHECK(base::DirectoryExists(runtime_dir));

  // Take ownership of the runtime directory.
  CHECK(runtime_dir_.Set(runtime_dir));
}

ArcVm::~ArcVm() {
  Shutdown();
}

std::unique_ptr<ArcVm> ArcVm::Create(
    base::FilePath kernel,
    base::FilePath rootfs,
    base::FilePath fstab,
    uint32_t cpus,
    base::FilePath pstore_path,
    uint32_t pstore_size,
    std::vector<ArcVm::Disk> disks,
    uint32_t vsock_cid,
    base::FilePath data_dir,
    std::unique_ptr<patchpanel::Client> network_client,
    std::unique_ptr<SeneschalServerProxy> seneschal_server_proxy,
    base::FilePath runtime_dir,
    ArcVmFeatures features,
    std::vector<std::string> params) {
  auto vm = base::WrapUnique(new ArcVm(vsock_cid, std::move(network_client),
                                       std::move(seneschal_server_proxy),
                                       std::move(runtime_dir), features));

  if (!vm->Start(std::move(kernel), std::move(rootfs), std::move(fstab), cpus,
                 std::move(pstore_path), pstore_size, std::move(disks),
                 std::move(data_dir), std::move(params))) {
    vm.reset();
  }

  return vm;
}

std::string ArcVm::GetVmSocketPath() const {
  return runtime_dir_.GetPath().Append(kCrosvmSocket).value();
}

bool ArcVm::Start(base::FilePath kernel,
                  base::FilePath rootfs,
                  base::FilePath fstab,
                  uint32_t cpus,
                  base::FilePath pstore_path,
                  uint32_t pstore_size,
                  std::vector<ArcVm::Disk> disks,
                  base::FilePath data_dir,
                  std::vector<std::string> params) {
  // Get the available network interfaces.
  network_devices_ = network_client_->NotifyArcVmStartup(vsock_cid_);
  if (network_devices_.empty()) {
    LOG(ERROR) << "No network devices available";
    return false;
  }

  // Open the tap device(s).
  std::vector<base::ScopedFD> tap_fds;
  for (const auto& dev : network_devices_) {
    auto fd =
        OpenTapDevice(dev.ifname(), true /*vnet_hdr*/, nullptr /*ifname_out*/);
    if (!fd.is_valid()) {
      LOG(ERROR) << "Unable to open and configure TAP device " << dev.ifname();
    } else {
      tap_fds.emplace_back(std::move(fd));
    }
  }
  if (tap_fds.empty()) {
    LOG(ERROR) << "No TAP devices available";
    return false;
  }

  const std::string rootfs_flag = rootfs_writable() ? "--rwdisk" : "--disk";

  std::string oem_etc_shared_dir = base::StringPrintf(
      "%s:%s:type=fs:cache=always:uidmap=%s:gidmap=%s:timeout=3600:rewrite-"
      "security-xattrs=true",
      kOemEtcSharedDir, kOemEtcSharedDirTag, kOemEtcUgidMap, kOemEtcUgidMap);

  std::string shared_data =
      CreateSharedDataParam(data_dir, "_data", true, false);
  std::string shared_data_media =
      CreateSharedDataParam(data_dir, "_data_media", false, true);

  std::string shared_media = base::StringPrintf(
      "%s:%s:type=9p:cache=never:uidmap=%s:gidmap=%s:ascii_casefold=true",
      kMediaSharedDir, kMediaSharedDirTag, kAndroidUidMap, kAndroidGidMap);

  const base::FilePath testharness_dir(kTestHarnessSharedDir);
  std::string shared_testharness = CreateSharedDataParam(
      testharness_dir, kTestHarnessSharedDirTag, true, false);

  // Build up the process arguments.
  // clang-format off
  base::StringPairs args = {
    { kCrosvmBin,          "run" },
    { "--cpus",            std::to_string(cpus) },
    { "--mem",             GetVmMemoryMiB() },
    { rootfs_flag,         rootfs.value() },
    { "--cid",             std::to_string(vsock_cid_) },
    { "--socket",          GetVmSocketPath() },
    { "--gpu",             "" },
    { "--wayland-sock",    kWaylandSocket },
    { "--wayland-sock",    "/run/arcvm/mojo/mojo-proxy.sock,name=mojo" },
    { "--syslog-tag",      base::StringPrintf("ARCVM(%u)", vsock_cid_) },
    { "--ac97",            "backend=cras,capture=true" },
    // Second AC97 for the aaudio path.
    { "--ac97",            "backend=cras,capture=true" },
    { "--android-fstab",   fstab.value() },
    { "--pstore",          base::StringPrintf("path=%s,size=%d",
                              pstore_path.value().c_str(), pstore_size) },
    { "--shared-dir",     std::move(oem_etc_shared_dir) },
    { "--shared-dir",     std::move(shared_data) },
    { "--shared-dir",     std::move(shared_data_media) },
    { "--shared-dir",     std::move(shared_media) },
    { "--shared-dir",     std::move(shared_testharness) },
    { "--no-smt",         "" },
    { "--params",         base::JoinString(params, " ") },
  };
  // clang-format on

  if (USE_CROSVM_WL_DMABUF)
    args.emplace_back("--wayland-dmabuf", "");

  if (USE_CROSVM_VIRTIO_VIDEO)
    args.emplace_back("--video-decoder", "");

  for (const auto& fd : tap_fds) {
    args.emplace_back("--tap-fd", std::to_string(fd.get()));
  }

  // Add any extra disks.
  for (const auto& disk : disks) {
    if (disk.writable) {
      args.emplace_back("--rwdisk", disk.path.value());
    } else {
      args.emplace_back("--disk", disk.path.value());
    }
  }

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

  // Put everything into the brillo::ProcessImpl.
  for (std::pair<std::string, std::string>& arg : args) {
    process_.AddArg(std::move(arg.first));
    if (!arg.second.empty())
      process_.AddArg(std::move(arg.second));
  }

  // Change the process group before exec so that crosvm sending SIGKILL to the
  // whole process group doesn't kill us as well. The function also changes the
  // cpu cgroup for ARCVM's crosvm processes.
  process_.SetPreExecCallback(base::Bind(
      &SetUpCrosvmProcess, base::FilePath(kArcvmCpuCgroup).Append("tasks")));

  if (!process_.Start()) {
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
  RunCrosvmCommand("stop", GetVmSocketPath());

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
  RunCrosvmCommand("suspend", GetVmSocketPath());
}

void ArcVm::HandleSuspendDone() {
  RunCrosvmCommand("resume", GetVmSocketPath());
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
