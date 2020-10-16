// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/executor.h"

#include <utility>

#include <base/bind.h>
#include <base/logging.h>

namespace hermes {

Executor::Executor(scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : task_runner_(task_runner) {
  CHECK(task_runner_);
}

void Executor::Execute(std::function<void()> f) {
  // TaskRunner::PostTask takes a base::Closure, not a std::function. Convert
  // the captureless lambda into a base::BindState for use as a base::Closure.
  task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce([](std::function<void()> f) { f(); }, std::move(f)));
}

void Executor::PostDelayedTask(const base::Location& from_here,
                               base::OnceClosure task,
                               base::TimeDelta delay) {
  task_runner_->PostDelayedTask(from_here, std::move(task), delay);
}

}  // namespace hermes
