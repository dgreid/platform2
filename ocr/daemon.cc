// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ocr/daemon.h"

#include <sysexits.h>

#include <base/logging.h>
#include <base/threading/thread_task_runner_handle.h>
#include <mojo/core/embedder/embedder.h>

namespace ocr {

OcrDaemon::OcrDaemon() = default;

OcrDaemon::~OcrDaemon() = default;

int OcrDaemon::OnInit() {
  int return_code = brillo::DBusDaemon::OnInit();
  if (return_code != EX_OK)
    return return_code;

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get() /* io_thread_task_runner */,
      mojo::core::ScopedIPCSupport::ShutdownPolicy::
          CLEAN /* blocking shutdown */);
  VLOG(0) << "Daemon successfully initialized";
  return EX_OK;
}

}  // namespace ocr
