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

#define LOG_TAG "v4l2_hw_event_mgr"

#include <mtkcam/v4l2/property_strings.h>
#include <mtkcam/v4l2/V4L2HwEventMgr.h>

// MTKCAM
#include <mtkcam/utils/std/Log.h>  // log
#include <property_lib.h>

// MTKCAM/V4L2
#include <mtkcam/v4l2/ipc_hw_event.h>

// STL
#include <mutex>
#include <unordered_map>

#include <cutils/compiler.h>  // for CC_LIKELY, CC_UNLIKELY

using NSCam::NSIoPipe::NSCamIOPipe::IV4L2PipeFactory;

static int g_logLevel = []() {
  return property_get_int32(PROP_V4L2_HWEVENTMGR_LOGLEVEL, 2);
}();

namespace v4l2 {

V4L2HwEventWorker::V4L2HwEventWorker(uint32_t sensorIdx,
                                     EPipeSignal signalToListen,
                                     const char* szCallerName)
    : m_sensorIdx(sensorIdx),
      m_logLevel(g_logLevel),
      m_listenedSignal(signalToListen),
      m_eventName(szCallerName) {
  CAM_LOGD("loglevel %d", m_logLevel);

  // create a V4L2IEventPipe
  IV4L2PipeFactory* p_factory = IV4L2PipeFactory::get();
  if (CC_UNLIKELY(p_factory == nullptr)) {
    CAM_LOGE("create V4L2PipeFactory failed");
    return;
  }

  // create V4L2IEventPipe
  m_pEventPipe = p_factory->getEventPipe(sensorIdx, LOG_TAG);
  if (m_pEventPipe.get() == nullptr) {
    CAM_LOGE("create V4L2IEventPipe failed");
    return;
  } else {
    MBOOL ret = m_pEventPipe->init();
    if (!ret) {
      CAM_LOGE("eventpipe init failed.");
      m_pEventPipe = nullptr;
      return;
    }
    ret = m_pEventPipe->start();
    if (!ret) {
      CAM_LOGE("eventpipe start failed.");
      m_pEventPipe = nullptr;
      return;
    }
  }

  // create IHal3A
  MAKE_Hal3A(
      m_pHal3A, [](IHal3A* p) { p->destroyInstance(LOG_TAG); }, sensorIdx,
      LOG_TAG);
}

V4L2HwEventWorker::~V4L2HwEventWorker() {
  m_pEventPipe = nullptr;
  m_pHal3A = nullptr;  // clear first, avoid unreference of m_eventName
}

int V4L2HwEventWorker::start() {
  m_worker_status = true;
  return V4L2DriverWorker::start();
}

int V4L2HwEventWorker::stop() {
  m_worker_status = false;
  if (m_pEventPipe.get()) {
    m_pEventPipe->stop();
  }
  return V4L2DriverWorker::stop();
}

void V4L2HwEventWorker::signal() {
  if (CC_UNLIKELY(m_pEventPipe.get() == nullptr)) {
    CAM_LOGE("cannot signal hw event since eventpipe is null");
    return;
  }
  m_pEventPipe->signal(m_listenedSignal);
}

void V4L2HwEventWorker::job() {
  // Job is:
  // 1. To wait hw signal.
  // 2. send3ACtrl to IHal3A.
  CAM_LOGD("wait signal(%d) [+]", m_listenedSignal);
  if (CC_UNLIKELY(m_pEventPipe.get() == nullptr)) {
    CAM_LOGE("cannot wait hw event since eventpipe is null");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return;
  }

  int err = m_pEventPipe->wait(m_listenedSignal);
  if (err != 0) {
    if (m_worker_status)
      CAM_LOGD("wait signal(%d) [-] failed with code=%#x", m_listenedSignal,
               err);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return;
  }

  if (!m_worker_status)
    return;

  v4l2::P1Event evt;
  evt.event = static_cast<int32_t>(m_listenedSignal);
  evt.sensorIdx = m_sensorIdx;
  evt.sensorDev = -1;  // unknown
  evt.requestNo = 0;

  if (CC_UNLIKELY(m_pHal3A == nullptr)) {
    return;
  }

  // 2. signal event
  m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_HwSignal, reinterpret_cast<MINTPTR>(&evt),
                       0);
}
};  // namespace v4l2
