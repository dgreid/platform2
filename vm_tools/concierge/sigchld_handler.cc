// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/threading/sequenced_task_runner_handle.h>
#include <sys/wait.h>
#include <utility>

#include "vm_tools/concierge/grpc_future_util.h"
#include "vm_tools/concierge/sigchld_handler.h"

namespace vm_tools {
namespace concierge {

Future<bool> SigchldHandler::GetFutureForProc(pid_t pid,
                                              base::TimeDelta timeout) {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  pid_t ret = waitpid(pid, nullptr, WNOHANG);
  if (ret == pid || (ret < 0 && errno == ECHILD)) {
    // Either the child exited or it doesn't exist anymore.
    return ResolvedFuture(true);
  }

  Promise<bool> p;
  Future<bool> f = p.GetFuture(base::SequencedTaskRunnerHandle::Get());
  promise_map_[pid] = std::move(p);
  base::SequencedTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](std::weak_ptr<SigchldHandler> weak_handler, pid_t pid) {
            std::shared_ptr<SigchldHandler> handler = weak_handler.lock();
            if (!handler) {
              LOG(WARNING) << "SigchldHandler has already been destroyed.";
              return;
            }
            handler->Timeout(pid);
          },
          weak_from_this(), pid),
      timeout);
  return f;
}

bool SigchldHandler::HandlerHelper(pid_t pid, bool result, const char* msg) {
  DCHECK(sequence_checker_.CalledOnValidSequence());

  auto promiseIter = promise_map_.find(pid);
  if (promiseIter != promise_map_.end()) {
    LOG(INFO) << msg << pid;
    promiseIter->second.SetValue(result);
    promise_map_.erase(promiseIter);
    return true;
  }
  return false;
  // Should we cancel the delayed task?
}

}  // namespace concierge
}  // namespace vm_tools
