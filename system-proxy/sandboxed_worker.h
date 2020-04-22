// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SYSTEM_PROXY_SANDBOXED_WORKER_H_
#define SYSTEM_PROXY_SANDBOXED_WORKER_H_

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include <base/files/file_descriptor_watcher_posix.h>
#include <base/files/scoped_file.h>
#include <base/memory/weak_ptr.h>
#include <chromeos/scoped_minijail.h>

namespace system_proxy {

class SystemProxyAdaptor;

class SandboxedWorker {
 public:
  explicit SandboxedWorker(base::WeakPtr<SystemProxyAdaptor> adaptor);
  SandboxedWorker(const SandboxedWorker&) = delete;
  SandboxedWorker& operator=(const SandboxedWorker&) = delete;
  virtual ~SandboxedWorker() = default;

  // Starts a sandboxed worker with pipes.
  virtual bool Start();
  // Sends the username and password to the worker via communication pipes.
  void SetUsernameAndPassword(const std::string& username,
                              const std::string& password);
  // Sends the listening address and port to the worker via communication
  // pipes.
  bool SetListeningAddress(uint32_t addr, int port);

  // Terminates the child process by sending a SIGTERM signal.
  virtual bool Stop();

  virtual bool IsRunning();

  void SetNetNamespaceLifelineFd(base::ScopedFD net_namespace_lifeline_fd);

  pid_t pid() { return pid_; }

 private:
  friend class SystemProxyAdaptorTest;
  FRIEND_TEST(SystemProxyAdaptorTest, SetSystemTrafficCredentials);

  void OnMessageReceived();
  void OnErrorReceived();
  // Called when a proxy resolver job is resolved. |proxy_servers| is the
  // ordered list of proxies returned by Chrome. In case of failure it will be
  // the direct proxy.
  void OnProxyResolved(const std::string& target_url,
                       bool success,
                       const std::vector<std::string>& proxy_servers);

  bool is_being_terminated_ = false;
  ScopedMinijail jail_;
  base::ScopedFD stdin_pipe_;
  base::ScopedFD stdout_pipe_;
  base::ScopedFD stderr_pipe_;

  // The fd will be released when the owning sandbox worker instance is
  // destroyed. Closing this fd will signal to the patchpanel service to tear
  // down the network namespace setup for the associated worker process.
  base::ScopedFD net_namespace_lifeline_fd_;

  std::unique_ptr<base::FileDescriptorWatcher::Controller> stdout_watcher_;
  std::unique_ptr<base::FileDescriptorWatcher::Controller> stderr_watcher_;

  // The adaptor that owns this worker.
  base::WeakPtr<SystemProxyAdaptor> adaptor_;
  pid_t pid_;
  base::WeakPtrFactory<SandboxedWorker> weak_ptr_factory_{this};
};

}  // namespace system_proxy

#endif  // SYSTEM_PROXY_SANDBOXED_WORKER_H_
