// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef VM_TOOLS_SYSLOG_GUEST_COLLECTOR_H_
#define VM_TOOLS_SYSLOG_GUEST_COLLECTOR_H_

#include <memory>

#include "vm_tools/syslog/collector.h"

namespace vm_tools {
namespace syslog {

// Responsible for listening on /dev/log for any userspace applications that
// wish to log messages with the system syslog.  TODO(chirantan):  This
// currently doesn't handle kernel oops or flushing during shutdown.
class GuestCollector : public Collector {
 public:
  // Create a new, initialized GuestCollector.
  static std::unique_ptr<GuestCollector> Create(base::Closure shutdown_closure);

  static std::unique_ptr<GuestCollector> CreateForTesting(
      base::ScopedFD syslog_fd,
      std::unique_ptr<vm_tools::LogCollector::Stub> stub);

  ~GuestCollector() override;

  bool Init();

 protected:
  // Sends logs through |stub_|.
  bool SendUserLogs() override;

 private:
  // Private default constructor.  Use the static factory function to create new
  // instances of this class.
  explicit GuestCollector(base::Closure shutdown_closure);

  // Initializes this Collector for tests.  Starts listening on the
  // provided file descriptor instead of creating a socket and binding to a
  // path on the file system.
  bool InitForTesting(base::ScopedFD syslog_fd,
                      std::unique_ptr<vm_tools::LogCollector::Stub> stub);

  // Called when |signal_fd_| becomes readable.
  void OnSignalReadable();

  bool EnterJail();

  // File descriptor for receiving signals.
  base::ScopedFD signal_fd_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> signal_controller_;

  // Closure for stopping the MessageLoop.  Posted to the thread's TaskRunner
  // when this program receives a SIGTERM.
  base::Closure shutdown_closure_;

  // Connection to the LogCollector service on the host.
  std::unique_ptr<vm_tools::LogCollector::Stub> stub_;

  base::WeakPtrFactory<GuestCollector> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(GuestCollector);
};

}  // namespace syslog
}  // namespace vm_tools

#endif  // VM_TOOLS_SYSLOG_GUEST_COLLECTOR_H_
