/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_MOJO_CHANNEL_MANAGER_IMPL_H_
#define CAMERA_COMMON_CAMERA_MOJO_CHANNEL_MANAGER_IMPL_H_

#include <memory>
#include <string>

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
  ~CameraMojoChannelManagerImpl() final;

  void ConnectToDispatcher(base::Closure on_connection_established,
                           base::Closure on_connection_error) final;

  scoped_refptr<base::SingleThreadTaskRunner> GetIpcTaskRunner() final;

  void RegisterServer(mojom::CameraHalServerPtr hal_ptr) final;

  // Creates a new MjpegDecodeAccelerator.
  // This API uses CameraHalDispatcher to pass |request| to another process to
  // create Mojo channel.
  bool CreateMjpegDecodeAccelerator(
      mojom::MjpegDecodeAcceleratorRequest request) final;

  // Creates a new JpegEncodeAccelerator.
  // This API uses CameraHalDispatcher to pass |request| to another process to
  // create Mojo channel.
  bool CreateJpegEncodeAccelerator(
      mojom::JpegEncodeAcceleratorRequest request) final;

  // Create a new CameraAlgorithmOpsPtr.
  // This API uses domain socket to connect to the Algo adapter as a parent to
  // create Mojo channel, and then return mojom::CameraAlgorithmOpsPtr.
  mojom::CameraAlgorithmOpsPtr CreateCameraAlgorithmOpsPtr(
      const std::string& socket_path) final;

 protected:
  friend class CameraMojoChannelManager;

  // Thread for IPC chores.
  static base::Thread* ipc_thread_;

 private:
  bool InitializeMojoEnv();

  // Ensure camera dispatcher Mojo channel connected.
  // It should be called for any public API that needs |dispatcher_|.
  void EnsureDispatcherConnectedOnIpcThread();

  void ConnectToDispatcherOnIpcThread(base::Closure on_connection_established,
                                      base::Closure on_connection_error);

  void RegisterServerOnIpcThread(mojom::CameraHalServerPtr hal_ptr);

  void CreateMjpegDecodeAcceleratorOnIpcThread(
      mojom::MjpegDecodeAcceleratorRequest request,
      base::Callback<void(bool)> callback);

  void CreateJpegEncodeAcceleratorOnIpcThread(
      mojom::JpegEncodeAcceleratorRequest request,
      base::Callback<void(bool)> callback);

  static void TearDownMojoEnv();

  static void TearDownMojoEnvLockedOnThread();

  // Resets the dispatcher.
  static void ResetDispatcherPtr();

  // The Mojo channel to CameraHalDispatcher in Chrome. All the Mojo
  // communication to |dispatcher_| happens on |ipc_thread_|.
  static mojom::CameraHalDispatcherPtr dispatcher_;

  // Used to cancel pending futures when error occurs.
  std::unique_ptr<cros::CancellationRelay> cancellation_relay_;

  static ino_t bound_socket_inode_num_;

  // A mutex to guard static variable.
  static base::NoDestructor<base::Lock> static_lock_;
  static mojo::core::ScopedIPCSupport* ipc_support_;
  static bool mojo_initialized_;

  DISALLOW_COPY_AND_ASSIGN(CameraMojoChannelManagerImpl);
};

}  // namespace cros
#endif  // CAMERA_COMMON_CAMERA_MOJO_CHANNEL_MANAGER_IMPL_H_
