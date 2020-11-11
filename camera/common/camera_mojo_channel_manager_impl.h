/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_MOJO_CHANNEL_MANAGER_IMPL_H_
#define CAMERA_COMMON_CAMERA_MOJO_CHANNEL_MANAGER_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_path_watcher.h>
#include <base/no_destructor.h>
#include <base/synchronization/lock.h>
#include <base/threading/thread.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/future.h"
#include "mojo/cros_camera_service.mojom.h"

namespace cros {

class CameraMojoChannelManagerImpl final : public CameraMojoChannelManager {
 public:
  CameraMojoChannelManagerImpl();
  CameraMojoChannelManagerImpl(const CameraMojoChannelManagerImpl&) = delete;
  CameraMojoChannelManagerImpl& operator=(const CameraMojoChannelManagerImpl&) =
      delete;

  ~CameraMojoChannelManagerImpl();

  // CameraMojoChannelManager implementations.

  scoped_refptr<base::SingleThreadTaskRunner> GetIpcTaskRunner();

  void RegisterServer(mojom::CameraHalServerPtr hal_ptr,
                      Callback on_construct_callback,
                      Callback on_error_callback);

  void CreateMjpegDecodeAccelerator(
      mojom::MjpegDecodeAcceleratorRequest request,
      Callback on_construct_callback,
      Callback on_error_callback);

  void CreateJpegEncodeAccelerator(mojom::JpegEncodeAcceleratorRequest request,
                                   Callback on_construct_callback,
                                   Callback on_error_callback);

  mojom::CameraAlgorithmOpsPtr CreateCameraAlgorithmOpsPtr(
      const std::string& socket_path, const std::string& pipe_name);

 protected:
  friend class CameraMojoChannelManager;

  // Thread for IPC chores.
  base::Thread ipc_thread_;

 private:
  template <typename T>
  struct PendingMojoRequest {
    T requestOrPtr;
    Callback on_construct_callback;
    Callback on_error_callback;
  };

  void OnSocketFileStatusChange(const base::FilePath& socket_path, bool error);

  // Callback method for the unix domain socket file change events.  The method
  // will try to establish the Mojo connection to the CameraHalDispatcher
  // started by Chrome.
  void OnSocketFileStatusChangeOnIpcThread();

  void TryConnectToDispatcher();

  void TryConsumePendingMojoRequests();

  void TearDownMojoEnvOnIpcThread();

  // Reset the dispatcher.
  void ResetDispatcherPtr();

  // The Mojo channel to CameraHalDispatcher in Chrome. All the Mojo
  // communication to |dispatcher_| happens on |ipc_thread_|.
  mojom::CameraHalDispatcherPtr dispatcher_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  // Watches for change events on the unix domain socket file created by Chrome.
  // Upon file change OnSocketFileStatusChange will be called to initiate
  // connection to CameraHalDispatcher.
  base::FilePathWatcher watcher_;

  // Inode number of current bound socket file.
  ino_t bound_socket_inode_num_;

  // Pending Mojo requests information which should be consumed when the
  // |dispatcher_| is connected.
  PendingMojoRequest<mojom::CameraHalServerPtr> camera_hal_server_request_;
  std::vector<PendingMojoRequest<mojom::JpegEncodeAcceleratorRequest>>
      jea_requests_;
  std::vector<PendingMojoRequest<mojom::MjpegDecodeAcceleratorRequest>>
      jda_requests_;

  // TODO(b/151270948): Remove this static variable once we implemnet CrOS
  // specific interface on all camera HALs.
  static CameraMojoChannelManagerImpl* instance_;
};

}  // namespace cros
#endif  // CAMERA_COMMON_CAMERA_MOJO_CHANNEL_MANAGER_IMPL_H_
