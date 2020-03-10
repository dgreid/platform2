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

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "V4L2EventPipe"

#include <inc/V4L2EventPipe.h>
#include <mtkcam/utils/std/Log.h>
#include <cstdint>
#include <memory>
#include <vector>

using NSCam::v4l2::V4L2StreamNode;

namespace NSCam {
namespace NSIoPipe {
namespace NSCamIOPipe {

V4L2EventPipe::V4L2EventPipe(MUINT32 sensor_idx, const char* sz_caller_name)
    : V4L2PipeBase(kPipeHwEvent, sensor_idx, sz_caller_name),
      m_sensorIdx(sensor_idx),
      m_node_name("V4L2EventPipe"),
      m_state(STATE_UNINITED) {
  MY_LOGD("m_sensorIdx %d", m_sensorIdx);
}

V4L2EventPipe::~V4L2EventPipe() {
  MY_LOGD("%s [+]", __FUNCTION__);

  const int currState = m_state.load(std::memory_order_relaxed);

  switch (currState) {
    default:
    case STATE_UNINITED:
      break;

    case STATE_SUBSCRIB:  // stop + uninit
      stop();
      uninit();
      break;

    case STATE_INITED:
      uninit();  // uninit before releasing
      break;
  }

  MY_LOGD("%s [-]", __FUNCTION__);
}

MBOOL V4L2EventPipe::init() {
  MY_LOGD("[%s] +", __FUNCTION__);

  V4L2PipeFactory* pipe_factory =
      dynamic_cast<V4L2PipeFactory*>(IV4L2PipeFactory::get());

  std::lock_guard<std::mutex> lk(m_opLock);

  /* check state first, if it's ok for inited */
  const int currState = m_state.load(std::memory_order_relaxed);
  if (false == isTransitable_locked(currState, ACT_INIT)) {
    MY_LOGD("has been inited, no need to do again");
    return MTRUE;
  }

  /* try to get the exists pipe manager */
  msp_pipev4l2mgr = pipe_factory->getV4L2PipeMgr(m_sensor_idx);
  if (CC_UNLIKELY(!msp_pipev4l2mgr)) {
    MY_LOGE(
        "pipe event mgr doesn't exists. V4L2 event pipe must be "
        "initialized after the related V4L2PipeMgr has been created.");
    return MFALSE;
  }

  /* retrieve p1 subdev */
  m_p1_subdev = msp_pipev4l2mgr->getSubDev();
  if (!m_p1_subdev) {
    MY_LOGE("cannot retrieve subdev");
    return MFALSE;
  }

  /* reset all conditions */
  for (auto& itr : m_eventsCond) {
    std::lock_guard<std::mutex> lk(itr.mx);
    itr.invalidated.store(false, std::memory_order_relaxed);
  }

  /* transit state */
  transitState_locked(currState, ACT_INIT);

  MY_LOGD("%s [-]", __FUNCTION__);
  return MTRUE;
}

int V4L2EventPipe::signal(EPipeSignal eType) {
  return 0;
}

int V4L2EventPipe::wait(EPipeSignal eType,
                        size_t timed_out_ms /* = 0xFFFFFFFF */) {
  MY_LOGD("%s [+]", __FUNCTION__);
  /* check state and transit it */
  std::unique_lock<std::mutex> lkOp(m_opLock);
  std::unique_lock<std::mutex> lkCond(m_eventsCond[eType].mx);
  /* check if the status goest invalidated or not */
  if (false ==
      m_eventsCond[eType].invalidated.load(std::memory_order_relaxed)) {
    lkOp.unlock();

    /* wait event */
    MY_LOGD("wait event (%#x) [+]", eType);
    m_eventsCond[eType].cond.wait(lkCond);
    MY_LOGD("wait event (%#x) [-]", eType);
  } else {
    return -ETIMEDOUT;
  }

  return 0;
}

MBOOL V4L2EventPipe::start() {
  MY_LOGD("+");

  std::lock_guard<std::mutex> lk(m_opLock);
  auto currState = m_state.load(std::memory_order_relaxed);
  /* check state is ok or not */
  if (false == isTransitable_locked(currState, ACT_START)) {
    if (currState ==
        STATE_SUBSCRIB) {  // already running, no need to start again
      return MTRUE;
    }
    MY_LOGI("current is not a valid state(%#x) to start listening events",
            currState);
    return MFALSE;
  }

  /* init poller thread */
  mp_poller = std::make_unique<v4l2::PollerThread>();
  if (!mp_poller) {
    MY_LOGE("fail to create PollerThread");
    return MFALSE;
  }

  std::vector<std::shared_ptr<V4L2Device>> v_device(1);
  v_device[0] = m_p1_subdev;
  status_t status =
      mp_poller->init(v_device, this, POLLPRI | POLLIN | POLLOUT | POLLERR);
  if (status != NO_ERROR) {
    MY_LOGE("poller init failed (ret = %d)", status);
    return MFALSE;
  }

  /* subscribe events */
  if (0 != subscribeEvents_locked()) {
    MY_LOGE("subscribes events failed.");
    return MFALSE;
  }

  /* request poller to poll */
  mp_poller->queueRequest();

  /* transit state */
  transitState_locked(currState, ACT_START);
  return MTRUE;
}

MBOOL V4L2EventPipe::stop() {
  MY_LOGD("+");

  std::lock_guard<std::mutex> lk(m_opLock);
  auto currState = m_state.load(std::memory_order_relaxed);
  /* check state is ok or not */
  if (false == isTransitable_locked(currState, ACT_STOP)) {
    MY_LOGI("current is not a valid state(%#x) to stop listening events",
            currState);
    return MFALSE;
  }

  /* makes as invalidated and notify all condition variables */
  for (auto& itr : m_eventsCond) {
    std::unique_lock<std::mutex> lkCond(itr.mx);
    itr.invalidated.store(true,
                          std::memory_order_relaxed);  // mark as invalidated
    lkCond.unlock();  // unlock first, critical section has been protected by
                      // invalidated flags
    itr.cond.notify_all();
  }

  /* stop poller first (wait until finished) */
  if (mp_poller) {
    mp_poller->flush(true);
    mp_poller = nullptr;
  }

  /* subscribe events */
  if (0 != unsubscribeEvents_locked()) {
    MY_LOGE("subscribes events failed.");
    return MFALSE;
  }

  /* transit state */
  transitState_locked(currState, ACT_STOP);
  return MTRUE;
}

MBOOL V4L2EventPipe::uninit() {
  MY_LOGD("+");

  std::lock_guard<std::mutex> lk(m_opLock);
  auto currState = m_state.load(std::memory_order_relaxed);
  /* check state is ok or not */
  if (false == isTransitable_locked(currState, ACT_UNINIT)) {
    MY_LOGI("current is not a valid state(%#x) to uninit", currState);
    return MFALSE;
  }

  /* clear resource */
  msp_pipev4l2mgr = nullptr;
  m_p1_subdev = nullptr;

  /* transit state */
  transitState_locked(currState, ACT_UNINIT);

  MY_LOGD("-");
  return MTRUE;
}

status_t V4L2EventPipe::notifyPollEvent(PollEventMessage* poll_msg) {
  MY_LOGD("+");
  if ((!poll_msg) || (!poll_msg->data.activeDevices)) {
    return BAD_VALUE;
  }

  /* check poll status */
  if (poll_msg->id == POLL_EVENT_ID_EVENT) {
    if (poll_msg->data.activeDevices->empty()) {
      LOGI("@%s: devices flushed", __FUNCTION__);
      return OK;
    }

    if (poll_msg->data.polledDevices->empty()) {
      LOGW("No devices Polled?");
      return OK;
    }

    if (poll_msg->data.activeDevices->size() !=
        poll_msg->data.polledDevices->size()) {
      LOGW("%zu inactive nodes for request %u, retry poll",
           poll_msg->data.inactiveDevices->size(), poll_msg->data.reqId);

      poll_msg->data.polledDevices->clear();
      *poll_msg->data.polledDevices =
          *poll_msg->data.inactiveDevices;  // retry with inactive devices

      return -EAGAIN;
    }
  } else if (poll_msg->id == POLL_EVENT_ID_TIMEOUT) {
    MY_LOGI("poller timeout[%dms], try again!", poll_msg->data.timeoutMs);
    return -EAGAIN;
  } else if (poll_msg->id == POLL_EVENT_ID_ERROR) {
    MY_LOGE("device poll failed");
    return -EAGAIN; /* try again */
  }

  /* ok, notify self it's ready to invoke dequeEvents_locked */
  uint32_t dequed_event_type = 0;
  int err = dequeEvents_locked(&dequed_event_type);
  if (err != 0) {
    MY_LOGE("dequeEvents_locked returns error code=%#x", err);
  } else {
    if (dequed_event_type == V4L2_EVENT_FRAME_SYNC) {
      m_eventsCond[EPipeSignal_VSYNC].cond.notify_all();
      m_eventsCond[EPipeSignal_SOF].cond.notify_all();
    } else {
      MY_LOGW("dequed event(%#x) but not handled yet", dequed_event_type);
    }
  }

  do {
    std::lock_guard<std::mutex> lk(m_opLock);
    auto currState = m_state.load(std::memory_order_relaxed);
    if (currState == STATE_SUBSCRIB) {
      /* request poller to poll */
      mp_poller->queueRequest();
    }
  } while (0);

  MY_LOGD("-");

  return OK;
}

int V4L2EventPipe::subscribeEvents_locked() {
  MY_LOGD("%s [+]", __FUNCTION__);

  /* get V4L2VideoNode */
  auto status = m_p1_subdev->SubscribeEvent(V4L2_EVENT_FRAME_SYNC);

  if (CC_UNLIKELY(status != 0)) {
    MY_LOGE("subscribe event failed, error code=%#x", status);
    return status;
  }

  return 0;
}

int V4L2EventPipe::unsubscribeEvents_locked() {
  MY_LOGD("%s [+]", __FUNCTION__);

  auto status = m_p1_subdev->UnsubscribeEvent(V4L2_EVENT_FRAME_SYNC);

  if (CC_UNLIKELY(status != 0)) {
    MY_LOGE("unsubscribe event failed, error code=%#x", status);
    return status;
  }

  return 0;
}

status_t V4L2EventPipe::dequeEvents_locked(uint32_t* r_dequed_event_type) {
  MY_LOGD("%s [+]", __FUNCTION__);

  struct v4l2_event event;
  auto status = m_p1_subdev->DequeueEvent(&event);
  if (CC_UNLIKELY(status != NO_ERROR)) {
    MY_LOGE("dequeue event got error code =%#x", status);
    return status;
  }

  *r_dequed_event_type = event.type;

  switch (event.type) {
    case V4L2_EVENT_FRAME_SYNC:
      MY_LOGD("V4L2_EVENT_FRAME_SYNC (seq=%u)",
              event.u.frame_sync.frame_sequence);
      break;

    default:
      MY_LOGE("illegal event type: %d", event.type);
      return -EINVAL;
  }

  return 0;
}

}  // namespace NSCamIOPipe
}  // namespace NSIoPipe
}  // namespace NSCam
