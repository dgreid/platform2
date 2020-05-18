/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_algorithm_bridge_impl.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <mojo/public/cpp/system/platform_handle.h>

#include "cros-camera/common.h"
#include "cros-camera/constants.h"

namespace cros {

// static
std::unique_ptr<CameraAlgorithmBridge> CameraAlgorithmBridge::CreateInstance(
    CameraAlgorithmBackend backend) {
  VLOGF_ENTER();
  return std::make_unique<CameraAlgorithmBridgeImpl>(backend);
}

CameraAlgorithmBridgeImpl::CameraAlgorithmBridgeImpl(
    CameraAlgorithmBackend backend)
    : algo_backend_(backend),
      callback_ops_(nullptr),
      ipc_thread_("IPC thread") {
  mojo_channel_manager_ = CameraMojoChannelManager::CreateInstance();
}

CameraAlgorithmBridgeImpl::~CameraAlgorithmBridgeImpl() {
  VLOGF_ENTER();
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&CameraAlgorithmBridgeImpl::DestroyOnIpcThread,
                            base::Unretained(this)));
  ipc_thread_.Stop();
  VLOGF_EXIT();
}

int32_t CameraAlgorithmBridgeImpl::Initialize(
    const camera_algorithm_callback_ops_t* callback_ops) {
  VLOGF_ENTER();
  if (!ipc_thread_.Start()) {
    LOGF(ERROR) << "Failed to start IPC thread";
    return -EFAULT;
  }
  const int32_t kInitializationRetryTimeoutMs = 3000;
  const int32_t kInitializationWaitConnectionMs = 500;
  const int32_t kInitializationRetrySleepUs = 100000;
  auto get_elapsed_ms = [](struct timespec& start) {
    struct timespec stop = {};
    if (clock_gettime(CLOCK_MONOTONIC, &stop)) {
      LOG(ERROR) << "Failed to get clock time";
      return 0L;
    }
    return (stop.tv_sec - start.tv_sec) * 1000 +
           (stop.tv_nsec - start.tv_nsec) / 1000000;
  };
  struct timespec start_ts = {};
  if (clock_gettime(CLOCK_MONOTONIC, &start_ts)) {
    LOG(ERROR) << "Failed to get clock time";
  }
  int ret = 0;
  do {
    int32_t elapsed_ms = get_elapsed_ms(start_ts);
    if (elapsed_ms >= kInitializationRetryTimeoutMs) {
      ret = -ETIMEDOUT;
      break;
    }
    auto future = cros::Future<int32_t>::Create(&relay_);
    ipc_thread_.task_runner()->PostTask(
        FROM_HERE, base::Bind(&CameraAlgorithmBridgeImpl::InitializeOnIpcThread,
                              base::Unretained(this), callback_ops,
                              cros::GetFutureCallback(future)));
    if (future->Wait(std::min(kInitializationWaitConnectionMs,
                              kInitializationRetryTimeoutMs - elapsed_ms))) {
      ret = future->Get();
      if (ret == 0 || ret == -EINVAL) {
        break;
      }
    }
    usleep(kInitializationRetrySleepUs);
  } while (1);
  VLOGF_EXIT();
  return ret;
}

int32_t CameraAlgorithmBridgeImpl::RegisterBuffer(int buffer_fd) {
  VLOGF_ENTER();
  auto future = cros::Future<int32_t>::Create(&relay_);
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraAlgorithmBridgeImpl::RegisterBufferOnIpcThread,
                 base::Unretained(this), buffer_fd,
                 cros::GetFutureCallback(future)));
  future->Wait();
  VLOGF_EXIT();
  return future->Get();
}

void CameraAlgorithmBridgeImpl::Request(uint32_t req_id,
                                        const std::vector<uint8_t>& req_header,
                                        int32_t buffer_handle) {
  VLOGF_ENTER();
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraAlgorithmBridgeImpl::RequestOnIpcThread,
                 base::Unretained(this), req_id, req_header, buffer_handle));
  VLOGF_EXIT();
}

void CameraAlgorithmBridgeImpl::DeregisterBuffers(
    const std::vector<int32_t>& buffer_handles) {
  VLOGF_ENTER();
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraAlgorithmBridgeImpl::DeregisterBuffersOnIpcThread,
                 base::Unretained(this), buffer_handles));
  VLOGF_EXIT();
}

void CameraAlgorithmBridgeImpl::InitializeOnIpcThread(
    const camera_algorithm_callback_ops_t* callback_ops,
    base::Callback<void(int32_t)> cb) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (!callback_ops || !callback_ops->return_callback) {
    cb.Run(-EINVAL);
    return;
  }
  if (cb_impl_) {
    LOGF(WARNING)
        << "Camera algorithm bridge is already initialized. Reinitializing...";
    DestroyOnIpcThread();
  }

  switch (algo_backend_) {
    case CameraAlgorithmBackend::kVendorCpu:
      interface_ptr_ = mojo_channel_manager_->CreateCameraAlgorithmOpsPtr(
          cros::constants::kCrosCameraAlgoSocketPathString, "vendor_cpu");
      break;
    case CameraAlgorithmBackend::kGoogleGpu:
      interface_ptr_ = mojo_channel_manager_->CreateCameraAlgorithmOpsPtr(
          cros::constants::kCrosCameraGPUAlgoSocketPathString, "google_gpu");
      break;
    case CameraAlgorithmBackend::kTest:
      interface_ptr_ = mojo_channel_manager_->CreateCameraAlgorithmOpsPtr(
          cros::constants::kCrosCameraAlgoSocketPathString, "test");
      break;
  }
  if (!interface_ptr_) {
    LOGF(ERROR) << "Failed to connect to the server";
    cb.Run(-EAGAIN);
    return;
  }
  interface_ptr_.set_connection_error_handler(
      base::Bind(&CameraAlgorithmBridgeImpl::OnConnectionErrorOnIpcThread,
                 base::Unretained(this)));
  cb_impl_.reset(new CameraAlgorithmCallbackOpsImpl(ipc_thread_.task_runner(),
                                                    callback_ops));
  interface_ptr_->Initialize(cb_impl_->CreateInterfacePtr(), cb);
  callback_ops_ = callback_ops;
  VLOGF_EXIT();
}

void CameraAlgorithmBridgeImpl::OnConnectionErrorOnIpcThread() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  DCHECK(callback_ops_);
  VLOGF_ENTER();
  DestroyOnIpcThread();
  if (callback_ops_->notify) {
    callback_ops_->notify(callback_ops_, CAMERA_ALGORITHM_MSG_IPC_ERROR);
  }
  VLOGF_EXIT();
}

void CameraAlgorithmBridgeImpl::DestroyOnIpcThread() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (interface_ptr_.is_bound()) {
    cb_impl_.reset();
    interface_ptr_.reset();
  }
  VLOGF_EXIT();
}

void CameraAlgorithmBridgeImpl::RegisterBufferOnIpcThread(
    int buffer_fd, base::Callback<void(int32_t)> cb) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (!interface_ptr_.is_bound()) {
    LOGF(ERROR) << "Interface is not bound probably because IPC is broken";
    cb.Run(-ECONNRESET);
    return;
  }
  int dup_fd = dup(buffer_fd);
  if (dup_fd < 0) {
    PLOGF(ERROR) << "Failed to dup fd: ";
    cb.Run(-errno);
    return;
  }
  interface_ptr_->RegisterBuffer(mojo::WrapPlatformFile(dup_fd), cb);
}

void CameraAlgorithmBridgeImpl::RequestOnIpcThread(
    uint32_t req_id, std::vector<uint8_t> req_header, int32_t buffer_handle) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (!interface_ptr_.is_bound()) {
    LOGF(ERROR) << "Interface is not bound probably because IPC is broken";
    return;
  }
  interface_ptr_->Request(req_id, std::move(req_header), buffer_handle);
  VLOGF_EXIT();
}

void CameraAlgorithmBridgeImpl::DeregisterBuffersOnIpcThread(
    std::vector<int32_t> buffer_handles) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  if (!interface_ptr_.is_bound()) {
    LOGF(ERROR) << "Interface is not bound probably because IPC is broken";
    return;
  }
  interface_ptr_->DeregisterBuffers(std::move(buffer_handles));
}

}  // namespace cros
