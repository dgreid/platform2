// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/event_dispatcher.h"

#include <base/run_loop.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>

using base::Callback;
using base::Closure;
using base::Location;

namespace shill {

EventDispatcher::EventDispatcher() = default;

EventDispatcher::~EventDispatcher() = default;

void EventDispatcher::DispatchForever() {
  base::RunLoop run_loop;
  quit_closure_ = run_loop.QuitWhenIdleClosure();
  run_loop.Run();
}

void EventDispatcher::DispatchPendingEvents() {
  base::RunLoop().RunUntilIdle();
}

void EventDispatcher::PostTask(const Location& location, const Closure& task) {
  PostDelayedTask(FROM_HERE, task, 0);
}

void EventDispatcher::PostDelayedTask(const Location& location,
                                      const Closure& task,
                                      int64_t delay_ms) {
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      location, task, base::TimeDelta::FromMilliseconds(delay_ms));
}

void EventDispatcher::QuitDispatchForever() {
  PostTask(FROM_HERE, quit_closure_);
}

}  // namespace shill
