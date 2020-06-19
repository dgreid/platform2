// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/syslog/host_collector.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/sysinfo.h>
#include <sys/un.h>

#include <utility>

#include <base/memory/ptr_util.h>

#include "vm_tools/syslog/log_pipe.h"

namespace pb = google::protobuf;

namespace vm_tools {
namespace syslog {

HostCollector::HostCollector(int64_t cid,
                             base::WeakPtr<LogPipeManager> log_pipe_manager)
    : cid_(cid), log_pipe_manager_(log_pipe_manager), weak_factory_(this) {}

HostCollector::~HostCollector() = default;

std::unique_ptr<HostCollector> HostCollector::Create(
    int64_t cid,
    base::FilePath logsocket_path,
    base::WeakPtr<LogPipeManager> log_pipe_manager) {
  LOG(INFO) << "Creating HostCollector watching " << logsocket_path;
  auto collector =
      base::WrapUnique<HostCollector>(new HostCollector(cid, log_pipe_manager));
  if (!collector->BindLogSocket(logsocket_path.value().c_str())) {
    collector.reset();
    return collector;
  }

  if (!collector->StartWatcher(kFlushPeriod)) {
    collector.reset();
  }

  return collector;
}

std::unique_ptr<HostCollector> HostCollector::CreateForTesting(
    int64_t cid,
    base::ScopedFD syslog_fd,
    base::WeakPtr<LogPipeManager> log_pipe_manager) {
  CHECK(log_pipe_manager);
  CHECK(syslog_fd.is_valid());

  auto collector = base::WrapUnique(new HostCollector(cid, log_pipe_manager));
  collector->SetSyslogFDForTesting(std::move(syslog_fd));

  if (!collector->StartWatcher(kFlushPeriodForTesting)) {
    collector.reset();
  }

  return collector;
}

bool HostCollector::SendUserLogs() {
  if (!log_pipe_manager_) {
    return false;
  }
  // We call LogPipeManager directly rather than through a stub because
  // we're in the same process.
  grpc::Status status =
      log_pipe_manager_->WriteSyslogRecords(cid_, syslog_request());
  return status.ok();
}

}  // namespace syslog
}  // namespace vm_tools
