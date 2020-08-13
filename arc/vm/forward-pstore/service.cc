// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/types.h>
#include <unistd.h>

#include <tuple>
#include <utility>
#include <vector>

#include <base/time/time.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_proxy.h>
#include <vm_concierge/proto_bindings/concierge_service.pb.h>

#include "arc/vm/forward-pstore/service.h"
#include "vm_tools/common/naming.h"
#include "vm_tools/common/pstore.h"

namespace arc {
namespace {

constexpr char kArcVmName[] = "arcvm";
constexpr char kArcVmDir[] = "/run/arcvm";
constexpr char kPstoreSourceName[] = "arcvm.pstore";
constexpr char kCryptohomeRoot[] = "/run/daemon-store/crosvm";
constexpr char kPstoreExtension[] = ".pstore";
constexpr base::TimeDelta kReadDelay = base::TimeDelta::FromSeconds(5);

base::FilePath GetPstoreDest(const std::string& owner_id) {
  return base::FilePath(kCryptohomeRoot)
      .Append(owner_id)
      .Append(vm_tools::GetEncodedName(kArcVmName))
      .AddExtension(kPstoreExtension);
}

}  // namespace

Service::Service(base::Closure quit_closure)
    : quit_closure_(quit_closure), weak_ptr_factory_(this) {}

Service::~Service() = default;

void Service::Start() {
  // Connect to dbus.
  dbus::Bus::Options opts;
  opts.bus_type = dbus::Bus::SYSTEM;
  bus_ = new dbus::Bus(std::move(opts));
  if (!bus_->Connect())
    LOG(FATAL) << "Failed to connect to system bus";

  // Subscribe to concierge signals
  auto concierge_proxy = bus_->GetObjectProxy(
      vm_tools::concierge::kVmConciergeServiceName,
      dbus::ObjectPath(vm_tools::concierge::kVmConciergeServicePath));
  if (!concierge_proxy)
    LOG(FATAL) << "Failed to get Concierge proxy";

  concierge_proxy->ConnectToSignal(
      vm_tools::concierge::kVmConciergeInterface,
      vm_tools::concierge::kVmIdChangedSignal,
      base::BindRepeating(&Service::OnVmIdChangedSignal,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&Service::OnSignalConnected,
                     weak_ptr_factory_.GetWeakPtr()));
  concierge_proxy->ConnectToSignal(
      vm_tools::concierge::kVmConciergeInterface,
      vm_tools::concierge::kVmStoppedSignal,
      base::BindRepeating(&Service::OnVmStoppedSignal,
                          weak_ptr_factory_.GetWeakPtr()),
      base::BindOnce(&Service::OnSignalConnected,
                     weak_ptr_factory_.GetWeakPtr()));
}

void Service::OnSignalConnected(const std::string& interface_name,
                                const std::string& signal_name,
                                bool is_connected) {
  if (!is_connected)
    LOG(FATAL) << "Failed to connect to signal: " << signal_name;
  VLOG(1) << "Connected to " << signal_name;
}

void Service::OnVmIdChangedSignal(dbus::Signal* signal) {
  DCHECK_EQ(signal->GetInterface(), vm_tools::concierge::kVmConciergeInterface);
  DCHECK_EQ(signal->GetMember(), vm_tools::concierge::kVmIdChangedSignal);

  vm_tools::concierge::VmIdChangedSignal vm_changed_signal;
  dbus::MessageReader reader(signal);
  if (!reader.PopArrayOfBytesAsProto(&vm_changed_signal)) {
    LOG(ERROR) << "Failed to parse proto from DBus Signal";
    return;
  }

  if (vm_changed_signal.name() != kArcVmName) {
    VLOG(1) << "Ignoring VmIdChangedSignal from non-ARC VM: "
            << vm_changed_signal.name();
    return;
  }

  VLOG(1) << "Received VmIdChangedSignal for ARCVM";
  ForwardPstore(vm_changed_signal.owner_id());
  return;
}

void Service::OnVmStoppedSignal(dbus::Signal* signal) {
  DCHECK_EQ(signal->GetInterface(), vm_tools::concierge::kVmConciergeInterface);
  DCHECK_EQ(signal->GetMember(), vm_tools::concierge::kVmStoppedSignal);

  vm_tools::concierge::VmStoppedSignal vm_stopped_signal;
  dbus::MessageReader reader(signal);
  if (!reader.PopArrayOfBytesAsProto(&vm_stopped_signal)) {
    PLOG(ERROR) << "Failed to parse proto from DBus Signal";
    return;
  }

  if (vm_stopped_signal.name() != kArcVmName) {
    LOG(INFO) << "Ignoring VmStoppedSignal from non-ARC VM: "
              << vm_stopped_signal.name();
    return;
  }

  // ForwardContents() one last time to get final dmesg output.
  ForwardContents(vm_stopped_signal.owner_id());

  // Stop timer and close fds.
  timer_.Stop();
  pstore_fd_.reset();
  root_fd_.reset();
  dest_fd_.reset();
}

void Service::ForwardPstore(const std::string& owner_id) {
  DCHECK(!pstore_fd_.is_valid());

  brillo::SafeFD root_fd;
  brillo::SafeFD arcvm_dir_fd;
  brillo::SafeFD pstore_fd;
  brillo::SafeFD::Error err;
  std::tie(root_fd, err) = brillo::SafeFD::Root();
  if (brillo::SafeFD::IsError(err) || !root_fd.is_valid()) {
    LOG(ERROR) << "Failed to open root fd, error:" << static_cast<int>(err);
    return;
  }
  std::tie(arcvm_dir_fd, err) =
      root_fd.OpenExistingDir(base::FilePath(kArcVmDir));
  if (brillo::SafeFD::IsError(err) || !arcvm_dir_fd.is_valid()) {
    LOG(ERROR) << "Failed to open " << kArcVmDir
               << ", error:" << static_cast<int>(err);
    return;
  }
  std::tie(pstore_fd, err) =
      arcvm_dir_fd.OpenExistingFile(base::FilePath(kPstoreSourceName));
  if (brillo::SafeFD::IsError(err) || !arcvm_dir_fd.is_valid()) {
    // On aarch64 platforms, crosvm does not create a pstore file. Log a warning
    // and return.
    VLOG(1) << "Failed to open " << kArcVmDir << "/" << kPstoreSourceName
            << ", error:" << static_cast<int>(err);
    return;
  }

  // Unlink pstore from /run/arcvm location.
  err = arcvm_dir_fd.Unlink(kPstoreSourceName);
  if (brillo::SafeFD::IsError(err))
    LOG(ERROR) << "Failed to unlink " << kPstoreSourceName
               << ", error:" << static_cast<int>(err);

  // Start forwarding the contents to cryptohome location.
  root_fd_ = std::move(root_fd);
  pstore_fd_ = std::move(pstore_fd);
  timer_.Start(FROM_HERE, kReadDelay,
               base::BindRepeating(&Service::ForwardContents,
                                   weak_ptr_factory_.GetWeakPtr(), owner_id));
  ForwardContents(owner_id);
}

void Service::ForwardContents(const std::string& owner_id) {
  if (!pstore_fd_.is_valid()) {
    LOG(ERROR) << "Pstore source fd is invalid";
    return;
  }

  // Seek to beginning of file before reading.
  if (lseek(pstore_fd_.get(), 0, SEEK_SET) != 0) {
    PLOG(ERROR) << "Cannot seek to beginning of pstore file";
    return;
  }
  // Read pstore.
  std::vector<char> content;
  brillo::SafeFD::Error err;
  std::tie(content, err) = pstore_fd_.ReadContents(vm_tools::kPstoreSize);
  if (brillo::SafeFD::IsError(err)) {
    LOG(ERROR) << "Failed to read pstore source fd, error:"
               << static_cast<int>(err);
    return;
  }

  // Write to cryptohome path.
  if (!dest_fd_.is_valid()) {
    base::FilePath dest = GetPstoreDest(owner_id);
    std::tie(dest_fd_, err) = root_fd_.MakeFile(dest, 0700, getuid(), getgid(),
                                                O_WRONLY | O_CLOEXEC | O_TRUNC);
    if (brillo::SafeFD::IsError(err) || !dest_fd_.is_valid()) {
      LOG(ERROR) << "Failed to open destination fd " << dest
                 << ", error:" << static_cast<int>(err);
      return;
    }
  }
  // Seek to beginning of file before writing.
  if (lseek(dest_fd_.get(), 0, SEEK_SET) != 0) {
    PLOG(ERROR) << "Cannot seek to beginning of pstore destination";
    return;
  }
  err = dest_fd_.Write(content.data(), content.size());
  if (brillo::SafeFD::IsError(err))
    LOG(ERROR) << "Failed to write to pstore destination, error:"
               << static_cast<int>(err);
}
}  // namespace arc
