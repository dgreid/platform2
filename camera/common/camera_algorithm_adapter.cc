/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_algorithm_adapter.h"

#include <dlfcn.h>

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/interface_request.h>
#include <mojo/public/cpp/system/invitation.h>

#include "cros-camera/camera_algorithm.h"
#include "cros-camera/common.h"

namespace cros {

namespace {

const char* GetAlgorithmLibraryName(const std::string& pipe_name) {
  // TODO(kamesan): Arrange the library names in some format like
  // libcam_algo_<pipe_name>.so
  if (pipe_name == "vendor_cpu") {
    return "libcam_algo.so";
  }
  if (pipe_name == "vendor_gpu") {
    return "libcam_algo_vendor_gpu.so";
  }
  if (pipe_name == "google_gpu") {
    return "libcam_gpu_algo.so";
  }
  if (pipe_name == "test") {
    return "libcam_algo_test.so";
  }
  NOTREACHED() << "Unknown message pipe name: " << pipe_name;
  return "";
}

}  // namespace

CameraAlgorithmAdapter::CameraAlgorithmAdapter()
    : algo_impl_(CameraAlgorithmOpsImpl::GetInstance()),
      algo_dll_handle_(nullptr),
      ipc_thread_("IPC thread") {}

CameraAlgorithmAdapter::~CameraAlgorithmAdapter() = default;

void CameraAlgorithmAdapter::Run(std::string pipe_name,
                                 base::ScopedFD channel) {
  VLOGF_ENTER();
  auto future = cros::Future<void>::Create(&relay_);
  ipc_lost_cb_ = cros::GetFutureCallback(future);
  ipc_thread_.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraAlgorithmAdapter::InitializeOnIpcThread,
                 base::Unretained(this), pipe_name, base::Passed(&channel)));
  future->Wait(-1);
  exit(EXIT_FAILURE);
}

void CameraAlgorithmAdapter::InitializeOnIpcThread(std::string pipe_name,
                                                   base::ScopedFD channel) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF(1) << "Setting up message pipe, name: " << pipe_name;
  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      ipc_thread_.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);
  mojo::IncomingInvitation invitation = mojo::IncomingInvitation::Accept(
      mojo::PlatformChannelEndpoint(mojo::PlatformHandle(std::move(channel))));
  mojom::CameraAlgorithmOpsRequest request(
      invitation.ExtractMessagePipe(pipe_name));

  VLOGF_ENTER();
  const char* algo_lib_name = GetAlgorithmLibraryName(pipe_name);
  algo_dll_handle_ = dlopen(algo_lib_name, RTLD_NOW);
  if (!algo_dll_handle_) {
    LOGF(ERROR) << "Failed to dlopen: " << dlerror();
    DestroyOnIpcThread();
    return;
  }
  camera_algorithm_ops_t* cam_algo = static_cast<camera_algorithm_ops_t*>(
      dlsym(algo_dll_handle_, CAMERA_ALGORITHM_MODULE_INFO_SYM_AS_STR));
  if (!cam_algo) {
    LOGF(ERROR) << "Camera algorithm is invalid";
    dlclose(algo_dll_handle_);
    DestroyOnIpcThread();
    return;
  }

  base::Closure ipc_lost_handler = base::Bind(
      &CameraAlgorithmAdapter::DestroyOnIpcThread, base::Unretained(this));
  algo_impl_->Bind(std::move(request), cam_algo, ipc_thread_.task_runner(),
                   ipc_lost_handler);
  VLOGF_EXIT();
}

void CameraAlgorithmAdapter::DestroyOnIpcThread() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  algo_impl_->Unbind();
  ipc_support_ = nullptr;
  if (algo_dll_handle_) {
    dlclose(algo_dll_handle_);
  }
  ipc_lost_cb_.Run();
  VLOGF_EXIT();
}

}  // namespace cros
