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

#define LOG_TAG "V4L2PipeBase"

#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "CamCapability/ICam_capability.h"
#include "inc/V4L2EventPipe.h"
#include "inc/V4L2NormalPipe.h"
#include "inc/V4L2PipeBase.h"
#include "inc/V4L2PipeMgr.h"
#include "inc/V4L2StatisticPipe.h"
#include "inc/V4L2TuningPipe.h"
#include "mtkcam/utils/std/Log.h"
#include <mtkcam/def/common.h>

using NSCam::v4l2::SyncReqMgr;
using NSCam::v4l2::V4L2StreamNode;

#define IQSenSum (2)
#define IQSenCombi (4)

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "V4L2PipeBase"

/******************************************************************************
 *  Module Version
 ******************************************************************************/
/**
 * The supported module API version.
 */
#define MY_MODULE_API_VERSION MTKCAM_MAKE_API_VERSION(1, 0)

/**
 * The supported sub-module API versions in ascending order.
 * Assume: all cameras support the same sub-module api versions.
 */
#define MY_SUB_MODULE_API_VERSION \
  { MTKCAM_MAKE_API_VERSION(1, 0), }
static const std::vector<MUINT32> gSubModuleapi_version = []() {
  std::vector<MUINT32> v(MY_SUB_MODULE_API_VERSION);
  std::sort(v.begin(), v.end());
  return v;
}();

namespace NSCam {
namespace NSIoPipe {
namespace NSCamIOPipe {

MBOOL V4L2PipeBase::disableLink(int port_index) {
  int ret = MTRUE;

  const auto search = msp_pipev4l2mgr->m_port_mapper.find(port_index);

  if (search != std::end(msp_pipev4l2mgr->m_port_mapper)) {
    std::vector<std::shared_ptr<V4L2StreamNode>>::const_iterator it_node =
        std::find_if(mv_active_node.begin(), mv_active_node.end(),
                     [search](const std::shared_ptr<V4L2StreamNode>& _node) {
                       return strstr(_node->getName(), search->second);
                     });

    if (it_node == mv_active_node.end()) {
      MY_LOGW("port index %d is not in active device, don't need to disable",
              port_index);
      ret = MFALSE;
    } else {
      mv_active_node.erase(it_node);
      auto status = msp_pipev4l2mgr->disableLink(DYNAMIC_LINK_BYVIDEONAME,
                                                 search->second);
      if (status != 0) {
        MY_LOGE("disable link of port index %d failed", port_index);
        ret = MFALSE;
      }
    }
  } else {
    MY_LOGE("cannot disable link of port index %d, since no device name found",
            port_index);
    ret = MFALSE;
  }

  return ret;
}

MBOOL V4L2PipeBase::passBufInfo(int port_index, BufInfo* buf_info) {
  switch (port_index) {
    case NSImageio::NSIspio::EPortIndex_IMGO:
    case NSImageio::NSIspio::EPortIndex_RRZO:
      buf_info->mMetaData.mCrop_s = MRect(
          MPoint(0, 0),
          MSize(m_sensor_config_params.crop.w, m_sensor_config_params.crop.h));
      buf_info->mMetaData.mCrop_d =
          MRect(MPoint(0, 0), buf_info->FrameBased.mDstSize);
      buf_info->mMetaData.mDstSize = buf_info->FrameBased.mDstSize;
      break;
    case NSImageio::NSIspio::EPortIndex_EISO:
    case NSImageio::NSIspio::EPortIndex_LCSO:
      buf_info->mMetaData.mCrop_s = buf_info->mMetaData.mCrop_d = MRect();
      buf_info->mMetaData.mDstSize =
          MSize(buf_info->mBuffer->getBufSizeInBytes(0), 1);
      break;
    default:
      buf_info->mMetaData.mCrop_s = buf_info->mMetaData.mCrop_d = MRect();
      buf_info->mMetaData.mDstSize = MSize();
      break;
  }

  return MTRUE;
}

std::shared_ptr<V4L2PipeMgr> V4L2PipeFactory::getV4L2PipeMgr(MUINT32 sensor_idx,
                                                             PipeTag pipe_tag) {
  MY_LOGD("+");

  if (sensor_idx >= kPipe_Sensor_RSVD) {
    MY_LOGE("InvalidSensorIdx = %d", sensor_idx);
    return nullptr;
  }
  std::lock_guard<std::mutex> lock(m_pipemgr_lock);
  std::shared_ptr<V4L2PipeMgr> sp_pipemgr = mwp_v4l2pipemgr[sensor_idx].lock();

  if ((!sp_pipemgr) &&
      (pipe_tag != kPipeTag_Unknown)) {  // create V4L2PipeMgr and build links
    int ret;
    sp_pipemgr = std::make_shared<V4L2PipeMgr>(sensor_idx);
    mwp_v4l2pipemgr[sensor_idx] = sp_pipemgr;
    MY_LOGI("Create V4L2PipeMgr[#%d](0x%p)", sensor_idx, sp_pipemgr.get());
    ret = sp_pipemgr->buildV4l2Links(pipe_tag);
    if (ret) {
      MY_LOGI("Create V4L2 links success with pipe tag: %d", pipe_tag);
    } else {
      MY_LOGE("Create V4L2 links fail");
      sp_pipemgr.reset();
      return nullptr;
    }
  } else if (pipe_tag == kPipeTag_Unknown) {  // for V4L2EventPipe
    if (!sp_pipemgr) {
      MY_LOGE("V4L2PipeMgr has not been ceated yet");
      return nullptr;
    }
  } else {  // get V4L2PipeMgr
    /* need to check if the pipe tag is okay or not */
    if (CC_UNLIKELY(pipe_tag != sp_pipemgr->getPipeTag())) {
      MY_LOGE(
          "V4L2PipeMgr has been created already, cannot create with "
          "another pipe_tag, current=%#x, target=%#x",
          sp_pipemgr->getPipeTag(), pipe_tag);
      return nullptr;
    }
  }

  MY_LOGD("- V4L2PipeMgr[%d](0x%p)", sensor_idx, sp_pipemgr.get());
  return sp_pipemgr;
}

MBOOL V4L2PipeBase::init(PipeTag pipe_tag) {
  PIPE_BASE_LOGD("+");

  mp_pipe_factory = dynamic_cast<V4L2PipeFactory*>(IV4L2PipeFactory::get());
  if (!mp_pipe_factory) {
    PIPE_BASE_LOGE("create pipe factory fail");
    return MFALSE;
  }

  msp_pipev4l2mgr = mp_pipe_factory->getV4L2PipeMgr(m_sensor_idx, pipe_tag);

  if (!msp_pipev4l2mgr) {
    PIPE_BASE_LOGE("create pipe v4l2 mgr fail with pipe tag: %d", pipe_tag);
    return MFALSE;
  }

  MBOOL ret = msp_pipev4l2mgr->queryV4l2StreamNode(&mv_active_node);
  if (!ret) {
    PIPE_BASE_LOGE("query stream node fail with pipe tag: %d", pipe_tag);
    return MFALSE;
  }

  mp_poller = std::make_unique<v4l2::PollerThread>();
  if (!mp_poller) {
    PIPE_BASE_LOGE("fail to create PollerThread");
    return MFALSE;
  }

  PIPE_BASE_LOGD("-");
  return MTRUE;
}

MBOOL V4L2PipeBase::uninit() {
  PIPE_BASE_LOGD("+");

  PIPE_BASE_LOGD("flush poller +");
  if (mp_poller) {
    mp_poller->flush(true);
    mp_poller = nullptr;
  }
  PIPE_BASE_LOGD("flush poller -");

  msp_pipev4l2mgr = nullptr;

  PIPE_BASE_LOGD("-");

  return MTRUE;
}

MBOOL V4L2PipeBase::enque(const QBufInfo& r_qbuf) {
  /* In order to speed up enque time, we don't use operation lock here and
   * create dedicated enque lock. */
  std::lock_guard<std::mutex> lock(m_op_enq_lock);

  std::unordered_map<int, BurstFrameQ> map_burst_frame;
  PIPE_BASE_LOGD("+");
  int reqId;

  const int cur_state = m_fsm_state.load(std::memory_order_relaxed);
  if (!checkFsm(cur_state, kOp_Enque)) {
    PIPE_BASE_LOGI("wrong state to Enque, current state is %d, not Streaming",
                   cur_state);
    return MFALSE;
  }

  if (r_qbuf.mvOut.empty()) {
    PIPE_BASE_LOGE("enque buffer can not be empty");
    return MFALSE;
  }

  for (auto buf : r_qbuf.mvOut) {
    std::shared_ptr<NSCam::v4l2::V4L2StreamNode> sp_node;
    int _port_index = buf.mPortID.index;
    auto it_map_node = m_map_node.find(_port_index);
    if (it_map_node == m_map_node.end()) {
      PIPE_BASE_LOGE("this dma port: %d did not config yet", _port_index);
      return MFALSE;
    }
    sp_node = it_map_node->second;

    NSCam::v4l2::BufInfo v4l2_bufinfo;
    PIPE_BASE_LOGD("valid port index %d, device %s", _port_index,
                   sp_node->getName());
    v4l2_bufinfo.mPortID = buf.mPortID;
    v4l2_bufinfo.mBuffer = buf.mBuffer;
    v4l2_bufinfo.magic_num = buf.FrameBased.mMagicNum_tuning;

    /* */
    passBufInfo(_port_index, &buf);

    /* check if enable RequestAPI, if yes, acquire it and append */
    std::function<int()> lazy_notify_enqueued;
    do {
      SyncReqMgr* p_reqMgr = msp_pipev4l2mgr->getSyncRegMgr();
      /* no SyncRequestManager, cannot enable */
      if (CC_UNLIKELY(p_reqMgr == nullptr)) {
        break;
      }

      const SyncReqMgr::SYNC_ID sync_id =
          SyncReqMgr::getSyncIdByNodeId(sp_node->getId());

      /* check if the given node has been enabled as RequestAPI */
      if (p_reqMgr->isEnableRequestAPI(sync_id) == false) {
        break;
      }

      /* look up the RequestAPI fd */
      int request_api_fd =
          p_reqMgr->acquireRequestAPI(sync_id, v4l2_bufinfo.magic_num);
      if (CC_UNLIKELY(request_api_fd <= 0)) {
        PIPE_BASE_LOGE(
            "RequestAPI enabled, but acquire failed, "
            "disable it (caller=%#x, magicnum=%d)",
            sync_id, v4l2_bufinfo.magic_num);
        break;
      }

      v4l2_bufinfo.mRequestFD = request_api_fd;

      /* attach lazy notify enqued job */
      lazy_notify_enqueued = [p_reqMgr, request_api_fd, sync_id]() -> int {
        return p_reqMgr->notifyEnqueuedByRequestAPI(sync_id, request_api_fd);
      };
    } while (0);

    if ((!sp_node) || (sp_node->enque(v4l2_bufinfo, true) != NO_ERROR)) {
      PIPE_BASE_LOGE("enque failed");
      return MFALSE;
    }

    if (lazy_notify_enqueued && lazy_notify_enqueued()) {
      PIPE_BASE_LOGE("notifyEnqueuedByRequestAPI fail");
      return MFALSE;
    }

    /* do buffer classfication, extract buffers of same dma port to a
     * BurstFrameQ */
    auto it_burst_frame = map_burst_frame.find(buf.mPortID.index);
    if (it_burst_frame == map_burst_frame.end()) {
      map_burst_frame.emplace(_port_index, buf);
      PIPE_BASE_LOGD("create %d BurstFrameQ", buf.mPortID.index);
    } else {
      it_burst_frame->second.mv_buf.push_back(buf);
    }
    PIPE_BASE_LOGD(
        "insert buf to (port_index/magic/IImageBuffer):(%d/%d/%p) BurstFrameQ",
        buf.mPortID.index, buf.FrameBased.mMagicNum_tuning,
        static_cast<void*>(buf.mBuffer));
    reqId = buf.FrameBased.mMagicNum_tuning;
  }

  /* insert enque request to enque container */
  {
    std::lock_guard<std::mutex> lock(m_enq_ctnr_lock);
    for (const auto& burst_frame : map_burst_frame) {
      auto it_enq_ctnr = m_map_enq_ctnr.find(burst_frame.first);
      if (it_enq_ctnr == m_map_enq_ctnr.end()) {
        PIPE_BASE_LOGE("memory leak at enque ctnr with port_index: %d",
                       burst_frame.first);
        return MFALSE;
      } else {
        it_enq_ctnr->second.push_back(burst_frame.second);
      }
    }
  }

  status_t status = OK;
  /* call poller request */
  if (cur_state == kState_Streaming) {
    status = mp_poller->queueRequest(reqId);
    if (status != NO_ERROR) {
      return MFALSE;
    }
  } else {
    /*
     * due to queueRequest will trigger poller thread to poll event,
     * however, call poll before streaming is forbidden. Instead of
     * queueRequest, we store the request count and pop them out at
     * start operation.
     */
    mv_enq_req.emplace_back(reqId);
  }

  PIPE_BASE_LOGD("-");
  return MTRUE;
}

MBOOL V4L2PipeBase::deque(const QPortID& q_qport,
                          QBufInfo* p_qbuf,
                          MUINT32 u4TimeoutMs) {
  std::unique_lock<std::mutex> lock_op(m_op_lock);
  PIPE_BASE_LOGD("+");

  int cur_state = m_fsm_state.load(std::memory_order_relaxed);
  if (!checkFsm(cur_state, kOp_Deque)) {
    PIPE_BASE_LOGI("wrong state to Deque, current state is %d not Streaming",
                   cur_state);
    return MFALSE;
  }

  if (CC_UNLIKELY((!p_qbuf) || (!p_qbuf->mvOut.empty()))) {
    PIPE_BASE_LOGE("deque buffer is null or not empty");
    return MFALSE;
  }

  bool ret = m_dequeue_cv.wait_for(
      lock_op, std::chrono::milliseconds(u4TimeoutMs), [q_qport, this]() {
        const int cur_state = m_fsm_state.load(std::memory_order_relaxed);
        if (CC_UNLIKELY(!checkFsm(cur_state, kOp_Deque))) {
          return true;
        }
        std::lock_guard<std::mutex> lock_deq(m_deq_ctnr_lock);
        for (const auto& port : q_qport.mvPortId) {
          auto it = m_map_deq_ctnr.find(port.index);
          if (CC_UNLIKELY((it == m_map_deq_ctnr.end())))
            return false;
          else if (it->second.empty())
            return false;
        }
        return true;
      });

  cur_state = m_fsm_state.load(std::memory_order_relaxed);
  if (CC_UNLIKELY(!checkFsm(cur_state, kOp_Deque))) {
    PIPE_BASE_LOGI("current state is Uninit, deque operation will be flushed");
    return MFALSE;
  }

  if (!ret) {
    for (const auto& port_id : q_qport.mvPortId)
      PIPE_BASE_LOGW("port=%d, deque frame time out[%dms]", port_id.index,
                     u4TimeoutMs);
    return MFALSE;
  }

  std::lock_guard<std::mutex> lock_deq(m_deq_ctnr_lock);

  for (const auto& port_id : q_qport.mvPortId) {
    auto it = m_map_deq_ctnr.find(port_id.index);
    if (it == m_map_deq_ctnr.end()) {
      PIPE_BASE_LOGE("not exist, wrong dma port index: %d", port_id.index);
      return MFALSE;
    }

    if (it->second.empty()) {
      PIPE_BASE_LOGE("deque container is empty at port index: %d",
                     port_id.index);
      return MFALSE;
    }

    p_qbuf->mvOut.insert(p_qbuf->mvOut.end(), it->second.front().mv_buf.begin(),
                         it->second.front().mv_buf.end());
    PIPE_BASE_LOGD(
        "deque buffer (magic,hal,IImageBuffer):(%d, %d, %p) at port index: %d",
        it->second.front().mv_buf.front().FrameBased.mMagicNum_tuning,
        it->second.front().mv_buf.front().mMetaData.mMagicNum_hal,
        static_cast<void*>(it->second.front().mv_buf.front().mBuffer),
        port_id.index);
    it->second.pop_front();
  }

  PIPE_BASE_LOGD("-");

  return MTRUE;
}

MBOOL V4L2PipeBase::configPipe(
    QInitParam const& init_param,
    std::map<int, std::vector<std::shared_ptr<IImageBuffer>>>* map_vbuffers) {
  PIPE_BASE_LOGD("+");

  for (const auto port_info : init_param.mPortInfo) {
    int port_index = port_info.mPortID.index;
    std::shared_ptr<NSCam::v4l2::V4L2StreamNode> sp_node;
    auto idx = m_map_node.find(port_index);
    if (idx != m_map_node.end()) {
      sp_node = idx->second;
      PIPE_BASE_LOGE(
          "error due to config twice, already exist in map id %d, node %p name "
          "%s",
          port_index, sp_node.get(), sp_node->getName());
      return MFALSE;
    }

    auto search = msp_pipev4l2mgr->m_port_mapper.find(port_index);
    if (search == msp_pipev4l2mgr->m_port_mapper.end()) {
      PIPE_BASE_LOGE("search failed, port index is not supported: %d @%d",
                     port_index, __LINE__);
      return MFALSE;
    }
    PIPE_BASE_LOGD("find in Port_Mapper (index, name):(%d, %s)", search->first,
                   search->second);

    std::vector<std::shared_ptr<V4L2StreamNode>>::const_iterator it_node =
        std::find_if(mv_active_node.begin(), mv_active_node.end(),
                     [search](const std::shared_ptr<V4L2StreamNode>& _node) {
                       return strstr(_node->getName(), search->second);
                     });
    if (it_node == mv_active_node.end()) {
      PIPE_BASE_LOGE("search v4l2 stream node '%s' fail", search->second);
      return MFALSE;
    }

    m_map_node.emplace(search->first, *it_node);
    sp_node = *it_node;
    mv_active_node.erase(it_node);

    status_t status = OK;
    int buf_boundary_in_bytes[3] = {0};
    IImageBufferAllocator::ImgParam img_param(
        port_info.mFmt, port_info.mDstSize, port_info.mStride,
        buf_boundary_in_bytes, 1, init_param.mSensorFormatOrder);

    status = sp_node->setBufPoolSize(port_info.mBufPoolSize);
    if (status != NO_ERROR) {
      PIPE_BASE_LOGE("Fail to setBufPoolSize");
      return MFALSE;
    }

    if (map_vbuffers) {
      auto it = map_vbuffers->find(port_index);
      if (it == map_vbuffers->end()) {
        PIPE_BASE_LOGE(
            "there is a mismatch between dma port and given query buffer");
        return MFALSE;
      }
      status = sp_node->setFormatAnGetdBuffers(&img_param, &(it->second));
    } else {
      status = sp_node->setBufFormat(&img_param);
    }

    if (status != NO_ERROR) {
      PIPE_BASE_LOGE("Fail to setFormat");
      return MFALSE;
    }

    m_map_enq_ctnr.emplace(port_index, std::deque<BurstFrameQ>{});
    m_map_deq_ctnr.emplace(port_index, std::deque<BurstFrameQ>{});
  }

  std::vector<std::shared_ptr<V4L2Device>> v_device;
  for (const auto& it : m_map_node)
    v_device.push_back(it.second->getVideoNode());

  status_t status =
      mp_poller->init(v_device, this, POLLPRI | POLLIN | POLLOUT | POLLERR);
  if (status != NO_ERROR) {
    CAM_LOGE("poller init failed (ret = %d)", status);
    return MFALSE;
  }

  PIPE_BASE_LOGD("-");
  return MTRUE;
}

MBOOL V4L2PipeBase::sendCommand(int cmd,
                                MINTPTR arg1,
                                MINTPTR arg2,
                                MINTPTR arg3) {
  PIPE_BASE_LOGE("should be overitten by derived class");
  return MFALSE;
}

MBOOL V4L2PipeBase::start() {
  std::unique_lock<std::mutex> lk_op_enque(m_op_enq_lock, std::defer_lock);
  std::unique_lock<std::mutex> lk_op(m_op_lock, std::defer_lock);
  std::lock(lk_op, lk_op_enque);
  PIPE_BASE_LOGD("+");
  status_t status = OK;

  const int cur_state = m_fsm_state.load(std::memory_order_relaxed);
  if (!checkFsm(cur_state, kOp_Start)) {
    PIPE_BASE_LOGI("wrong state to start, current state is %d not standby",
                   cur_state);
    return MFALSE;
  }

  for (const auto& node : m_map_node) {
    status = node.second->start();
    if (status != NO_ERROR) {
      PIPE_BASE_LOGE("Fail to start streaming");
      return MFALSE;
    }
  }

  if (mv_enq_req.empty()) {
    PIPE_BASE_LOGW("no buf enqued before call start");
  } else {
    for (auto req_id : mv_enq_req)
      status = mp_poller->queueRequest(req_id);
  }

  updateFsm(cur_state, kOp_Start);
  PIPE_BASE_LOGD("-");
  return MTRUE;
}

MBOOL V4L2PipeBase::stop() {
  std::unique_lock<std::mutex> lk_op(m_op_lock, std::defer_lock);
  std::unique_lock<std::mutex> lk_op_enq(m_op_enq_lock, std::defer_lock);
  std::lock(lk_op, lk_op_enq);

  PIPE_BASE_LOGD("+");
  status_t status = OK;

  const int cur_state = m_fsm_state.load(std::memory_order_relaxed);
  if (!checkFsm(cur_state, kOp_Stop)) {
    PIPE_BASE_LOGI("wrong state to stop, current state is %d not streaming",
                   cur_state);
    return MFALSE;
  }

  /* flush deq thread before stop hw to avoid illegal poll event */
  if (mp_poller) {
    mp_poller->flush(true);
    mp_poller = nullptr;
  }

  for (const auto& node : m_map_node) {
    status = node.second->stop();
    if (status != NO_ERROR) {
      PIPE_BASE_LOGE("Fail to stop streaming");
      return MFALSE;
    }
  }

  updateFsm(cur_state, kOp_Stop);
  lk_op.unlock();
  lk_op_enq.unlock();

  m_dequeue_cv.notify_all();
  PIPE_BASE_LOGD("-");
  return MTRUE;
}

status_t V4L2PipeBase::notifyPollEvent(PollEventMessage* poll_msg) {
  if ((!poll_msg) || (!poll_msg->data.activeDevices)) {
    return BAD_VALUE;
  }

  if (poll_msg->id == POLL_EVENT_ID_EVENT) {
    if (poll_msg->data.activeDevices->empty()) {
      PIPE_BASE_LOGD("@%s: devices flushed", __FUNCTION__);
      return OK;
    }

    if (poll_msg->data.polledDevices->empty()) {
      PIPE_BASE_LOGW("No devices Polled?");
      return OK;
    }

    if (poll_msg->data.activeDevices->size() !=
        poll_msg->data.polledDevices->size()) {
      PIPE_BASE_LOGI("%zu inactive nodes for request %u, retry poll",
                     poll_msg->data.inactiveDevices->size(),
                     poll_msg->data.reqId);

      poll_msg->data.polledDevices->clear();
      *poll_msg->data.polledDevices =
          *poll_msg->data.inactiveDevices;  // retry with inactive devices

      return -EAGAIN;
    }
  } else if (poll_msg->id == POLL_EVENT_ID_TIMEOUT) {
    for (auto& it_node : m_map_node)
      PIPE_BASE_LOGI("port=%d,magic_num=%d,poller timeout[%dms],try again!",
                     it_node.first, poll_msg->data.reqId,
                     poll_msg->data.timeoutMs);
    return -EAGAIN;
  } else if (poll_msg->id ==
             POLL_EVENT_ID_ERROR) {  // FIX ME, stt pipe print too many this non
                                     // sense error log before HW start
    // PIPE_BASE_LOGE("device poll failed");
    return -EAGAIN;
  }

  std::unique_lock<std::mutex> lock_enq(m_enq_ctnr_lock, std::defer_lock);
  std::unique_lock<std::mutex> lock_deq(m_deq_ctnr_lock, std::defer_lock);
  std::lock(lock_enq, lock_deq);

  /* call deque for each video device */
  for (auto& it_node : m_map_node) {
    NSCam::v4l2::BufInfo buf;
    buf.mPortID.index = it_node.first;
    if (it_node.second->deque(&buf) != NO_ERROR) {
      PIPE_BASE_LOGE("deque failed");
      return -EINVAL;
    }

    /* dequeued, check RequestAPI, if it's not 0, mark this RequestAPI fd as
     * done */
    do {
      /* no Request API enabled */
      if (buf.mRequestFD <= 0) {
        buf.magic_num = buf.mSequenceNum;
        break;
      }
      SyncReqMgr* p_reqMgr = msp_pipev4l2mgr->getSyncRegMgr();
      if (CC_UNLIKELY(p_reqMgr == nullptr)) {
        PIPE_BASE_LOGE(
            "Cannot validate a magic number, RequestAPI has been "
            "enabled, but SyncReqMgr doesn't exist");
        break;
      }

      /* validate magic number and mark done */
      buf.magic_num = p_reqMgr->validateMagicNum(
          SyncReqMgr::getSyncIdByNodeId(it_node.second->getId()),
          buf.mRequestFD);
      if (CC_UNLIKELY(buf.magic_num == 0)) {
        PIPE_BASE_LOGE("validated a magic number from RequestAPI(%#x) failed",
                       buf.mRequestFD);
        break;
      }
    } while (0);

    const auto& it_enq_ctnr = m_map_enq_ctnr.find(it_node.first);

    std::deque<BurstFrameQ>::iterator it_burst_frame =
        it_enq_ctnr->second.begin();
    for (; it_burst_frame != it_enq_ctnr->second.end(); it_burst_frame++) {
      std::vector<BufInfo>::const_iterator it_buf = std::find_if(
          it_burst_frame->mv_buf.begin(), it_burst_frame->mv_buf.end(),
          [buf](const BufInfo& _buf) { return buf.mBuffer == _buf.mBuffer; });
      if (it_buf != it_burst_frame->mv_buf.end()) {
        PIPE_BASE_LOGD("search buffer ok");
        break;
      }
    }

    if (it_burst_frame == it_enq_ctnr->second.end()) {
      PIPE_BASE_LOGE(
          "fail at mapping v4l2 deuqe buffer to enque container, this is a "
          "orphan buffer");
      continue;
    }

    it_burst_frame->mv_buf.front().FrameBased.mMagicNum_tuning = buf.magic_num;
    it_burst_frame->mv_buf.front().mMetaData.mMagicNum_hal = buf.magic_num;
    it_burst_frame->mv_buf.front().mMetaData.mTimeStamp_B = buf.timestamp;
    it_burst_frame->mv_buf.front().mMetaData.mTimeStamp = buf.timestamp;
    it_burst_frame->mv_buf.front().mSize = buf.mSize;

    PIPE_BASE_LOGD(
        "move buffer (magic, IImageBuffer, width, height, size):(%d, %p, %d, "
        "%d, %d) "
        "from enque ctnr to deque ctnr at port index: %d",
        it_burst_frame->mv_buf.front().FrameBased.mMagicNum_tuning,
        static_cast<void*>(it_burst_frame->mv_buf.front().mBuffer),
        it_burst_frame->mv_buf.front().mMetaData.mDstSize.w,
        it_burst_frame->mv_buf.front().mMetaData.mDstSize.h,
        it_burst_frame->mv_buf.front().mSize, it_node.first);

    PIPE_BASE_LOGD("crop_s (width/height):(%d/%d)",
                   it_burst_frame->mv_buf.front().mMetaData.mCrop_s.s.w,
                   it_burst_frame->mv_buf.front().mMetaData.mCrop_s.s.h);

    /* update magic number by V4L2Buffer's sequence number */
    PIPE_BASE_LOGD(
        "update magic number(curr=%d) by driver says(target=%d), timestamp "
        "%lld",
        it_burst_frame->mv_buf.front().FrameBased.mMagicNum_tuning,
        buf.magic_num, buf.timestamp);

    const auto& it_deq_ctnr = m_map_deq_ctnr.find(it_node.first);
    it_deq_ctnr->second.emplace_back(std::move(*it_burst_frame));
    it_enq_ctnr->second.erase(it_burst_frame);
  }

  lock_enq.unlock();
  lock_deq.unlock();

  m_dequeue_cv.notify_all();

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
V4L2PipeFactory* getV4L2PipeFactory() {
  static auto pipe_factory = new V4L2PipeFactory();

  return pipe_factory;
}

/******************************************************************************
 *
 ******************************************************************************/
VISIBILITY_PUBLIC extern "C" mtkcam_module*
get_mtkcam_module_iopipe_CamIO_NormalPipe() {
  return static_cast<mtkcam_module*>(getV4L2PipeFactory());
}

/******************************************************************************
 *
 ******************************************************************************/
V4L2PipeFactory::V4L2PipeFactory() : m_pipe_meminfo{}, m_plat_sensor_info{} {
  //// + mtkcam_module initialization
  get_module_api_version = []() -> uint32_t { return MY_MODULE_API_VERSION; };

  get_module_extension = []() -> void* { return getV4L2PipeFactory(); };

  get_sub_module_api_version = [](uint32_t const** versions,
                                  size_t* count) -> int {
    if (!versions || !count) {
      MY_LOGE("invalid arguments - versions:%p count:%p", versions, count);
      return -EINVAL;
    }

    // Assume: all cameras support the same sub-module api versions.
    *versions = gSubModuleapi_version.data();
    *count = gSubModuleapi_version.size();
    return 0;
  };
  //// - mtkcam_module initialization
}

std::shared_ptr<NSCam::NSIoPipe::NSCamIOPipe::V4L2IIOPipe>
V4L2PipeFactory::getSubModule(IspPipeType pipe_type,
                              MUINT32 sensor_index,
                              char const* sz_caller_name,
                              MUINT32 api_version) {
  if (sensor_index >= kPipe_Sensor_RSVD) {
    MY_LOGE("InvalidSensorIdx = %d", sensor_index);
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(m_pipe_module_lock);
  std::shared_ptr<V4L2IIOPipe> sp_iopipe;
  switch (pipe_type) {
    case kPipeNormal:
      if (!std::binary_search(gSubModuleapi_version.begin(),
                              gSubModuleapi_version.end(), api_version)) {
        MY_LOGE("[%s:%d] Unsupported sub-module api version:%#x",
                sz_caller_name, sensor_index, api_version);
        return nullptr;
      }
      if (MTKCAM_GET_MAJOR_API_VERSION(api_version) != 1) {
        MY_LOGE("[%s:%d] Not implement for sub-module api version:%#x",
                sz_caller_name, sensor_index, api_version);
        return nullptr;
      }

      /* create or get normalpipe instance */
      sp_iopipe = mwp_normalpipe[sensor_index].lock();
      if (!sp_iopipe) {
        /* create normalpipe instance */
        sp_iopipe = std::make_shared<V4L2NormalPipe>(pipe_type, sensor_index,
                                                     "V4L2NormalPipe");
        m_pipe_meminfo.npipe_alloc_mem_sum += sizeof(V4L2NormalPipe);
        mwp_normalpipe[sensor_index] = sp_iopipe;
        MY_LOGI("create V4L2NormalPipe instance=%p, user: %s, sensor index: %d",
                sp_iopipe.get(), sz_caller_name, sensor_index);

        /* query sensor stt info while normalpipe was created */
        IHalSensorList* p_sensor_list = IHalSensorList::getInstance();
        int sensor_cnt = p_sensor_list->queryNumberOfSensors();
        if ((sensor_cnt > IOPIPE_MAX_SENSOR_CNT) || (sensor_cnt == 0)) {
          MY_LOGE("Not support %d sensors", sensor_cnt);
          return nullptr;
        }
        m_plat_sensor_info.existed_sensor_cnt = sensor_cnt;
        for (int i = 0; i < sensor_cnt; i++) {
          m_plat_sensor_info.sensor_info[i].idx = i;
          m_plat_sensor_info.sensor_info[i].typeformw =
              p_sensor_list->queryType(i);
          m_plat_sensor_info.sensor_info[i].dev_id =
              p_sensor_list->querySensorDevIdx(i);
          p_sensor_list->querySensorStaticInfo(
              m_plat_sensor_info.sensor_info[i].dev_id,
              &m_plat_sensor_info.sensor_info[i].stt_info);
          MY_LOGI("N:%d,SensorName=%s,Type=%d,DevId=%d", i,
                  p_sensor_list->queryDriverName(i),
                  m_plat_sensor_info.sensor_info[i].typeformw,
                  m_plat_sensor_info.sensor_info[i].dev_id);
        }
      }
      break;

    case kPipeStt:
      /* create or get statisticpip instance */
      sp_iopipe = mwp_sttpipe[sensor_index].lock();
      if (!sp_iopipe) {
        /* create statisticspipe instance */
        sp_iopipe = std::make_shared<V4L2StatisticPipe>(pipe_type, sensor_index,
                                                        "V4L2StatisticPipe");
        m_pipe_meminfo.npipe_alloc_mem_sum += sizeof(V4L2StatisticPipe);
        mwp_sttpipe[sensor_index] = sp_iopipe;
        MY_LOGI(
            "create V4L2StatisticPipe instance=%p, user: %s, sensor index: %d",
            sp_iopipe.get(), sz_caller_name, sensor_index);
      }
      break;

    case kPipeStt2:
      sp_iopipe = mwp_sttpipe2[sensor_index].lock();
      if (!sp_iopipe) {
        sp_iopipe = std::make_shared<V4L2StatisticPipe>(pipe_type, sensor_index,
                                                        "V4L2StatisticPipe2");
        m_pipe_meminfo.npipe_alloc_mem_sum += sizeof(V4L2StatisticPipe);
        mwp_sttpipe2[sensor_index] = sp_iopipe;
        MY_LOGI(
            "create V4L2StatisticPipe(meta2) instance=%p, user: %s, sensor "
            "index: %d",
            sp_iopipe.get(), sz_caller_name, sensor_index);
      }
      break;

    case kPipeTuning:
      /* create or get tuningpipe instance */
      sp_iopipe = mwp_tuningpipe[sensor_index].lock();
      if (!sp_iopipe) {
        sp_iopipe = std::make_shared<V4L2TuningPipe>(pipe_type, sensor_index,
                                                     "V4L2TuningPipe");
        m_pipe_meminfo.npipe_alloc_mem_sum += sizeof(V4L2TuningPipe);
        mwp_tuningpipe[sensor_index] = sp_iopipe;
        MY_LOGI("create V4L2TuningPipe instance=%p, user: %s, sensor index: %d",
                sp_iopipe.get(), sz_caller_name, sensor_index);
      }
      break;

    default:
      MY_LOGE("[%s:%d] Not supported pipe type:%#x", sz_caller_name,
              sensor_index, pipe_type);
      return nullptr;
  }
  V4L2PipeBase* v4l2_pipe_base =
      reinterpret_cast<V4L2PipeBase*>(sp_iopipe.get());

  MY_LOGI("pipe_type: %d, user: %s, sensor index: %d",
          v4l2_pipe_base->m_pipe_type, sz_caller_name, sensor_index);

  return sp_iopipe;
}

std::shared_ptr<NSCam::NSIoPipe::NSCamIOPipe::V4L2IEventPipe>
V4L2PipeFactory::getEventPipe(MUINT32 sensorIndex,
                              const char* szCallerName,
                              MUINT32 /* apiVersion=0 */
) {
  if (sensorIndex >= kPipe_Sensor_RSVD) {
    MY_LOGE("InvalidSensorIdx = %d", sensorIndex);
    return nullptr;
  }

  std::lock_guard<std::mutex> lk(m_eventpipe_lock);
  auto eventpipe = mwp_eventpipe[sensorIndex].lock();  // lazy init
  if (eventpipe.get() == nullptr) {
    eventpipe = std::shared_ptr<V4L2IEventPipe>(
        new V4L2EventPipe(sensorIndex, szCallerName));
    mwp_eventpipe[sensorIndex] = eventpipe;
  }
  return eventpipe;
}

MBOOL V4L2PipeFactory::query(MUINT32 port_idx,
                             MUINT32 cmd,
                             const NormalPipe_QueryIn& input,
                             NormalPipe_QueryInfo* query_info) const {
  MBOOL ret = MTRUE;
  NormalPipe_InputInfo qryInput;

  if (cmd) {
    CAM_CAPABILITY* pinfo = CAM_CAPABILITY::getInstance(LOG_TAG);

    qryInput.format = input.img_fmt;
    qryInput.width = input.width;
    qryInput.pixelMode = input.pix_mode;

    if (pinfo->GetCapability(port_idx, static_cast<ENPipeQueryCmd>(cmd),
                             qryInput, query_info) == MFALSE) {
      MY_LOGE("some query op fail");
      ret = MFALSE;
    }
  } else {
    MY_LOGW("invalid cmd");
    ret = MFALSE;
  }

  return ret;
}

MBOOL V4L2PipeFactory::query(MUINT32 port_idx,
                             MUINT32 cmd,
                             MINT imgFmt,
                             NormalPipe_QueryIn const& input,
                             NormalPipe_QueryInfo* query_info) const {
  MBOOL ret = MTRUE;
  NormalPipe_InputInfo qryInput;

  if (cmd) {
    CAM_CAPABILITY* pinfo = CAM_CAPABILITY::getInstance(LOG_TAG);

    qryInput.format = static_cast<EImageFormat>(imgFmt);
    qryInput.width = input.width;
    qryInput.pixelMode = input.pix_mode;

    if (pinfo->GetCapability(port_idx, static_cast<ENPipeQueryCmd>(cmd),
                             qryInput, query_info) == MFALSE) {
      MY_LOGE("some query op fail");
      ret = MFALSE;
    }
  } else {
    MY_LOGW("invalid cmd");
    ret = MFALSE;
  }

  return ret;
}

MBOOL V4L2PipeFactory::query(MUINT32 cmd, MUINTPTR IO_struct) const {
  return MTRUE;
}

}  // namespace NSCamIOPipe
}  // namespace NSIoPipe
}  // namespace NSCam
