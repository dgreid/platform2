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

#define LOG_TAG "sb_p1_3a_cb"

#include <mtkcam/v4l2/property_strings.h>
#include <mtkcam/v4l2/V4L2P13ACallback.h>

// MTKCAM
#include <mtkcam/utils/std/Log.h>  // log
#include <property_lib.h>

// STL
#include <thread>

#include <cutils/compiler.h>  // for CC_LIKELY, CC_UNLIKELY

static int g_logLevel = []() {
  return property_get_int32(PROP_V4L2_P13ACALLBACK_LOGLEVEL, 2);
}();

namespace v4l2 {

V4L2P13ACallback::V4L2P13ACallback(int sensorIdx, IHal3ACb* notifier)
    : m_pNotifier(notifier), m_logLevel(g_logLevel) {
  // create IHal3A
  MAKE_Hal3A(
      m_pHal3A, [](IHal3A* p) { p->destroyInstance(LOG_TAG); }, sensorIdx,
      LOG_TAG);
}

V4L2P13ACallback::~V4L2P13ACallback() {}

void V4L2P13ACallback::ack() {
  constexpr const int ACK = NS3Av3::IPC_P1NotifyCb_T::ACK;
  m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_NotifyCb, ACK, 0);
}

void V4L2P13ACallback::validate() {
  m_worker_status = true;
  m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_NotifyCbEnable, 1, 0);  // enable by arg1
}

void V4L2P13ACallback::invalidate() {
  m_worker_status = false;
  m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_NotifyCbEnable, 0, 0);  // disable by arg1
}

int V4L2P13ACallback::start() {
  this->validate();
  return V4L2DriverWorker::start();
}

int V4L2P13ACallback::stop() {
  this->invalidate();
  return V4L2DriverWorker::stop();
}

void V4L2P13ACallback::job() {
  IPC_P1NotifyCb_T param;

  CAM_LOGD_IF(m_logLevel >= 3, "ipc_dequeue [+]");
  int result = ipc_dequeue(&param, 3000);
  CAM_LOGD_IF(m_logLevel >= 3, "ipc_dequeue [-]");

  if (CC_UNLIKELY(result != 0)) {
    if (m_worker_status)
      CAM_LOGW("ipc_dequeue returns fail(%d)", result);
    std::this_thread::yield();
    return;
  }

  // synchroized call
  if (param.u4CapType == IHal3ACb::eID_NOTIFY_3APROC_FINISH) {
    CAM_LOGI("trigger eID_NOTIFY_3APROC_FINISH");
    IPC_P1NotifyCb_Proc_Finish_T* q = &param.u.proc_finish;
    // notify
    m_pNotifier->doNotifyCb(IHal3ACb::eID_NOTIFY_3APROC_FINISH,
                            reinterpret_cast<MINTPTR>(q->pRequestResult),
                            q->magicnum,
                            reinterpret_cast<MINTPTR>(q->pCapParam));
  } else if (param.u4CapType == IHal3ACb::eID_NOTIFY_VSYNC_DONE) {
    CAM_LOGI("trigger eID_NOTIFY_VSYNC_DONE");
    m_pNotifier->doNotifyCb(IHal3ACb::eID_NOTIFY_VSYNC_DONE, 0, 0, 0);
  } else {
    CAM_LOGW("dequeued IHal3A's response, but not support type=%#x",
             param.u4CapType);
    std::this_thread::yield();  // hint to reschedule
    return;
  }

  // ack after done
  CAM_LOGD_IF(m_logLevel >= 3, "ack IHal3A [+]");
  ack();
  CAM_LOGD_IF(m_logLevel >= 3, "ack IHal3A [-]");
}

int V4L2P13ACallback::_ipc_acquire_param(IPC_P1NotifyCb_T* pResult,
                                         uint32_t /* timeoutMs */
) {
  constexpr const int WAIT_3A_PROC_FINISHED =
      NS3Av3::IPC_P1NotifyCb_T::WAIT_3A_PROC_FINISHED;

  // ask for data
  auto bResult =
      m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_NotifyCb, WAIT_3A_PROC_FINISHED,
                           reinterpret_cast<uintptr_t>(pResult));

  return bResult == MTRUE ? 0 : -1;
}
};  // namespace v4l2
