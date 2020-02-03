// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/cicerone/crash_listener_impl.h"

#include <fcntl.h>
#include <unistd.h>

#include <base/files/scoped_file.h>
#include <base/optional.h>
#include <base/posix/eintr_wrapper.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/process.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>

#include "vm_tools/cicerone/service.h"
#include "vm_tools/cicerone/virtual_machine.h"

namespace vm_tools {
namespace cicerone {

CrashListenerImpl::CrashListenerImpl(
    base::WeakPtr<vm_tools::cicerone::Service> service)
    : service_(service), task_runner_(base::ThreadTaskRunnerHandle::Get()) {}

grpc::Status CrashListenerImpl::CheckMetricsConsent(
    grpc::ServerContext* ctx,
    const EmptyMessage* request,
    MetricsConsentResponse* response) {
  response->set_consent_granted(metrics_.AreMetricsEnabled());
  return grpc::Status::OK;
}

grpc::Status CrashListenerImpl::SendCrashReport(grpc::ServerContext* ctx,
                                                const CrashReport* crash_report,
                                                EmptyMessage* response) {
  // Set O_CLOEXEC on the pipe so that the write end doesn't get kept open by
  // the child process after we're done with it.
  int pipefd[2];
  if (HANDLE_EINTR(pipe2(pipefd, O_CLOEXEC)) != 0) {
    return {grpc::UNKNOWN, "Failed to create pipe"};
  }

  base::ScopedFD read(pipefd[0]);
  base::ScopedFD write(pipefd[1]);

  // Turn off CLOEXEC for the read end, as that needs to be sent to the child
  // process
  if (HANDLE_EINTR(fcntl(read.get(), F_SETFD, 0)) != 0) {
    return {grpc::UNKNOWN, "Failed to unset CLOEXEC on read end of pipe"};
  }

  brillo::ProcessImpl crash_reporter;
  crash_reporter.AddArg("/sbin/crash_reporter");
  crash_reporter.AddArg("--vm_crash");
  if (auto pid = GetPidFromPeerAddress(ctx)) {
    crash_reporter.AddArg(base::StringPrintf("--vm_pid=%d", *pid));
  }
  crash_reporter.BindFd(read.get(), 0 /* stdin */);
  crash_reporter.SetCloseUnusedFileDescriptors(true);

  if (!crash_reporter.Start())
    return {grpc::UNKNOWN, "Failed to start crash_reporter"};

  // Close the read end of the pipe after passing it to the child process.
  read.reset();

  google::protobuf::io::FileOutputStream output(write.get());
  if (!google::protobuf::TextFormat::Print(*crash_report, &output)) {
    return {grpc::INVALID_ARGUMENT, "Failed to print CrashReport protobuf"};
  }
  if (!output.Flush()) {
    return {grpc::UNKNOWN, "Failed to send report to crash_reporter"};
  }
  // Close the write end of the pipe after we finish writing to it
  // so the child process knows we've finished.
  write.reset();

  int exit_status = crash_reporter.Wait();
  if (exit_status == 0)
    return grpc::Status::OK;
  else
    return {grpc::UNKNOWN, "Crash_reporter encountered an error"};
}

base::Optional<pid_t> CrashListenerImpl::GetPidFromPeerAddress(
    grpc::ServerContext* ctx) {
  uint32_t cid = 0;
  std::string peer_address = ctx->peer();
  if (sscanf(peer_address.c_str(), "vsock:%u", &cid) != 1) {
    LOG(WARNING) << "Failed to parse peer address " << peer_address;
    return base::nullopt;
  }

  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);

  VirtualMachine* vm = nullptr;
  std::string owner_id;
  std::string vm_name;
  bool result;
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&CrashListenerImpl::GetVirtualMachineForCidOrToken,
                     base::Unretained(this), cid, &vm, &owner_id, &vm_name,
                     &result, &event));

  event.Wait();
  if (!result) {
    LOG(ERROR) << "Failed to get VM for peer address " << peer_address;
    return base::nullopt;
  }

  return vm->pid();
}

void CrashListenerImpl::GetVirtualMachineForCidOrToken(
    const uint32_t cid,
    VirtualMachine** vm_out,
    std::string* owner_id_out,
    std::string* name_out,
    bool* ret_value,
    base::WaitableEvent* event) {
  *ret_value = service_->GetVirtualMachineForCidOrToken(cid, "", vm_out,
                                                        owner_id_out, name_out);
  event->Signal();
}

}  // namespace cicerone
}  // namespace vm_tools
