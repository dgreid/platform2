/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_CAMERA3_CALLBACK_OPS_DELEGATE_H_
#define CAMERA_HAL_ADAPTER_CAMERA3_CALLBACK_OPS_DELEGATE_H_

#include <hardware/camera3.h>

#include "common/utils/cros_camera_mojo_utils.h"
#include "mojo/camera3.mojom.h"

namespace cros {

class CameraDeviceAdapter;

class Camera3CallbackOpsDelegate
    : public internal::MojoChannel<mojom::Camera3CallbackOps> {
 public:
  explicit Camera3CallbackOpsDelegate(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  ~Camera3CallbackOpsDelegate() = default;

  void ProcessCaptureResult(mojom::Camera3CaptureResultPtr result);

  void Notify(mojom::Camera3NotifyMsgPtr msg);

 private:
  void ProcessCaptureResultOnThread(mojom::Camera3CaptureResultPtr result);

  void NotifyOnThread(mojom::Camera3NotifyMsgPtr msg);

  DISALLOW_IMPLICIT_CONSTRUCTORS(Camera3CallbackOpsDelegate);
};

}  // end of namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA3_CALLBACK_OPS_DELEGATE_H_
