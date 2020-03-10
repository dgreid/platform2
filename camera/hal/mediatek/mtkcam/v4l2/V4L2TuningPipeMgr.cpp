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

#define LOG_TAG "V4L2TuningPipeMgr"

// Implementation of header
#include <mtkcam/v4l2/V4L2TuningPipeMgr.h>

#include <mtkcam/v4l2/mtk_p1_metabuf.h>

#include <mtkcam/v4l2/property_strings.h>

// MTKCAM
#include <mtkcam/aaa/aaa_hal_common.h>
#include <mtkcam/utils/std/Log.h>  // log
#include <property_lib.h>

// STL
#include <map>            // std::map
#include <memory>         // std::shared_ptr
#include <mutex>          // std::mutex, std::lock_guard
#include <string>         // std::string
#include <unordered_map>  // std::unordered_map
#include <utility>        // std::move
#include <vector>         // std::vector

#include <cutils/compiler.h>

/* indicates the amount of tuning buffer that going to ask fro driver */
#define TUNING_BUF_COUNT 6

/* describes to dump TUNING buffer before enqueue to driver */
#define _DEBUG_DUMP_TUNING_ 1

/* describes the path to dump */
#define _DEBUG_DUMP_PATH_ "/var/cache/camera/"

static int g_dump = []() {
  return property_get_int32(PROP_V4L2_TUNINGPIPEMGR_DUMP, 0);
}();

static int g_logLevel = []() {
  return property_get_int32(PROP_V4L2_TUNINGPIPEMGR_LOGLEVEL, 2);
}();

static void _dump_tuning(NSCam::IImageBuffer* pImg, int magic_num) {
#if _DEBUG_DUMP_TUNING_
  if (!!g_dump == false) {
    return;
  }

  // e.g.: /var/cache/camera/meta1_1920x1080_magic_0.bin
  std::string fname(_DEBUG_DUMP_PATH_);
  fname += "tuning_";
  fname += std::to_string(pImg->getImgSize().w);
  fname += "x";
  fname += std::to_string(pImg->getImgSize().h);
  fname += "_magic_";
  fname += std::to_string(magic_num);
  fname += ".bin";
  pImg->saveToFile(fname.c_str());
  CAM_LOGD("saveToFile: %s", fname.c_str());
#endif
}

namespace v4l2 {

V4L2TuningPipeMgr::V4L2TuningPipeMgr(PipeTag pipe_tag, uint32_t sensorIdx)
    : m_logLevel(g_logLevel), m_seqCnt(1), m_enqCount(0) {
  m_sensorIdx = sensorIdx;
  // create IHal3A
  MAKE_Hal3A(
      m_pHal3A, [](IHal3A* p) { p->destroyInstance(LOG_TAG); }, sensorIdx,
      LOG_TAG);

  IV4L2PipeFactory* pFactory = IV4L2PipeFactory::get();
  if (CC_UNLIKELY(pFactory == nullptr)) {
    CAM_LOGE("cannot create IV4L2PipeFactory");
    m_pHal3A = nullptr;
    return;
  }

  m_pTuningPipe = pFactory->getSubModule(kPipeTuning, sensorIdx, LOG_TAG);
  if (CC_UNLIKELY(!m_pTuningPipe.get())) {
    CAM_LOGE("create tuningpipe failed");
    m_pHal3A = nullptr;
    return;
  }

  /* init */
  MBOOL ret = m_pTuningPipe->init(pipe_tag);
  if (CC_UNLIKELY(!ret)) {
    CAM_LOGE("init tuningpipe failed");
    m_pTuningPipe = nullptr;
    m_pHal3A = nullptr;
  }

  /* configure pipe */
  if (CC_UNLIKELY(configurePipe() != 0)) {
    CAM_LOGE("configure tuningpipe failed");
    m_pTuningPipe->uninit();
    m_pTuningPipe = nullptr;
    m_pHal3A = nullptr;
    return;
  }
}

V4L2TuningPipeMgr::~V4L2TuningPipeMgr() {
  /* unlock buffers */
  for (auto& el : m_driverBuffers) {
    if (CC_LIKELY(!!el.get())) {
      el->unlockBuf(LOG_TAG);
    }
  }

  /* uninit sttpipe and recycle it */
  if (CC_LIKELY(isValidState())) {
    m_pTuningPipe->uninit();
    m_pTuningPipe = nullptr;
  }
  m_pHal3A = nullptr;  // clear first, avoid unreference of m_eventName
}

void V4L2TuningPipeMgr::terminate() {
  CAM_LOGD_IF(m_logLevel, "manually terminate [+]");
  m_worker_status = false;
  // tells IHal3A it's time to stop
  m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_WaitTuningReq,
                       IPC_IspTuningMgr_T::cmdTERMINATED, 0);

  CAM_LOGD_IF(m_logLevel, "manually terminate [-]");
}

void V4L2TuningPipeMgr::revive() {
  CAM_LOGD_IF(m_logLevel, "manually revive [+]");
  // tells IHal3A restart IPCTuningMgr
  m_worker_status = true;
  m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_WaitTuningReq,
                       IPC_IspTuningMgr_T::cmdREVIVE, 0);
  CAM_LOGD_IF(m_logLevel, "manually revive [-]");
}

int V4L2TuningPipeMgr::startWorker() {
  if (CC_UNLIKELY(isValidState() == false)) {
    CAM_LOGE("cannot start V4L2TuningPipeMgr since state is invalid");
    return -EPIPE;
  }

  revive();  // revive IPCTuningMgr first;
  return V4L2DriverWorker::start();
}

int V4L2TuningPipeMgr::startPipe() {
  if (CC_UNLIKELY(isValidState() == false)) {
    CAM_LOGE("cannot start V4L2TuningPipeMgr since state is invalid");
    return -EPIPE;
  }

  return m_pTuningPipe->start();
}

int V4L2TuningPipeMgr::stop() {
  if (CC_UNLIKELY(isValidState() == false)) {
    CAM_LOGE("cannot stop V4L2TuningPipeMgr since state is invalid");
    return -EPIPE;
  }

  m_pTuningPipe->stop();
  terminate();  // kill IPCTuningMgr first.
  return V4L2DriverWorker::stop();
}

void V4L2TuningPipeMgr::waitUntilEnqued() {
  if (CC_UNLIKELY(isValidState() == false)) {
    CAM_LOGW("ignore %s since state is invalid", __FUNCTION__);
    return;
  }

  /* wait until a buffer has been enqueued */
  std::unique_lock<std::mutex> lk(m_enqMutex);
  m_enqCond.wait(
      lk, [this]() { return 0 < m_enqCount.load(std::memory_order_relaxed); });
}

int V4L2TuningPipeMgr::configurePipe() {
  /* port */
  std::vector<PortInfo> v_port_info;
  /* mmap buffers, from driver */
  std::map<int, std::vector<std::shared_ptr<IImageBuffer>>> map_vbuffers;

  /* prepare Port info */
  v_port_info.emplace_back(NSCam::NSIoPipe::PORT_TUNING, NSCam::eImgFmt_BLOB,
                           MSize(MTK_P1_SIZE_TUNING, 1), MRect(),
                           MTK_P1_SIZE_TUNING, 0, 0, 0, 0, TUNING_BUF_COUNT);
  map_vbuffers.emplace(NSCam::NSIoPipe::PORT_TUNING.index,
                       std::vector<std::shared_ptr<IImageBuffer>>{});

  /* configure pipe & retrieve mmap buffers */
  QInitParam params;
  params.mPortInfo = std::move(v_port_info);  // copy container
  MBOOL ret = m_pTuningPipe->configPipe(params, &map_vbuffers);
  if (!ret) {
    CAM_LOGE("configure tuningpipe failed");
    return -EPIPE;
  }

  /* push these buffer into unused buffer queue */
  m_driverBuffers = std::move(map_vbuffers[NSCam::NSIoPipe::PORT_TUNING.index]);
  auto& v_imgs = m_driverBuffers;
  if (v_imgs.empty()) {
    CAM_LOGE("has no tuning buffer, test tuningpipe failed");
    return -EPIPE;
  }

  for (auto& p : v_imgs) {
    if (p.get() == nullptr) {
      continue;
    }

    p->lockBuf(LOG_TAG, NSCam::eBUFFER_USAGE_HW_CAMERA_READWRITE |
                            NSCam::eBUFFER_USAGE_SW_READ_OFTEN);
    BufInfo info(NSCam::NSIoPipe::PORT_TUNING, p.get(),
                 MSize(p->getImgSize().w, p->getImgSize().h),
                 MRect(p->getImgSize().w, p->getImgSize().h), 0);
    std::lock_guard<std::mutex> lk(m_unusedBufsLock);
    m_unusedBufs.emplace(info);
  }
  MY_LOGI("tuning buf count=%zu", m_unusedBufs.size());

  return 0;
}

bool V4L2TuningPipeMgr::isValidState() const {
  const bool b1 = m_pTuningPipe != nullptr;
  const bool b2 = m_pHal3A != nullptr;

  if (CC_UNLIKELY(!b1)) {
    CAM_LOGW("tuningPipe is null");
  }
  if (CC_UNLIKELY(!b2)) {
    CAM_LOGW("hal3a is null");
  }

  return b1 && b2;
}

int V4L2TuningPipeMgr::enqueTuningBufToDrv(IImageBuffer* pImage) {
  QBufInfo buf_info;
  buf_info.mvOut.emplace_back(
      NSCam::NSIoPipe::PORT_TUNING, pImage,
      MSize(pImage->getImgSize().w, pImage->getImgSize().h),
      MRect(pImage->getImgSize().w, pImage->getImgSize().h),
      m_seqCnt.fetch_add(1, std::memory_order_relaxed));

  auto r = m_pTuningPipe->enque(buf_info);
  if (r) {
    m_enqCount++;
    m_enqCond.notify_all();
  }
  return (r == MTRUE ? 0 : -1);
}

int V4L2TuningPipeMgr::composeTuningBuffer(BufInfo* pInfo,
                                           mtk_p1_metabuf_tuning** ppBuffer,
                                           int* pr_buffer_fd) {
  if (CC_UNLIKELY(pInfo->mBuffer == nullptr)) {
    CAM_LOGE("compose tuning buffer failed since no IImageBuffer");
    return -EPIPE;
  }

  void* va = reinterpret_cast<void*>(pInfo->mBuffer->getBufVA(0));
  if (CC_UNLIKELY(va == nullptr)) {
    CAM_LOGE("compose tuning buffer failed since no VA");
    return -EPIPE;
  }

  /* compose as mtk_p1_metabuf_tuning */
  *ppBuffer = reinterpret_cast<mtk_p1_metabuf_tuning*>(va);
  *pr_buffer_fd = reinterpret_cast<int>(pInfo->mBuffer->getFD(0));

  /* move to in-using queue */
  std::lock_guard<std::mutex> lk(m_bufInfoLock);
  m_bufInfoTuning.emplace(va, *pInfo);
  return 0;
}

void V4L2TuningPipeMgr::job() {
  int cmd = IPC_IspTuningMgr_T::cmdWAIT_REQUEST;  // wait request
  uint32_t magicnum = 0xFFFFFFFF;

  // wait event from server...
  {
    IPC_IspTuningMgr_T p;
    CAM_LOGD_IF(m_logLevel, "wait IHal3A's response [+]");
    auto result = m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_WaitTuningReq,
                                       IPC_IspTuningMgr_T::cmdWAIT_REQUEST,
                                       reinterpret_cast<MINTPTR>(&p));
    CAM_LOGD_IF(m_logLevel, "wait IHal3A's response [-]");
    if (result != MTRUE) {
      if (m_worker_status)
        CAM_LOGW("IHal3A wait response fail, may be disconnected.");
      std::this_thread::yield();
      return;
    }
    CAM_LOGD_IF(m_logLevel, "IHal3A's response=%d", p.response);
    cmd = p.response;       // update responsed
    magicnum = p.magicnum;  // saves magicnum for cmdACQUIRE_FROM_FMK
  }

  if (IPC_IspTuningMgr_T::cmdACQUIRE_FROM_FMK ==
      cmd) {  // acquire buffer by IHal3A
    // we have to dequeue a tuning buffer from driver,
    // and enqueue to ISP framework.

    /* dequeue a tuning buffer from driver */
    mtk_p1_metabuf_tuning* pTuning = nullptr;

    /* dequeue tuning buffer from driver ... */
    constexpr const int RETRY_TIMES = 100;
    int dequeOK = 0;
    int bufFd = 0;
    for (size_t retry = 0; retry <= RETRY_TIMES; ++retry) {
      dequeOK = dequeFromDrv(magicnum, &pTuning, &bufFd);
      if (CC_LIKELY(dequeOK == 0)) {
        break;
      }
    }

    /* if deque from driver failed more */
    if (CC_UNLIKELY(dequeOK != 0)) {
      CAM_LOGE(
          "deque tuning buffer from driver failed "
          "more than %d times ",
          RETRY_TIMES);
      assert(0);  // basically, this is a fatal error and cannot recover,
                  // assert it and let RDs debug
    }

    IPC_IspTuningMgr_T p;
    p.response = cmd;
    p.magicnum = magicnum;
    p.bufVa = reinterpret_cast<uint64_t>(pTuning);
    p.bufFd = bufFd;

    // exchanging tuning buffer
    CAM_LOGD_IF(m_logLevel, "ExchangeTuningBuf(ACQUIRE_FROM_FMK) [+]");
    auto result = m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_ExchangeTuningBuf, cmd,
                                       reinterpret_cast<MINTPTR>(&p));
    CAM_LOGD_IF(m_logLevel, "ExchangeTuningBuf(ACQUIRE_FROM_FMK) [-]");
    CAM_LOGD_IF(m_logLevel,
                "exchange buf(ACQUIRE_FROM_FMK): result=%d, bufVa=%" PRIu64
                ", bufFd=%" PRIu64 "",
                result, p.bufVa, p.bufFd);
    if (result != MTRUE) {
      CAM_LOGW("IHal3A E3ACtrl_IPC_P1_ExchangeTuningBuf cmd(%d) returns fail",
               cmd);
    } else {
    }
  } else if (IPC_IspTuningMgr_T::cmdRESULT_FROM_FMK ==
             cmd) {  // result from IHal3A
    IPC_IspTuningMgr_T p;
    CAM_LOGD_IF(m_logLevel, "ExchangeTuningBuf(RESULT_FROM_FMK) [+]");
    auto result = m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_ExchangeTuningBuf, cmd,
                                       reinterpret_cast<MINTPTR>(&p));
    CAM_LOGD_IF(m_logLevel, "ExchangeTuningBuf(RESULT_FROM_FMK) [-]");
    CAM_LOGD_IF(m_logLevel,
                "exchange buf(RESULT_FROM_FMK): result=%d, magicnum = %u, "
                "bufVa=%" PRIu64 ", bufFd=%" PRIu64 "",
                result, p.magicnum, p.bufVa, p.bufFd);
    if (result == MTRUE) {
      enqueToDrv(p.magicnum, reinterpret_cast<mtk_p1_metabuf_tuning*>(p.bufVa));
    } else {
      std::this_thread::yield();
    }
  } else if (IPC_IspTuningMgr_T::cmdTERMINATED == cmd) {
    // the server has been terminated.
  } else {
    CAM_LOGW("unsupported command %d", cmd);
  }
}

int V4L2TuningPipeMgr::dequeFromDrv(uint32_t magicNum,
                                    mtk_p1_metabuf_tuning** ppBuffer,
                                    int* pr_buffer_fd) {
  CAM_LOGD_IF(m_logLevel, "dequeBuffer from driver, magicNum=%u [+]", magicNum);
  /* checks the unused buffer queue, if there's some, return one from it */
  {
    std::unique_lock<std::mutex> lk(m_unusedBufsLock);
    if (!m_unusedBufs.empty()) {
      BufInfo info = m_unusedBufs.front();
      m_unusedBufs.pop();
      lk.unlock();  // unlock first
      /* compose buffer */
      int err = composeTuningBuffer(&info, &(*ppBuffer), pr_buffer_fd);
      if (CC_LIKELY(err == 0)) {
        return 0;
      }
      CAM_LOGE("compose buffer failed, try dequeue a new one");
      enqueTuningBufToDrv(info.mBuffer);  // enqueue back..
    }
  }

  /* no un-used buffers, deque from driver */
  const QPortID ports = []() {
    QPortID ports;
    ports.mvPortId.push_back(NSCam::NSIoPipe::PORT_TUNING);
    return ports;
  }();

  QBufInfo q_buf_info;
  MBOOL ret = m_pTuningPipe->deque(ports, &q_buf_info);
  if (!ret) {
    CAM_LOGE("dequeue tuning buffer from driver failed");
    return -EPIPE;
  }

  /* check output */
  if (CC_UNLIKELY(q_buf_info.mvOut.empty())) {
    CAM_LOGE("dequeue ok but output is empty");
    return -EPIPE;
  }

  /* place all buffers into unused buffer queue except the first one */
  m_unusedBufsLock.lock();
  for (size_t i = 1; i < q_buf_info.mvOut.size(); ++i) {
    m_unusedBufs.emplace(q_buf_info.mvOut[i]);
  }
  m_unusedBufsLock.unlock();

  /* compose the first buffer */
  int err =
      composeTuningBuffer(&q_buf_info.mvOut[0], &(*ppBuffer), pr_buffer_fd);
  if (CC_UNLIKELY(err != 0)) {
    CAM_LOGE("compose buffer failed, try dequeue a new one");
    enqueTuningBufToDrv(q_buf_info.mvOut[0].mBuffer);
    return -EPIPE;
  }

  CAM_LOGD_IF(m_logLevel, "dequeue tuning (magicnum=%d)", magicNum);

  return 0;
}

int V4L2TuningPipeMgr::enqueToDrv(uint32_t magicnum,
                                  mtk_p1_metabuf_tuning* pBuffer) {
  /* finding buffer from the in-using buffer container */
  std::unique_lock<std::mutex> lk(m_bufInfoLock);
  auto itr_pair_bufinfo =
      m_bufInfoTuning.find(reinterpret_cast<void*>(pBuffer));
  if (CC_UNLIKELY(itr_pair_bufinfo == m_bufInfoTuning.end())) {
    /* not found, weird ignore. */
    CAM_LOGE("cannot find in-using buffer %p, cannot enqueue to driver.",
             pBuffer);
    return -ENOENT;
  }
  /* found, remove it from in-using container and enqueue to driver */
  BufInfo info = itr_pair_bufinfo->second;
  m_bufInfoTuning.erase(itr_pair_bufinfo);
  lk.unlock();  // unlock first.

  /* update magic number */
  info.FrameBased.mMagicNum_tuning = magicnum;

  /* dump tuning */
  _dump_tuning(info.mBuffer, info.FrameBased.mMagicNum_tuning);

  return enqueTuningBufToDrv(info.mBuffer);
}
};  // namespace v4l2
