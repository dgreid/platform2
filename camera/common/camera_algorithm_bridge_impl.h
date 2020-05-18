/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_ALGORITHM_BRIDGE_IMPL_H_
#define CAMERA_COMMON_CAMERA_ALGORITHM_BRIDGE_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include <base/synchronization/lock.h>
#include <base/threading/thread.h>

#include "common/camera_algorithm_callback_ops_impl.h"
#include "common/camera_algorithm_ops_impl.h"
#include "cros-camera/camera_algorithm.h"
#include "cros-camera/camera_algorithm_bridge.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/future.h"

namespace cros {

// This is the implementation of CameraAlgorithmBridge interface. It is used
// by the camera HAL process.

class CameraAlgorithmBridgeImpl : public CameraAlgorithmBridge {
 public:
  explicit CameraAlgorithmBridgeImpl(CameraAlgorithmBackend backend);

  ~CameraAlgorithmBridgeImpl();

  // This method registers a callback function for buffer handle return.
  int32_t Initialize(const camera_algorithm_callback_ops_t* callback_ops);

  // Register a buffer to the camera algorithm library and gets
  // the handle associated with it.
  int32_t RegisterBuffer(int buffer_fd);

  // Post a request for the camera algorithm library to process the
  // given buffer.
  void Request(uint32_t req_id,
               const std::vector<uint8_t>& req_header,
               int32_t buffer_handle);

  // Deregisters buffers to the camera algorithm library.
  void DeregisterBuffers(const std::vector<int32_t>& buffer_handles);

 private:
  void PreInitializeOnIpcThread(base::Callback<void(void)> cb);

  void InitializeOnIpcThread(
      const camera_algorithm_callback_ops_t* callback_ops,
      base::Callback<void(int32_t)> cb);

  void OnConnectionErrorOnIpcThread();

  void DestroyOnIpcThread();

  void RegisterBufferOnIpcThread(int buffer_fd,
                                 base::Callback<void(int32_t)> cb);

  void RequestOnIpcThread(uint32_t req_id,
                          std::vector<uint8_t> req_header,
                          int32_t buffer_handle);

  void DeregisterBuffersOnIpcThread(std::vector<int32_t> buffer_handles);

  // The algorithm backend this bridge is created for.
  CameraAlgorithmBackend algo_backend_;

  // Return callback registered by HAL
  const camera_algorithm_callback_ops_t* callback_ops_;

  // Camera Mojo channel manager.
  // We use it to get CameraAlgorithmOpsPtr.
  std::unique_ptr<CameraMojoChannelManager> mojo_channel_manager_;

  // Pointer to local proxy of remote CameraAlgorithmOps interface
  // implementation.
  mojom::CameraAlgorithmOpsPtr interface_ptr_;

  // Pointer to CameraAlgorithmCallbackOpss interface implementation.
  std::unique_ptr<CameraAlgorithmCallbackOpsImpl> cb_impl_;

  // Thread for IPC chores
  base::Thread ipc_thread_;

  // Store observers for future locks
  cros::CancellationRelay relay_;

  DISALLOW_COPY_AND_ASSIGN(CameraAlgorithmBridgeImpl);
};

}  // namespace cros

#endif  // CAMERA_COMMON_CAMERA_ALGORITHM_BRIDGE_IMPL_H_
