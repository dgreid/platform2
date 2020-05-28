// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_SIGCHLD_HANDLER_H_
#define VM_TOOLS_CONCIERGE_SIGCHLD_HANDLER_H_

#include <map>
#include <memory>

#include "vm_tools/concierge/future.h"

namespace vm_tools {
namespace concierge {

class SigchldHandler : public std::enable_shared_from_this<SigchldHandler> {
 public:
  SigchldHandler() = default;

  // Returns a future that will be fulfilled before or right after the timeout.
  // True if SigchldReceived is called with the pid or the process has already
  // been exited before calling this function. False if SigchldReceived is not
  // called before the timeout
  Future<bool> GetFutureForProc(pid_t pid, base::TimeDelta timeout);

  // Report to this class that the process has exited. Set the associated
  // promise to true. Return false if the pid was not registered
  bool Received(pid_t pid) {
    return HandlerHelper(pid, true, "Sigchld received for pid:");
  }

  // Set the associated promise to false. Remove the pid from the map.
  // Return false if the pid was not registered
  bool Cancel(pid_t pid) {
    return HandlerHelper(pid, false, "Sigchild handler cancelled for pid:");
  }

  // Remove when C++17 is available
  std::weak_ptr<SigchldHandler> weak_from_this() { return shared_from_this(); }

 private:
  // Set the associated promise to false. Remove the pid from the map.
  // Return false if the pid was not registered
  bool Timeout(pid_t pid) {
    return HandlerHelper(pid, false, "Sigchld did not come in time for pid:");
  }

  bool HandlerHelper(pid_t pid, bool result, const char* msg);

  using PromiseMap = std::map<pid_t, Promise<bool>>;
  PromiseMap promise_map_;

  // Ensure calls are made on the right thread.
  base::SequenceChecker sequence_checker_;

  DISALLOW_COPY_AND_ASSIGN(SigchldHandler);
};

}  // namespace concierge
}  // namespace vm_tools

#endif  // VM_TOOLS_CONCIERGE_SIGCHLD_HANDLER_H_
