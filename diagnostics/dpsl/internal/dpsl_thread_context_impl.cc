// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/dpsl/internal/dpsl_thread_context_impl.h"

#include <utility>

#include <base/lazy_instance.h>
#include <base/location.h>
#include <base/logging.h>
#include <base/threading/thread_local.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>

#include "diagnostics/dpsl/internal/callback_utils.h"

namespace diagnostics {

namespace {

// Whether an instance of DpslThreadContextImpl was created on the current
// thread.
base::LazyInstance<base::ThreadLocalBoolean>::Leaky
    g_thread_context_impl_created = LAZY_INSTANCE_INITIALIZER;

}  // namespace

// static
void DpslThreadContextImpl::CleanThreadCounterForTesting() {
  g_thread_context_impl_created.Pointer()->Set(false);
}

DpslThreadContextImpl::DpslThreadContextImpl()
    : thread_id_(base::PlatformThread::CurrentId()),
      // Initialize the SingleThreadTaskExecutor only if there's no TaskRunner
      // yet (it could be already set up by the calling code via other means,
      // e.g., brillo::Daemon).
      owned_task_executor_(
          base::ThreadTaskRunnerHandle::IsSet()
              ? nullptr
              : std::make_unique<base::SingleThreadTaskExecutor>(
                    base::MessagePumpType::IO)),
      task_runner_(base::ThreadTaskRunnerHandle::Get()) {}

DpslThreadContextImpl::~DpslThreadContextImpl() {
  CHECK(sequence_checker_.CalledOnValidSequence())
      << "Called from wrong thread";
}

bool DpslThreadContextImpl::BelongsToCurrentThread() {
  return base::PlatformThread::CurrentId() == thread_id_;
}

void DpslThreadContextImpl::RunEventLoop() {
  CHECK(sequence_checker_.CalledOnValidSequence())
      << "Called from wrong thread";
  CHECK(!base::RunLoop::IsRunningOnCurrentThread())
      << "Called from already running message loop";

  CHECK(!current_run_loop_);
  base::RunLoop run_loop;
  current_run_loop_ = &run_loop;

  run_loop.Run();

  current_run_loop_ = nullptr;
}

bool DpslThreadContextImpl::IsEventLoopRunning() {
  CHECK(sequence_checker_.CalledOnValidSequence())
      << "Called from wrong thread";
  return current_run_loop_ != nullptr;
}

void DpslThreadContextImpl::PostTask(std::function<void()> task) {
  task_runner_->PostTask(FROM_HERE,
                         MakeCallbackFromStdFunction(std::move(task)));
}

void DpslThreadContextImpl::PostDelayedTask(std::function<void()> task,
                                            int64_t delay_milliseconds) {
  CHECK_GE(delay_milliseconds, 0) << "Delay must be non-negative";
  task_runner_->PostDelayedTask(
      FROM_HERE, MakeCallbackFromStdFunction(std::move(task)),
      base::TimeDelta::FromMilliseconds(delay_milliseconds));
}

void DpslThreadContextImpl::QuitEventLoop() {
  CHECK(sequence_checker_.CalledOnValidSequence())
      << "Called from wrong thread";

  if (current_run_loop_)
    current_run_loop_->Quit();
}

// static
std::unique_ptr<DpslThreadContext> DpslThreadContext::Create(
    DpslGlobalContext* global_context) {
  CHECK(global_context) << "GlobalContext is nullptr";

  // Verify we're not called twice on the current thread.
  CHECK(!g_thread_context_impl_created.Pointer()->Get())
      << "Duplicate DpslThreadContext instances constructed on the same thread";
  g_thread_context_impl_created.Pointer()->Set(true);

  return std::make_unique<DpslThreadContextImpl>();
}

}  // namespace diagnostics
