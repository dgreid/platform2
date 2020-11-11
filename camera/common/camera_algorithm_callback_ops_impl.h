/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_ALGORITHM_CALLBACK_OPS_IMPL_H_
#define CAMERA_COMMON_CAMERA_ALGORITHM_CALLBACK_OPS_IMPL_H_

#include <base/single_thread_task_runner.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "cros-camera/camera_algorithm.h"
#include "mojo/algorithm/camera_algorithm.mojom.h"

namespace cros {

// This is the implementation of CameraAlgorithmCallbackOps mojo interface. It
// is used by the camera HAL process.

class CameraAlgorithmCallbackOpsImpl
    : public mojom::CameraAlgorithmCallbackOps {
 public:
  CameraAlgorithmCallbackOpsImpl(
      scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
      const camera_algorithm_callback_ops_t* callback_ops);
  CameraAlgorithmCallbackOpsImpl(const CameraAlgorithmCallbackOpsImpl&) =
      delete;
  CameraAlgorithmCallbackOpsImpl& operator=(
      const CameraAlgorithmCallbackOpsImpl&) = delete;

  ~CameraAlgorithmCallbackOpsImpl() override {}

  // Implementation of mojom::CameraAlgorithmCallbackOps::Return interface. It
  // is expected to be called on |CameraAlgorithmBridgeImpl::ipc_thread_|.
  void Return(uint32_t req_id, uint32_t status, int32_t buffer_handle) override;

  // Create the local proxy of remote CameraAlgorithmCallbackOps interface
  // implementation. It is expected to be called on
  // |CameraAlgorithmBridgeImpl::ipc_thread_|.
  mojom::CameraAlgorithmCallbackOpsPtr CreateInterfacePtr();

 private:
  // Binding of CameraAlgorithmCallbackOps interface to message pipe
  mojo::Binding<mojom::CameraAlgorithmCallbackOps> binding_;

  // Task runner of |CameraAlgorithmBridgeImpl::ipc_thread_|
  const scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner_;

  // Return callback registered by HAL
  const camera_algorithm_callback_ops_t* callback_ops_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_CAMERA_ALGORITHM_CALLBACK_OPS_IMPL_H_
