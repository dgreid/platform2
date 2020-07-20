/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/camera_mojo_channel_manager_impl.h"

#include <grp.h>

#include <string>
#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/no_destructor.h>
#include <mojo/core/embedder/embedder.h>

#include "cros-camera/common.h"
#include "cros-camera/constants.h"
#include "cros-camera/ipc_util.h"

namespace cros {

namespace {

constexpr ino_t kInvalidInodeNum = 0;

// Gets the socket file by |socket_path| and checks if it is in correct group
// and has correct permission. Returns |kInvalidInodeNum| if it is invalid.
// Otherwise, returns its inode number.
ino_t GetSocketInodeNumber(const base::FilePath& socket_path) {
  // Ensure that socket file is ready before trying to connect the dispatcher.
  struct group arc_camera_group;
  struct group* result = nullptr;
  char buf[1024];
  if (HANDLE_EINTR(getgrnam_r(constants::kArcCameraGroup, &arc_camera_group,
                              buf, sizeof(buf), &result)) != 0 ||
      !result) {
    // TODO(crbug.com/1053569): Remove the log once we solve the race condition
    // issue.
    LOGF(INFO) << "Failed to get group information of the socket file";
    return kInvalidInodeNum;
  }

  int mode;
  if (!base::GetPosixFilePermissions(socket_path, &mode) || mode != 0660) {
    // TODO(crbug.com/1053569): Remove the log once we solve the race condition
    // issue.
    LOGF(INFO) << "The socket file is not ready (Unexpected permission)";
    return kInvalidInodeNum;
  }

  struct stat st;
  if (stat(socket_path.value().c_str(), &st) ||
      st.st_gid != arc_camera_group.gr_gid) {
    // TODO(crbug.com/1053569): Remove the log once we solve the race condition
    // issue.
    LOGF(INFO) << "The socket file is not ready (Unexpected group id)";
    return kInvalidInodeNum;
  }
  return st.st_ino;
}

}  // namespace

// static
CameraMojoChannelManagerImpl* CameraMojoChannelManagerImpl::instance_ = nullptr;

CameraMojoChannelManagerImpl::CameraMojoChannelManagerImpl()
    : ipc_thread_("MojoIpcThread"), bound_socket_inode_num_(kInvalidInodeNum) {
  instance_ = this;
  if (!ipc_thread_.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOGF(ERROR) << "Failed to start IPC Thread";
    return;
  }
  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      ipc_thread_.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);

  base::FilePath socket_path(constants::kCrosCameraSocketPathString);
  if (!watcher_.Watch(
          socket_path, false,
          base::Bind(&CameraMojoChannelManagerImpl::OnSocketFileStatusChange,
                     base::Unretained(this)))) {
    LOGF(ERROR) << "Failed to watch socket path";
    return;
  }
}

CameraMojoChannelManagerImpl::~CameraMojoChannelManagerImpl() {
  if (ipc_thread_.IsRunning()) {
    ipc_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&CameraMojoChannelManagerImpl::TearDownMojoEnvOnIpcThread,
                   base::Unretained(this)));
    ipc_thread_.Stop();
  }
}

// static
std::unique_ptr<CameraMojoChannelManager>
CameraMojoChannelManager::CreateInstance() {
  return base::WrapUnique<CameraMojoChannelManager>(
      new CameraMojoChannelManagerImpl());
}

// static
CameraMojoChannelManager* CameraMojoChannelManager::GetInstance() {
  DCHECK(CameraMojoChannelManagerImpl::instance_ != nullptr);
  return CameraMojoChannelManagerImpl::instance_;
}

scoped_refptr<base::SingleThreadTaskRunner>
CameraMojoChannelManagerImpl::GetIpcTaskRunner() {
  CHECK(ipc_thread_.task_runner());
  return ipc_thread_.task_runner();
}

void CameraMojoChannelManagerImpl::RegisterServer(
    mojom::CameraHalServerPtr hal_ptr,
    Callback on_construct_callback,
    Callback on_error_callback) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();

  camera_hal_server_request_ = {
      .requestOrPtr = std::move(hal_ptr),
      .on_construct_callback = std::move(on_construct_callback),
      .on_error_callback = std::move(on_error_callback)};
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraMojoChannelManagerImpl::TryConnectToDispatcher,
                 base::Unretained(this)));
}

void CameraMojoChannelManagerImpl::CreateMjpegDecodeAccelerator(
    mojom::MjpegDecodeAcceleratorRequest request,
    Callback on_construct_callback,
    Callback on_error_callback) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();

  PendingMojoRequest<mojom::MjpegDecodeAcceleratorRequest> pending_request = {
      .requestOrPtr = std::move(request),
      .on_construct_callback = std::move(on_construct_callback),
      .on_error_callback = std::move(on_error_callback)};
  jda_requests_.push_back(std::move(pending_request));
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraMojoChannelManagerImpl::TryConnectToDispatcher,
                 base::Unretained(this)));
}

void CameraMojoChannelManagerImpl::CreateJpegEncodeAccelerator(
    mojom::JpegEncodeAcceleratorRequest request,
    Callback on_construct_callback,
    Callback on_error_callback) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();

  PendingMojoRequest<mojom::JpegEncodeAcceleratorRequest> pending_request = {
      .requestOrPtr = std::move(request),
      .on_construct_callback = std::move(on_construct_callback),
      .on_error_callback = std::move(on_error_callback)};
  jea_requests_.push_back(std::move(pending_request));
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraMojoChannelManagerImpl::TryConnectToDispatcher,
                 base::Unretained(this)));
}

mojom::CameraAlgorithmOpsPtr
CameraMojoChannelManagerImpl::CreateCameraAlgorithmOpsPtr(
    const std::string& socket_path, const std::string& pipe_name) {
  VLOGF_ENTER();

  mojo::ScopedMessagePipeHandle parent_pipe;
  mojom::CameraAlgorithmOpsPtr algorithm_ops;

  base::FilePath socket_file_path(socket_path);
  MojoResult result = cros::CreateMojoChannelToChildByUnixDomainSocket(
      socket_file_path, &parent_pipe, pipe_name);
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

void CameraMojoChannelManagerImpl::OnSocketFileStatusChange(
    const base::FilePath& socket_path, bool error) {
  if (error) {
    LOGF(ERROR) << "Error occurs in socket file watcher.";
    return;
  }

  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          &CameraMojoChannelManagerImpl::OnSocketFileStatusChangeOnIpcThread,
          base::Unretained(this)));
}

void CameraMojoChannelManagerImpl::OnSocketFileStatusChangeOnIpcThread() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();

  base::FilePath socket_path(constants::kCrosCameraSocketPathString);
  if (dispatcher_.is_bound()) {
    // If the dispatcher is already bound but the inode number of the socket is
    // unreadable or has been changed, we assume the other side of the
    // dispatcher (Chrome) might be destroyed. As a result, we fire the on error
    // event here in case it is not fired correctly.
    if (bound_socket_inode_num_ != GetSocketInodeNumber(socket_path)) {
      ipc_thread_.task_runner()->PostTask(
          FROM_HERE,
          base::Bind(&CameraMojoChannelManagerImpl::ResetDispatcherPtr,
                     base::Unretained(this)));
    }
    return;
  }

  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraMojoChannelManagerImpl::TryConnectToDispatcher,
                 base::Unretained(this)));
}

void CameraMojoChannelManagerImpl::TryConnectToDispatcher() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();

  if (dispatcher_.is_bound()) {
    TryConsumePendingMojoRequests();
    return;
  }

  base::FilePath socket_path(constants::kCrosCameraSocketPathString);
  ino_t socket_inode_num = GetSocketInodeNumber(socket_path);
  if (socket_inode_num == kInvalidInodeNum) {
    return;
  }

  mojo::ScopedMessagePipeHandle child_pipe;
  MojoResult result = cros::CreateMojoChannelToParentByUnixDomainSocket(
      socket_path, &child_pipe);
  if (result != MOJO_RESULT_OK) {
    LOGF(WARNING) << "Failed to create Mojo Channel to " << socket_path.value();
    return;
  }

  dispatcher_ = mojo::MakeProxy(
      mojom::CameraHalDispatcherPtrInfo(std::move(child_pipe), 0u),
      ipc_thread_.task_runner());
  dispatcher_.set_connection_error_handler(
      base::Bind(&CameraMojoChannelManagerImpl::ResetDispatcherPtr,
                 base::Unretained(this)));
  bound_socket_inode_num_ = socket_inode_num;

  TryConsumePendingMojoRequests();
}

void CameraMojoChannelManagerImpl::TryConsumePendingMojoRequests() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();

  if (camera_hal_server_request_.requestOrPtr) {
    dispatcher_->RegisterServer(
        std::move(camera_hal_server_request_.requestOrPtr));
    std::move(camera_hal_server_request_.on_construct_callback).Run();
  }

  for (auto& request : jda_requests_) {
    if (request.requestOrPtr) {
      dispatcher_->GetMjpegDecodeAccelerator(std::move(request.requestOrPtr));
      std::move(request.on_construct_callback).Run();
    }
  }

  for (auto& request : jea_requests_) {
    if (request.requestOrPtr) {
      dispatcher_->GetJpegEncodeAccelerator(std::move(request.requestOrPtr));
      std::move(request.on_construct_callback).Run();
    }
  }
}

void CameraMojoChannelManagerImpl::TearDownMojoEnvOnIpcThread() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();

  ResetDispatcherPtr();
  ipc_support_.reset();
}

void CameraMojoChannelManagerImpl::ResetDispatcherPtr() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();

  if (camera_hal_server_request_.on_error_callback) {
    std::move(camera_hal_server_request_.on_error_callback).Run();
    camera_hal_server_request_ = {};
  }

  for (auto& request : jda_requests_) {
    if (request.on_error_callback) {
      std::move(request.on_error_callback).Run();
    }
  }
  jda_requests_.clear();

  for (auto& request : jea_requests_) {
    if (request.on_error_callback) {
      std::move(request.on_error_callback).Run();
    }
  }
  jea_requests_.clear();

  dispatcher_.reset();
  bound_socket_inode_num_ = kInvalidInodeNum;
}

}  // namespace cros
