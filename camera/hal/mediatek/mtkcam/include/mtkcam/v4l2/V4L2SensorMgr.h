/*
 * Copyright (C) 2019 MediaTek Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_V4L2_V4L2SENSORMGR_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_V4L2_V4L2SENSORMGR_H_

// MTKCAM
#include <mtkcam/aaa/IHal3A.h>
#include <mtkcam/drv/IHalSensor.h>
// V4L2
#include <mtkcam/v4l2/ipc_queue.h>
#include <mtkcam/v4l2/V4L2DriverWorker.h>

#include <memory>

namespace v4l2 {

using NS3Av3::E3ACtrl_IPC_AE_GetSensorParam;
using NS3Av3::E3ACtrl_IPC_AE_GetSensorParamEnable;
using NS3Av3::IHal3A;
using NS3Av3::IPC_SensorParam_T;
using NSCam::IHalSensor;

class VISIBILITY_PUBLIC V4L2SensorWorker
    : public V4L2DriverWorker,
      public IPCQueueClient<IPC_SensorParam_T> {
 public:
  explicit V4L2SensorWorker(uint32_t sensorIdx);
  ~V4L2SensorWorker();

 public:
  void validate();
  void invalidate();

  // re-implementations of V4L2DriverWorker
 protected:
  void job() override;

 public:
  int start() override;
  int stop() override;

  // re-implementations of IPCQueueClient
 protected:
  int _ipc_acquire_param(IPC_SensorParam_T* pResult,
                         uint32_t timeoutMs) override;

 private:
  int m_logLevel;
  std::shared_ptr<IHalSensor> m_pHalSensor;
  std::shared_ptr<IHal3A> m_pHal3A;
  bool m_worker_status;
};

};  // namespace v4l2

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_V4L2_V4L2SENSORMGR_H_
