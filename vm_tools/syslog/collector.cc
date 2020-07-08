// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/syslog/collector.h"

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/un.h>

#include <linux/vm_sockets.h>  // Needs to come after sys/socket.h

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/callback.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/posix/eintr_wrapper.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_piece.h>
#include <base/strings/stringprintf.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <chromeos/scoped_minijail.h>
#include <grpcpp/grpcpp.h>

#include "vm_tools/syslog/parser.h"

using std::string;

namespace pb = google::protobuf;

namespace vm_tools {
namespace syslog {
namespace {
// Periodic interval for flushing buffered logs.
constexpr int64_t kTimerFlushMilliseconds = 5000;

// Periodic interval for flushing buffered logs during testing.
constexpr int64_t kTimerFlushMillisecondsForTesting = 500;

// Maximum size the buffer can reach before logs are immediately flushed.
constexpr size_t kBufferThreshold = 4096;

// Size of the largest syslog record as defined by RFC3164.
constexpr size_t kMaxSyslogRecord = 1024;

// Max number of records we should attempt to read out of the socket at a time.
constexpr int kMaxRecordCount = 11;

// Path to the standard syslog listening path.
constexpr char kDevLog[] = "/dev/log";

// Known host port for the LogCollector service.
constexpr unsigned int kLogCollectorPort = 9999;

// Path to the standard empty directory where we will jail the daemon.
constexpr char kPivotRoot[] = "/mnt/empty";

// Name for the "syslog" user and group.
constexpr char kSyslog[] = "syslog";

}  // namespace

std::unique_ptr<Collector> Collector::Create(base::Closure shutdown_closure) {
  auto collector =
      base::WrapUnique<Collector>(new Collector(std::move(shutdown_closure)));

  if (!collector->Init()) {
    collector.reset();
  }

  return collector;
}

void Collector::OnSignalReadable() {
  signalfd_siginfo info;
  if (read(signal_fd_.get(), &info, sizeof(info)) != sizeof(info)) {
    PLOG(ERROR) << "Failed to read from signalfd";
  }
  DCHECK_EQ(info.ssi_signo, SIGTERM);

  FlushLogs();
  base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, shutdown_closure_);
}

void Collector::OnSyslogReadable() {
  bool more = true;
  for (int i = 0; i < kMaxRecordCount && more; ++i) {
    more = ReadOneSyslogRecord();

    // Send all buffered records immediately if we've crossed the threshold.
    if (buffered_size_ > kBufferThreshold) {
      FlushLogs();
      timer_.Reset();
    }
  }
}

Collector::Collector(base::Closure shutdown_closure)
    : shutdown_closure_(std::move(shutdown_closure)), weak_factory_(this) {}

bool Collector::Init() {
  // Start listening on the syslog socket.
  syslog_fd_.reset(socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0));

  if (!syslog_fd_.is_valid()) {
    PLOG(ERROR) << "Failed to create unix domain socket";
    return false;
  }

  // Make sure that any previous socket is cleaned up before attempting to bind
  // to it again.  We don't really care whether the unlink succeeds or not.
  HANDLE_EINTR(unlink(kDevLog));

  struct sockaddr_un sun = {
      .sun_family = AF_UNIX,
  };
  strncpy(sun.sun_path, kDevLog, sizeof(sun.sun_path));

  if (bind(syslog_fd_.get(), reinterpret_cast<struct sockaddr*>(&sun),
           sizeof(sun)) != 0) {
    PLOG(ERROR) << "Failed to bind logging socket";
    return false;
  }

  // Give everyone write permissions to the socket.
  if (chmod(sun.sun_path, 0666) != 0) {
    PLOG(ERROR) << "Unable to change permissions for syslog socket";
    return false;
  }

  syslog_controller_ = base::FileDescriptorWatcher::WatchReadable(
      syslog_fd_.get(), base::BindRepeating(&Collector::OnSyslogReadable,
                                            base::Unretained(this)));
  if (!syslog_controller_) {
    LOG(ERROR) << "Failed to watch syslog file descriptor";
    return false;
  }

  // Start listening for SIGTERM.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGTERM);

  signal_fd_.reset(signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK));
  if (!signal_fd_.is_valid()) {
    PLOG(ERROR) << "Unable to create signalfd";
    return false;
  }
  signal_controller_ = base::FileDescriptorWatcher::WatchReadable(
      signal_fd_.get(), base::BindRepeating(&Collector::OnSignalReadable,
                                            base::Unretained(this)));
  if (!signal_controller_) {
    LOG(ERROR) << "Failed to watch signal file descriptor";
    return false;
  }

  // Block the standard SIGTERM handler since we will be getting it via the
  // signalfd.
  sigprocmask(SIG_BLOCK, &mask, nullptr);

  // Figure out the boot time so that we can timestamp kernel logs.
  struct sysinfo info;
  if (sysinfo(&info) != 0) {
    PLOG(ERROR) << "Failed to read sysinfo";
    return false;
  }
  boot_time_ = base::Time::Now() - base::TimeDelta::FromSeconds(info.uptime);

  // Create the stub to the LogCollector service on the host.
  stub_ = vm_tools::LogCollector::NewStub(grpc::CreateChannel(
      base::StringPrintf("vsock:%u:%u", VMADDR_CID_HOST, kLogCollectorPort),
      grpc::InsecureChannelCredentials()));
  if (!stub_) {
    LOG(ERROR) << "Failed to create stub for LogCollector service";
    return false;
  }

  // Start a timer to periodically flush logs.
  timer_.Start(FROM_HERE,
               base::TimeDelta::FromMilliseconds(kTimerFlushMilliseconds),
               base::Bind(&Collector::FlushLogs, weak_factory_.GetWeakPtr()));

  // Start a new log request buffer.
  syslog_request_ = pb::Arena::CreateMessage<vm_tools::LogRequest>(&arena_);
  buffered_size_ = 0;

  // Drop all unnecessary privileges.
  ScopedMinijail jail(minijail_new());
  if (!jail) {
    PLOG(ERROR) << "Failed to create minijail";
    return false;
  }

  minijail_change_user(jail.get(), kSyslog);
  minijail_change_group(jail.get(), kSyslog);
  minijail_no_new_privs(jail.get());

  // Pivot into an empty directory where we have no permissions.
  minijail_namespace_vfs(jail.get());
  minijail_enter_pivot_root(jail.get(), kPivotRoot);

  minijail_enter(jail.get());

  // Everything succeeded.
  return true;
}

void Collector::FlushLogs() {
  if (syslog_request_->records_size() <= 0) {
    // Nothing to do.  Just return.
    return;
  }

  if (syslog_request_->records_size() > 0) {
    grpc::ClientContext ctx;
    vm_tools::EmptyMessage response;
    grpc::Status status =
        stub_->CollectUserLogs(&ctx, *syslog_request_, &response);

    if (!status.ok()) {
      LOG(ERROR) << "Failed to send user logs to LogCollector service.  Error "
                 << "code " << status.error_code() << ": "
                 << status.error_message();
    }
  }

  // Reset everything.
  arena_.Reset();
  syslog_request_ = pb::Arena::CreateMessage<vm_tools::LogRequest>(&arena_);
  buffered_size_ = 0;
}

bool Collector::ReadOneSyslogRecord() {
  char buf[kMaxSyslogRecord + 1];
  ssize_t ret =
      HANDLE_EINTR(recv(syslog_fd_.get(), buf, kMaxSyslogRecord, MSG_DONTWAIT));
  if (ret < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      PLOG(ERROR) << "Failed to read from syslog socket";
    }
    return false;
  }

  if (ret == 0) {
    // We didn't read anything but that doesn't necessarily mean there was an
    // error.
    return true;
  }
  // Make sure the buffer is properly terminated.
  buf[ret] = '\0';

  // Attempt to parse the record.
  auto* record = pb::Arena::CreateMessage<vm_tools::LogRecord>(&arena_);
  if (!ParseSyslogRecord(buf, ret, record)) {
    LOG(ERROR) << "Failed to parse syslog record";

    // Return true here because while we failed to parse this message there may
    // still be more messages pending in the kernel buffer.
    return true;
  }

  // We have a valid entry. Update the buffered message count and store the
  // message.
  buffered_size_ += record->ByteSizeLong();

  // Safe because |record| was created by the same Arena that owns
  // |syslog_request_|.
  syslog_request_->add_records()->UnsafeArenaSwap(record);

  return true;
}

std::unique_ptr<Collector> Collector::CreateForTesting(
    base::ScopedFD syslog_fd,
    base::Time boot_time,
    std::unique_ptr<vm_tools::LogCollector::Stub> stub) {
  CHECK(stub);
  CHECK(syslog_fd.is_valid());
  auto collector = base::WrapUnique<Collector>(new Collector(base::Closure()));

  if (!collector->InitForTesting(std::move(syslog_fd), boot_time,
                                 std::move(stub))) {
    collector.reset();
  }

  return collector;
}

bool Collector::InitForTesting(
    base::ScopedFD syslog_fd,
    base::Time boot_time,
    std::unique_ptr<vm_tools::LogCollector::Stub> stub) {
  // Set the fake boot time.
  boot_time_ = boot_time;

  // Start listening on the syslog socket.
  syslog_fd_.swap(syslog_fd);

  syslog_controller_ = base::FileDescriptorWatcher::WatchReadable(
      syslog_fd_.get(), base::BindRepeating(&Collector::OnSyslogReadable,
                                            base::Unretained(this)));
  if (!syslog_controller_) {
    LOG(ERROR) << "Failed to watch syslog file descriptor";
    return false;
  }

  // Store the stub for the LogCollector.
  stub_ = std::move(stub);

  // Start a timer to periodically flush logs.
  timer_.Start(
      FROM_HERE,
      base::TimeDelta::FromMilliseconds(kTimerFlushMillisecondsForTesting),
      base::Bind(&Collector::FlushLogs, weak_factory_.GetWeakPtr()));

  // Start a new log request buffer.
  syslog_request_ = pb::Arena::CreateMessage<vm_tools::LogRequest>(&arena_);
  buffered_size_ = 0;

  // Everything succeeded.
  return true;
}

}  // namespace syslog
}  // namespace vm_tools
