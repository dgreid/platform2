// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VPN_MANAGER_DAEMON_H_
#define VPN_MANAGER_DAEMON_H_

#include <sys/resource.h>

#include <memory>
#include <string>

#include <base/macros.h>

namespace brillo {

class Process;

}  // namespace brillo

namespace vpn_manager {

// Holds the state of a running daemon process, and allows lookup via
// process id file.  Provides a method for the running process to be
// terminated either expliclty via a method call or implicitly via the
// object destructor.
class Daemon {
 public:
  struct ResourceLimits {
    rlim_t as;
  };

  explicit Daemon(const std::string& pid_file);
  Daemon(const Daemon&) = delete;
  Daemon& operator=(const Daemon&) = delete;

  virtual ~Daemon();

  // Clear any reference to a process, terminating the process if it is
  // still running.
  virtual void ClearProcess();

  // Replace the current process with a new process instance.  Returns
  // the new process.  The process pointer returned is still owned by
  // this object.
  virtual brillo::Process* CreateProcess();

  // Replace the current process with a new process instance.  Returns the new
  // process.  The process pointer returned is still owned by this object.  Note
  // that if setting the resource limits fails, it will fail when the Process is
  // Start()ed, *not* when this method returns.
  virtual brillo::Process* CreateProcessWithResourceLimits(
      const ResourceLimits& limits);

  // Find a process associated with the process-id file.  If one is found,
  // replace the current |process_| instance with this result.  Returns
  // true if a process was found, false otherwise.
  virtual bool FindProcess();

  // Returns true if the stored process is currently running.
  virtual bool IsRunning();

  // Stop the running daemon "nicely", sending it a SIGTERM signal first,
  // then a SIGKILL.  Returns true if the process does not exist or if
  // it was successfully reaped after a SIGTERM, false otherwise.
  virtual bool Terminate();

  // Return the process id of the running daemon.
  virtual pid_t GetPid() const;

 private:
  friend class DaemonTest;

  // Replace the current process with |process|.  Any previous process
  // will be terminated if it is not the same process id as |process|.
  // Returns the underlying pointer managed by |process|.
  brillo::Process* SetProcess(std::unique_ptr<brillo::Process> process);

  // Give daemon time to shut down cleanly after a SIGTERM before killing it
  // in a more decisive fashion.
  static const int kTerminationTimeoutSeconds;

  // Process instance associated with this process.
  std::unique_ptr<brillo::Process> process_;

  // File name where the process id for this daemon is held.
  std::string pid_file_;
};

}  // namespace vpn_manager

#endif  // VPN_MANAGER_DAEMON_H_
