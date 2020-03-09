/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_mojo_channel_manager_impl.h"

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <mojo/core/embedder/embedder.h>

#include "cros-camera/common.h"
#include "cros-camera/constants.h"
#include "cros-camera/ipc_util.h"

namespace cros {

// static
mojom::CameraHalDispatcherPtr CameraMojoChannelManagerImpl::dispatcher_;
base::Thread* CameraMojoChannelManagerImpl::ipc_thread_ = nullptr;
base::NoDestructor<base::Lock> CameraMojoChannelManagerImpl::static_lock_;
mojo::core::ScopedIPCSupport* CameraMojoChannelManagerImpl::ipc_support_;
bool CameraMojoChannelManagerImpl::mojo_initialized_ = false;

// static
std::unique_ptr<CameraMojoChannelManager>
CameraMojoChannelManager::CreateInstance() {
  return base::WrapUnique<CameraMojoChannelManager>(
      new CameraMojoChannelManagerImpl());
}

CameraMojoChannelManagerImpl::CameraMojoChannelManagerImpl() {
  VLOGF_ENTER();

  cancellation_relay_ = std::make_unique<CancellationRelay>();

  bool success = InitializeMojoEnv();
  CHECK(success);
}

CameraMojoChannelManagerImpl::~CameraMojoChannelManagerImpl() {
  VLOGF_ENTER();
}

void CameraMojoChannelManagerImpl::ConnectToDispatcher(
    base::Closure on_connection_established,
    base::Closure on_connection_error) {
  ipc_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraMojoChannelManagerImpl::ConnectToDispatcherOnIpcThread,
                 base::Unretained(this),
                 base::Passed(std::move(on_connection_established)),
                 base::Passed(std::move(on_connection_error))));
}

scoped_refptr<base::SingleThreadTaskRunner>
CameraMojoChannelManagerImpl::GetIpcTaskRunner() {
  CHECK(CameraMojoChannelManagerImpl::ipc_thread_);
  return CameraMojoChannelManagerImpl::ipc_thread_->task_runner();
}

void CameraMojoChannelManagerImpl::RegisterServer(
    mojom::CameraHalServerPtr hal_ptr) {
  ipc_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraMojoChannelManagerImpl::RegisterServerOnIpcThread,
                 base::Unretained(this), base::Passed(std::move(hal_ptr))));
}

bool CameraMojoChannelManagerImpl::CreateMjpegDecodeAccelerator(
    mojom::MjpegDecodeAcceleratorRequest request) {
  auto is_success = Future<bool>::Create(cancellation_relay_.get());

  ipc_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraMojoChannelManagerImpl::
                     CreateMjpegDecodeAcceleratorOnIpcThread,
                 base::Unretained(this), base::Passed(std::move(request)),
                 GetFutureCallback(is_success)));
  if (!is_success->Wait()) {
    return false;
  }
  return is_success->Get();
}

bool CameraMojoChannelManagerImpl::CreateJpegEncodeAccelerator(
    mojom::JpegEncodeAcceleratorRequest request) {
  auto is_success = Future<bool>::Create(cancellation_relay_.get());

  ipc_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          &CameraMojoChannelManagerImpl::CreateJpegEncodeAcceleratorOnIpcThread,
          base::Unretained(this), base::Passed(std::move(request)),
          GetFutureCallback(is_success)));
  if (!is_success->Wait()) {
    return false;
  }
  return is_success->Get();
}

mojom::CameraAlgorithmOpsPtr
CameraMojoChannelManagerImpl::CreateCameraAlgorithmOpsPtr(
    const std::string& socket_path) {
  VLOGF_ENTER();
  base::AutoLock l(*static_lock_);

  if (!mojo_initialized_) {
    LOGF(WARNING) << "Mojo environment is not initialized";
    return nullptr;
  }

  mojo::ScopedMessagePipeHandle parent_pipe;
  mojom::CameraAlgorithmOpsPtr algorithm_ops;

  base::FilePath socket_file_path(socket_path);
  MojoResult result = cros::CreateMojoChannelToChildByUnixDomainSocket(
      socket_file_path, &parent_pipe);
  if (result != MOJO_RESULT_OK) {
    LOGF(WARNING) << "Failed to create Mojo Channel to "
                  << socket_file_path.value();
    return nullptr;
  }

  algorithm_ops.Bind(
      mojom::CameraAlgorithmOpsPtrInfo(std::move(parent_pipe), 0u));

  LOGF(INFO) << "Connected to CameraAlgorithmOps";

  VLOGF_EXIT();
  return algorithm_ops;
}

bool CameraMojoChannelManagerImpl::InitializeMojoEnv() {
  base::AutoLock l(*static_lock_);

  if (mojo_initialized_) {
    return true;
  }

  ipc_thread_ = new base::Thread("MojoIpcThread");
  if (!ipc_thread_->StartWithOptions(
          base::Thread::Options(base::MessageLoop::TYPE_IO, 0))) {
    LOGF(ERROR) << "Failed to start IPC Thread";
    delete ipc_thread_;
    ipc_thread_ = nullptr;
    return false;
  }
  mojo::core::Init();
  ipc_support_ = new mojo::core::ScopedIPCSupport(
      ipc_thread_->task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);
  mojo_initialized_ = true;
  LOGF(INFO) << "Mojo IPC environment initialized";
  return true;
}

void CameraMojoChannelManagerImpl::EnsureDispatcherConnectedOnIpcThread() {
  DCHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  base::AutoLock l(*static_lock_);

  if (!mojo_initialized_) {
    LOGF(WARNING) << "Mojo environment is not initialized";
    return;
  }

  if (dispatcher_.is_bound()) {
    return;
  }

  mojo::ScopedMessagePipeHandle child_pipe;

  base::FilePath socket_path(constants::kCrosCameraSocketPathString);
  MojoResult result = cros::CreateMojoChannelToParentByUnixDomainSocket(
      socket_path, &child_pipe);
  if (result != MOJO_RESULT_OK) {
    LOGF(WARNING) << "Failed to create Mojo Channel to " << socket_path.value();
    return;
  }

  dispatcher_ = mojo::MakeProxy(
      mojom::CameraHalDispatcherPtrInfo(std::move(child_pipe), 0u),
      ipc_thread_->task_runner());
  dispatcher_.set_connection_error_handler(
      base::Bind(&CameraMojoChannelManagerImpl::OnDispatcherError));

  LOGF(INFO) << "Connected to CameraHalDispatcher";

  VLOGF_EXIT();
}

void CameraMojoChannelManagerImpl::ConnectToDispatcherOnIpcThread(
    base::Closure on_connection_established,
    base::Closure on_connection_error) {
  DCHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

  if (dispatcher_.is_bound()) {
    return;
  }

  EnsureDispatcherConnectedOnIpcThread();
  if (!dispatcher_.is_bound()) {
    on_connection_error.Run();
    return;
  }

  auto callbacks_combined = [](base::Closure callback1,
                               base::Closure callback2) {
    callback1.Run();
    callback2.Run();
  };
  dispatcher_.set_connection_error_handler(
      base::Bind(callbacks_combined,
                 base::Bind(&CameraMojoChannelManagerImpl::OnDispatcherError),
                 std::move(on_connection_error)));
  on_connection_established.Run();
}

void CameraMojoChannelManagerImpl::RegisterServerOnIpcThread(
    mojom::CameraHalServerPtr hal_ptr) {
  DCHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

  EnsureDispatcherConnectedOnIpcThread();
  if (dispatcher_.is_bound()) {
    dispatcher_->RegisterServer(std::move(hal_ptr));
  }
}

void CameraMojoChannelManagerImpl::CreateMjpegDecodeAcceleratorOnIpcThread(
    mojom::MjpegDecodeAcceleratorRequest request,
    base::Callback<void(bool)> callback) {
  DCHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

  EnsureDispatcherConnectedOnIpcThread();
  if (!dispatcher_.is_bound()) {
    callback.Run(false);
    return;
  }
  dispatcher_->GetMjpegDecodeAccelerator(std::move(request));
  callback.Run(true);
}

void CameraMojoChannelManagerImpl::CreateJpegEncodeAcceleratorOnIpcThread(
    mojom::JpegEncodeAcceleratorRequest request,
    base::Callback<void(bool)> callback) {
  DCHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

  EnsureDispatcherConnectedOnIpcThread();
  if (!dispatcher_.is_bound()) {
    callback.Run(false);
    return;
  }
  dispatcher_->GetJpegEncodeAccelerator(std::move(request));
  callback.Run(true);
}

// static
__attribute__((destructor(101))) void
CameraMojoChannelManagerImpl::TearDownMojoEnv() {
  base::AutoLock l(*static_lock_);

  if (!mojo_initialized_) {
    return;
  }
  mojo_initialized_ = false;

  ipc_thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(CameraMojoChannelManagerImpl::TearDownMojoEnvLockedOnThread));
  ipc_thread_->Stop();
  delete ipc_thread_;
  ipc_thread_ = nullptr;
  LOGF(INFO) << "Mojo IPC environment destroyed";
}

// static
void CameraMojoChannelManagerImpl::TearDownMojoEnvLockedOnThread() {
  DCHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());

  if (dispatcher_.is_bound()) {
    dispatcher_.reset();
  }
  delete ipc_support_;
  ipc_support_ = nullptr;
}

// static
void CameraMojoChannelManagerImpl::OnDispatcherError() {
  DCHECK(ipc_thread_->task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  LOGF(ERROR) << "Mojo channel to CameraHalDispatcher is broken";
  dispatcher_.reset();
}

}  // namespace cros
