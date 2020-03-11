// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/vm_support_proper.h"

#include <base/logging.h>
#include <base/strings/stringprintf.h>
#include <chromeos/constants/vm_tools.h>
#include <grpcpp/grpcpp.h>

#include <sys/socket.h>
#include <utility>

#include <linux/vm_sockets.h>

#include "crash-reporter/user_collector.h"

VmSupportProper::VmSupportProper() {
  std::string addr = base::StringPrintf("vsock:%u:%u", VMADDR_CID_HOST,
                                        vm_tools::kCrashListenerPort);

  // It's safe to use an unencrypted/authenticated channel here because the
  // whole channel exists within a single machine, and so we can rely on the
  // kernel to provide us with confidentiality and integrity. Our usage of a
  // vsock address guarantees this.
  auto channel =
      grpc::CreateChannel(std::move(addr), grpc::InsecureChannelCredentials());
  stub_ = std::make_unique<vm_tools::cicerone::CrashListener::Stub>(
      std::move(channel));
}

void VmSupportProper::AddMetadata(UserCollector* collector) {
  // TODO(hollingum): implement me.
}

void VmSupportProper::FinishCrash(const base::FilePath& crash_meta_path) {
  LOG(INFO) << "A program crashed in the VM";
  // TODO(hollingum): implement me.
}

bool VmSupportProper::GetMetricsConsent() {
  grpc::ClientContext ctx;
  vm_tools::EmptyMessage request;
  vm_tools::cicerone::MetricsConsentResponse response;
  grpc::Status status = stub_->CheckMetricsConsent(&ctx, request, &response);
  return status.ok() && response.consent_granted();
}

bool VmSupportProper::ShouldDump(pid_t pid, std::string* out_reason) {
  // Namespaces are accessed via the /proc/*/ns/* set of paths. The kernel
  // guarantees that if two processes share a namespace, their corresponding
  // namespace files will have the same inode number, as reported by stat.
  //
  // For now, we are only interested in processes in the root PID
  // namespace. When invoked by the kernel in response to a crash,
  // crash_reporter will be run in the root of all the namespace hierarchies, so
  // we can easily check this by comparing the crashed process PID namespace
  // with our own.
  struct stat st;

  auto namespace_path = base::StringPrintf("/proc/%d/ns/pid", pid);
  if (stat(namespace_path.c_str(), &st) < 0) {
    *out_reason = "failed to get process PID namespace";
    return false;
  }
  ino_t inode = st.st_ino;

  if (stat("/proc/self/ns/pid", &st) < 0) {
    *out_reason = "failed to get own PID namespace";
    return false;
  }
  ino_t self_inode = st.st_ino;

  if (inode != self_inode) {
    *out_reason = "ignoring - process not in root namespace";
    return false;
  }

  return true;
}
