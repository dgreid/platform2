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

#define LOG_TAG "v4l2_sensor_mgr"

#include <mtkcam/v4l2/property_strings.h>
#include <mtkcam/v4l2/V4L2SensorMgr.h>

// MTKCAM
#include <cutils/compiler.h>
#include <mtkcam/utils/std/Log.h>  // log
#include <property_lib.h>

// STL
#include <memory>   // std::shared_ptr
#include <thread>   // std::yield
#include <utility>  // std::move

static int g_logLevel = []() {
  return property_get_int32(PROP_V4L2_SENSORMGR_LOGLEVEL, 2);
}();

using NSCam::IHalSensorList;

namespace v4l2 {

V4L2SensorWorker::V4L2SensorWorker(uint32_t sensorIdx)
    : m_logLevel(g_logLevel) {
  // Sensor hal init
  IHalSensorList* pIHalSensorList = GET_HalSensorList();

  // create IHalSensor
  std::shared_ptr<IHalSensor> pHalSensor(
      pIHalSensorList->createSensor(LOG_TAG, sensorIdx),
      [](IHalSensor* p) { p->destroyInstance(LOG_TAG); });
  m_pHalSensor = std::move(pHalSensor);

  // create IHal3A
  MAKE_Hal3A(
      m_pHal3A, [](IHal3A* p) { p->destroyInstance(LOG_TAG); }, sensorIdx,
      LOG_TAG);
}

V4L2SensorWorker::~V4L2SensorWorker() {}

void V4L2SensorWorker::validate() {
  m_worker_status = true;
  m_pHal3A->send3ACtrl(E3ACtrl_IPC_AE_GetSensorParamEnable, 1,
                       0);  // enable by arg1
}

void V4L2SensorWorker::invalidate() {
  m_worker_status = false;
  m_pHal3A->send3ACtrl(E3ACtrl_IPC_AE_GetSensorParamEnable, 0,
                       0);  // disable by arg1
}

int V4L2SensorWorker::start() {
  validate();
  return V4L2DriverWorker::start();
}

int V4L2SensorWorker::stop() {
  invalidate();
  return V4L2DriverWorker::stop();
}

void V4L2SensorWorker::job() {
  // dequeue a sensor setting
  IPC_SensorParam_T s;
  CAM_LOGD_IF(m_logLevel >= 3, "ipc_dequeue [+]");
  int result = ipc_dequeue(&s, 4500);
  CAM_LOGD_IF(m_logLevel >= 3, "ipc_dequeue [-]");
  if (CC_LIKELY(result == 0) && m_worker_status) {
    // configure sensor
    m_pHalSensor->sendCommand(
        s.sensorDev, s.cmd, reinterpret_cast<MUINTPTR>(&s.p1.field),
        sizeof(s.p1.field), reinterpret_cast<MUINTPTR>(&s.p2.field),
        sizeof(s.p2.field), reinterpret_cast<MUINTPTR>(&s.p3.field),
        sizeof(s.p3.field));
  } else {
    std::this_thread::yield();
  }
}

int V4L2SensorWorker::_ipc_acquire_param(IPC_SensorParam_T* pResult,
                                         uint32_t timeoutMs) {
  MBOOL bResult = m_pHal3A->send3ACtrl(E3ACtrl_IPC_AE_GetSensorParam,
                                       reinterpret_cast<MINTPTR>(pResult),
                                       static_cast<uintptr_t>(timeoutMs));

  return bResult == MTRUE ? 0 : -1;
}
};  // namespace v4l2
