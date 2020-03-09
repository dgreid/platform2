// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SYSTEM_PROXY_SANDBOXED_WORKER_H_
#define SYSTEM_PROXY_SANDBOXED_WORKER_H_

#include <array>
#include <memory>
#include <string>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <chromeos/scoped_minijail.h>

namespace system_proxy {

class SandboxedWorker {
 public:
  SandboxedWorker();
  SandboxedWorker(const SandboxedWorker&) = delete;
  SandboxedWorker& operator=(const SandboxedWorker&) = delete;
  virtual ~SandboxedWorker() = default;

  // Starts a sandboxed worker with pipes.
  virtual void Start();
  // Sends the username and password to the worker via communication pipes.
  void SetUsernameAndPassword(const std::string& username,
                              const std::string& password);
  // Sends the listening address and port to the worker via communication
  // pipes.
  void SetListeningAddress(uint32_t addr, int port);

  // Terminates the child process by sending a SIGTERM signal.
  virtual bool Stop();

  virtual bool IsRunning();

  pid_t pid() { return pid_; }

 private:
  friend class SystemProxyAdaptorTest;
  FRIEND_TEST(SystemProxyAdaptorTest, SetSystemTrafficCredentials);

  void OnMessageReceived();
  void OnErrorReceived();

  bool is_being_terminated_ = false;
  ScopedMinijail jail_;
  base::ScopedFD stdin_pipe_;
  base::ScopedFD stdout_pipe_;
  base::ScopedFD stderr_pipe_;

  std::unique_ptr<base::FileDescriptorWatcher::Controller> stdout_watcher_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> stderr_watcher_;

  pid_t pid_;
};

}  // namespace system_proxy

#endif  // SYSTEM_PROXY_SANDBOXED_WORKER_H_
