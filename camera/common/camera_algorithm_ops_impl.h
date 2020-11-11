/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_CAMERA_ALGORITHM_OPS_IMPL_H_
#define CAMERA_COMMON_CAMERA_ALGORITHM_OPS_IMPL_H_

#include <vector>

#include <base/threading/thread.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "cros-camera/camera_algorithm.h"
#include "mojo/algorithm/camera_algorithm.mojom.h"

namespace cros {

// This is the implementation of CameraAlgorithmOps mojo interface. It is used
// by the sandboxed camera algorithm library process.

class CameraAlgorithmOpsImpl : public mojom::CameraAlgorithmOps,
                               private camera_algorithm_callback_ops_t {
 public:
  CameraAlgorithmOpsImpl(const CameraAlgorithmOpsImpl&) = delete;
  CameraAlgorithmOpsImpl& operator=(const CameraAlgorithmOpsImpl&) = delete;

  // Get singleton instance
  static CameraAlgorithmOpsImpl* GetInstance();

  // Completes a binding by removing the message pipe endpoint from |request|
  // and binding it to the interface implementation.
  bool Bind(mojom::CameraAlgorithmOpsRequest request,
            camera_algorithm_ops_t* cam_algo,
            scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner,
            const base::Closure& ipc_lost_handler);

  // Unbinds the underlying pipe.
  void Unbind();

  // Implementation of mojom::CameraAlgorithmOps::Initialize interface
  void Initialize(mojom::CameraAlgorithmCallbackOpsPtr callbacks,
                  InitializeCallback callback) override;

  // Implementation of mojom::CameraAlgorithmOps::RegisterBuffer interface
  void RegisterBuffer(mojo::ScopedHandle buffer_fd,
                      RegisterBufferCallback callback) override;

  // Implementation of mojom::CameraAlgorithmOps::Request interface
  void Request(uint32_t req_id,
               const std::vector<uint8_t>& req_headers,
               int32_t buffer_handle) override;

  // Implementation of mojom::CameraAlgorithmOps::DeregisterBuffers interface
  void DeregisterBuffers(const std::vector<int32_t>& buffer_handles) override;

 private:
  CameraAlgorithmOpsImpl();

  ~CameraAlgorithmOpsImpl() override {}

  static void ReturnCallbackForwarder(
      const camera_algorithm_callback_ops_t* callback_ops,
      uint32_t req_id,
      uint32_t status,
      int32_t buffer_handle);

  void ReturnCallbackOnIPCThread(uint32_t req_id,
                                 uint32_t status,
                                 int32_t buffer_handle);

  // Binding of CameraAlgorithmOps interface to message pipe
  mojo::Binding<mojom::CameraAlgorithmOps> binding_;

  // Interface of camera algorithm library
  camera_algorithm_ops_t* cam_algo_;

  // Pointer to self for ReturnCallbackForwarder to get the singleton instance
  static CameraAlgorithmOpsImpl* singleton_;

  // Task runner of |CameraAlgorithmAdapter::ipc_thread_|
  scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner_;

  // Pointer to local proxy of remote CameraAlgorithmCallback interface
  // implementation
  mojom::CameraAlgorithmCallbackOpsPtr cb_ptr_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_CAMERA_ALGORITHM_OPS_IMPL_H_
