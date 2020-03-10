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

#define LOG_TAG "V4L2SttPipeMgr"

// Implementation of header
#include <mtkcam/v4l2/mtk_p1_metabuf.h>
#include <mtkcam/v4l2/property_strings.h>
#include <mtkcam/v4l2/V4L2SttPipeMgr.h>

// MTKCAM
#include <mtkcam/utils/std/Log.h>  // log
#include <property_lib.h>

#include <mtkcam/aaa/aaa_hal_common.h>

// STL
#include <map>            // std::map
#include <memory>         // std::shared_ptr
#include <mutex>          // std::mutex
#include <string>         // std::string
#include <unordered_map>  // std::unordered_map
#include <utility>        // std::move
#include <vector>         // std::vector

#include <cutils/compiler.h>

using NSCam::NSIoPipe::NSCamIOPipe::BufInfo;
using NSCam::NSIoPipe::NSCamIOPipe::ENPipeCmd_SET_META2_DISABLED;
using NSCam::NSIoPipe::NSCamIOPipe::IV4L2PipeFactory;
using NSCam::NSIoPipe::NSCamIOPipe::kPipeStt;
using NSCam::NSIoPipe::NSCamIOPipe::kPipeStt2;
using NSCam::NSIoPipe::NSCamIOPipe::PortInfo;
using NSCam::NSIoPipe::NSCamIOPipe::QBufInfo;
using NSCam::NSIoPipe::NSCamIOPipe::QInitParam;
using NSCam::NSIoPipe::NSCamIOPipe::QPortID;

/* indicates the amount of meta1/meta2 buffer that going to ask from driver */
#define META_BUF_COUNT 5

/* describes the delay time (in milliseconds) to retry deque from driver */
#define _DEQ_FROM_DRV_FAIL_RETRY_DEAY_MS_ 10

/* describes to compile impl of dump META1/META2 buffer if dequeued */
#define _DEBUG_DUMP_META_ 1

/* describes the path to dump */
#define _DEBUG_DUMP_PATH_ "/var/cache/camera/"

static int g_dump = []() {
  return property_get_int32(PROP_V4L2_STTPIPEMGR_DUMP, 0);
}();

static int g_logLevel = []() {
  return property_get_int32(PROP_V4L2_STTPIPEMGR_LOGLEVEL, 2);
}();

using NSCam::MRect;
using NSCam::MSize;

namespace v4l2 {

template <typename BUF_TYPE>
int _getMetaId() {
  return 0;
}
template <>
int _getMetaId<mtk_p1_metabuf_meta1>() {
  return 1;
}
template <>
int _getMetaId<mtk_p1_metabuf_meta2>() {
  return 2;
}

template <typename BUF_TYPE>
void _dump_meta(IImageBuffer* pImg, int magic_num) {
#if _DEBUG_DUMP_META_
  if (!!g_dump == false) {
    return;
  }

  static std::atomic<uint32_t> _serials_(0);

  // e.g.: /var/cache/camera/meta1_1920x1080_magic_0.bin
  std::string fname(_DEBUG_DUMP_PATH_);
  fname += "meta";
  fname += std::to_string(_getMetaId<BUF_TYPE>());
  fname += "_";
  fname += std::to_string(pImg->getImgSize().w);
  fname += "x";
  fname += std::to_string(pImg->getImgSize().h);
  fname += "_magic_";
  fname += std::to_string(magic_num);
  fname += "_serial_";
  fname += std::to_string(_serials_.fetch_add(1, std::memory_order_relaxed));
  fname += ".bin";
  pImg->saveToFile(fname.c_str());
  CAM_LOGD("saveToFile: %s", fname.c_str());
#endif
}

template <>
V4L2IIOPipe* V4L2SttPipeMgr::getSttPipe<mtk_p1_metabuf_meta1>() {
  return m_pSttPipe.get();
}

template <>
V4L2IIOPipe* V4L2SttPipeMgr::getSttPipe<mtk_p1_metabuf_meta2>() {
  return m_pSttPipe2.get();
}

template <>
std::atomic<uint32_t>& V4L2SttPipeMgr::getSeqCnt<mtk_p1_metabuf_meta1>() {
  return m_seqCnt1;
}

template <>
std::atomic<uint32_t>& V4L2SttPipeMgr::getSeqCnt<mtk_p1_metabuf_meta2>() {
  return m_seqCnt2;
}

template <>
V4L2SttPipeMgr::SeqCtrl&
V4L2SttPipeMgr::getSequentialControl<mtk_p1_metabuf_meta1>() {
  return m_seqCtrlMeta1;
}

template <>
V4L2SttPipeMgr::SeqCtrl&
V4L2SttPipeMgr::getSequentialControl<mtk_p1_metabuf_meta2>() {
  return m_seqCtrlMeta2;
}

template <>
int V4L2SttPipeMgr::getPortIndex<mtk_p1_metabuf_meta1>() const {
  return NSCam::NSIoPipe::PORT_META1.index;
}

template <>
int V4L2SttPipeMgr::getPortIndex<mtk_p1_metabuf_meta2>() const {
  return NSCam::NSIoPipe::PORT_META2.index;
}

V4L2SttPipeMgr::V4L2SttPipeMgr(PipeTag pipe_tag,
                               uint32_t sensorIdx,
                               int enableMeta2)
    : m_sensorIdx(sensorIdx),
      m_logLevel(g_logLevel),
      m_seqCnt1(1)  // start from 1
      ,
      m_seqCnt2(1),
      m_bDequingMeta2(false),
      m_dqErrCntMeta1(0),
      m_dqErrCntMeta2(0) {
  // create IHal3A
  MAKE_Hal3A(
      m_pHal3A, [](IHal3A* p) { p->destroyInstance(LOG_TAG); }, sensorIdx,
      LOG_TAG);

  IV4L2PipeFactory* pFactory = IV4L2PipeFactory::get();
  if (CC_UNLIKELY(pFactory == nullptr)) {
    CAM_LOGE("cannot create IV4L2PipeFactory");
    return;
  }

  m_pSttPipe = pFactory->getSubModule(kPipeStt, sensorIdx, LOG_TAG);
  if (CC_UNLIKELY(!m_pSttPipe.get())) {
    CAM_LOGE("create sttpipe failed");
    return;
  }

  m_pSttPipe2 = pFactory->getSubModule(kPipeStt2, sensorIdx, LOG_TAG);
  if (CC_UNLIKELY(!m_pSttPipe2.get())) {
    CAM_LOGE("create sttgpipe2 failed");
    return;
  }

  /* init */
  MBOOL ret = m_pSttPipe->init(pipe_tag);
  if (!ret) {
    CAM_LOGE("init sttpipe failed, tag=%#x", pipe_tag);
    destroyV4L2IIOPipe();
    return;
  }

  ret = m_pSttPipe2->init(pipe_tag);
  if (!ret) {
    CAM_LOGE("init sttpipe2 failed, tag=%#x", pipe_tag);
    destroyV4L2IIOPipe();
  }

  if (enableMeta2 == DISABLE_META2) {
    CAM_LOGI("disable linking of meta2 [+]");
    auto result =
        m_pSttPipe2->sendCommand(ENPipeCmd_SET_META2_DISABLED, 0, 0, 0);
    CAM_LOGI("disable linking of meta2 [-]");
    if (CC_UNLIKELY(result == MFALSE)) {
      CAM_LOGE("disable link of meta2 failed");
    }
    m_pSttPipe2->uninit();
    m_pSttPipe2 = nullptr;
    CAM_LOGI("destroyed m_pSttPipe2");
  }

  /* configure pipe */
  if (CC_UNLIKELY(configurePipe() != 0)) {
    CAM_LOGE("configure sttpipe failed");
    if (m_pSttPipe) {
      m_pSttPipe->uninit();
    }

    if (m_pSttPipe2) {
      m_pSttPipe2->uninit();
    }
    destroyV4L2IIOPipe();
    return;
  }

  CAM_LOGI("loglevel %d", m_logLevel);
}

V4L2SttPipeMgr::~V4L2SttPipeMgr() {
  m_pHal3A = nullptr;  // clear first, avoid unreference of m_eventName

  /* unlock buffer first */
  for (auto& el : m_map_vbuffers) {
    auto& v_spimgs = el.second;
    for (auto& i : v_spimgs) {
      if (CC_LIKELY(!!i.get())) {
        i->unlockBuf(LOG_TAG);
      }
    }
  }

  /* uninit sttpipe and recycle it */
  if (CC_LIKELY(isValidState())) {
    if (m_pSttPipe) {
      m_pSttPipe->uninit();
    }

    if (m_pSttPipe2) {
      m_pSttPipe2->uninit();
    }
    destroyV4L2IIOPipe();
  }
}

void V4L2SttPipeMgr::destroyV4L2IIOPipe() {
  m_pSttPipe = nullptr;
  m_pSttPipe2 = nullptr;
}

int V4L2SttPipeMgr::configurePipe() {
  if (m_pSttPipe.get() == nullptr && m_pSttPipe2.get() == nullptr) {
    CAM_LOGE("configurePipe failed since no stt pipe");
    return -EPIPE;
  }

  SeqCtrl& seqCtrl1 = getSequentialControl<mtk_p1_metabuf_meta1>();
  SeqCtrl& seqCtrl2 = getSequentialControl<mtk_p1_metabuf_meta2>();

  /* prepare port info */
  std::vector<PortInfo> v_port_info;
  std::vector<PortInfo> v_port_info2;

  /* container for mmap buffers */
  std::map<int, std::vector<std::shared_ptr<IImageBuffer>>> map_vbuffers;
  std::map<int, std::vector<std::shared_ptr<IImageBuffer>>> map_vbuffers2;

  v_port_info.emplace_back(NSCam::NSIoPipe::PORT_META1, NSCam::eImgFmt_BLOB,
                           MSize(MTK_P1_SIZE_META1, 1), MRect(),
                           MTK_P1_SIZE_META1, 0, 0, 0, 0, META_BUF_COUNT);
  map_vbuffers.emplace(NSCam::NSIoPipe::PORT_META1.index,
                       std::vector<std::shared_ptr<IImageBuffer>>{});

  v_port_info2.emplace_back(NSCam::NSIoPipe::PORT_META2, NSCam::eImgFmt_BLOB,
                            MSize(MTK_P1_SIZE_META2, 1), MRect(),
                            MTK_P1_SIZE_META2, 0, 0, 0, 0, META_BUF_COUNT);
  map_vbuffers2.emplace(NSCam::NSIoPipe::PORT_META2.index,
                        std::vector<std::shared_ptr<IImageBuffer>>{});

  /* configure pipe */
  if (m_pSttPipe) {
    QInitParam params;
    params.mPortInfo = std::move(v_port_info);
    MBOOL ret = m_pSttPipe->configPipe(params, &map_vbuffers);
    if (CC_UNLIKELY(!ret)) {
      CAM_LOGE("configure sttpipe failed");
      return -EPIPE;
    }
  } else {
    map_vbuffers.clear();
  }

  if (m_pSttPipe2) {
    QInitParam params;
    params.mPortInfo = std::move(v_port_info2);
    MBOOL ret = m_pSttPipe2->configPipe(params, &map_vbuffers2);
    if (CC_UNLIKELY(!ret)) {
      CAM_LOGE("configure sttpipe2 failed");
      return -EPIPE;
    }
  } else {
    map_vbuffers2.clear();
  }

  /* enqueue default buffers */
  do {
    auto& v_imgs_meta1 = map_vbuffers[NSCam::NSIoPipe::PORT_META1.index];
    auto& v_imgs_meta2 = map_vbuffers2[NSCam::NSIoPipe::PORT_META2.index];
    bool bTestOK = true;
    LOGI("stt buff size=%zu", v_imgs_meta1.size());
    if (m_pSttPipe && v_imgs_meta1.empty()) {
      LOGE("%s: has no meta1 buffer, test sttpipe failed", __FUNCTION__);
      bTestOK = false;
    }
    if (m_pSttPipe2 && v_imgs_meta2.empty()) {
      LOGE("%s: has no meta2 buffer, test sttpipe failed", __FUNCTION__);
      bTestOK = false;
    }
    if (!bTestOK) {
      return -EPIPE;
    }

    /* lambda to enqueue buffer to sttpipe */
    auto enque_to_stt = [&](V4L2IIOPipe* pPipe, MUINT32 port_idx,
                            IImageBuffer* pImg, uint32_t seq) mutable -> MBOOL {
      QBufInfo buf_info;
      pImg->lockBuf(LOG_TAG, NSCam::eBUFFER_USAGE_HW_CAMERA_READWRITE |
                                 NSCam::eBUFFER_USAGE_SW_READ_OFTEN);
      buf_info.mvOut.emplace_back(
          NSCam::NSIoPipe::PortID(port_idx), pImg,
          MSize(pImg->getImgSize().w, pImg->getImgSize().h),
          MRect(pImg->getImgSize().w, pImg->getImgSize().h), seq);
      LOGD("enque_to_stt: enque buffer(%d) to port %d", seq, port_idx);
      return pPipe->enque(buf_info);
    };

    size_t idx1 = 0;  // build heap to index key map
    size_t idx2 = 0;  // ^^^^^^^^^^^^^^^^^^^^^^^^^^^

    for (auto& p : v_imgs_meta1) {
      /* if no sttpipe, no need to configure */
      if (m_pSttPipe.get() == nullptr)
        break;

      seqCtrl1.heap2idx_meta[p.get()] = idx1++;
      auto seq = m_seqCnt1.fetch_add(1, std::memory_order_relaxed);
      MBOOL ret = enque_to_stt(m_pSttPipe.get(),
                               NSCam::NSIoPipe::PORT_META1.index, p.get(), seq);
      if (!ret) {
        LOGE("%s: enqueue to sttpipe failed", __FUNCTION__);
        return -EPIPE;
      }
    }
    for (auto& p : v_imgs_meta2) {
      /* if no sttpipe2, no need to configure */
      if (m_pSttPipe2.get() == nullptr)
        break;

      seqCtrl2.heap2idx_meta[p.get()] = idx2++;
      auto seq = m_seqCnt2.fetch_add(1, std::memory_order_relaxed);
      MBOOL ret = enque_to_stt(m_pSttPipe2.get(),
                               NSCam::NSIoPipe::PORT_META2.index, p.get(), seq);
      if (!ret) {
        LOGE("%s: enqueue to sttpipe failed", __FUNCTION__);
        return -EPIPE;
      }
    }
  } while (0);

  if (m_pSttPipe) {
    seqCtrl1.pendingQueue.resize(
        map_vbuffers[NSCam::NSIoPipe::PORT_META1.index].size());
    seqCtrl1.pendingTarget.store(0, std::memory_order_relaxed);
    /* hold std::shared_ptr<IImageBuffer> buffers, keeps them alive */
    m_map_vbuffers = std::move(map_vbuffers);
  }

  if (m_pSttPipe2) {
    seqCtrl2.pendingQueue.resize(
        map_vbuffers2[NSCam::NSIoPipe::PORT_META2.index].size());
    seqCtrl2.pendingTarget.store(0, std::memory_order_relaxed);
    m_map_vbuffers.insert(map_vbuffers2.begin(), map_vbuffers2.end());
  }

  return 0;
}

int V4L2SttPipeMgr::start() {
  if (CC_UNLIKELY(isValidState() == false)) {
    CAM_LOGE("cannot start V4L2SttPipeMgr since the state is not valid");
    return -EPIPE;
  }

  m_worker_status = true;
  m_pSttPipe->start();

  /* enable meta2 dequing thread if necessary*/
  if (m_pSttPipe2) {
    m_pSttPipe2->start();
    auto lambda_deque_meta2 = [this]() {
      m_bDequingMeta2.store(true, std::memory_order_relaxed);
      while (m_bDequingMeta2.load(std::memory_order_relaxed)) {
        job2();
      }
    };

    m_threadDeqMeta2 = std::thread(lambda_deque_meta2);
  }

  return V4L2DriverWorker::start();
}

int V4L2SttPipeMgr::stop() {
  if (CC_UNLIKELY(isValidState() == false)) {
    CAM_LOGE("cannot stop V4L2SttPipeMgr since the state is not valid");
    return -EPIPE;
  }

  m_worker_status = false;

  if (m_pSttPipe2) {
    m_pSttPipe2->stop();
    m_bDequingMeta2.store(false, std::memory_order_relaxed);
    CAM_LOGI("wait thread deque meta2 stop [+]");
    if (m_threadDeqMeta2.joinable()) {
      m_threadDeqMeta2.join();
    }
    CAM_LOGI("wait thread deque meta2 stop [-]");
  }

  m_pSttPipe->stop();
  return V4L2DriverWorker::stop();
}

template <class BUF_TYPE>
int V4L2SttPipeMgr::enqueIImageBufferToDrv(IImageBuffer* pImage) {
  auto q2drv = [this](IImageBuffer* pImage, int target_idx) {
    const int port_idx = getPortIndex<BUF_TYPE>();
    QBufInfo buf_info;
    buf_info.mvOut.emplace_back(
        NSCam::NSIoPipe::PortID(port_idx), pImage,
        MSize(pImage->getImgSize().w, pImage->getImgSize().h),
        MRect(pImage->getImgSize().w, pImage->getImgSize().h),
        getSeqCnt<BUF_TYPE>().fetch_add(1, std::memory_order_relaxed));
    getSttPipe<BUF_TYPE>()->enque(buf_info);
  };

  /* get the related sequential control object */
  SeqCtrl& seqCtrl = getSequentialControl<BUF_TYPE>();

  /* find the index of pImage */
  int target_idx = seqCtrl.heap2idx_meta.at(pImage);

  std::lock_guard<std::mutex> lk(seqCtrl.pendingQueueLock);

  if (target_idx >= seqCtrl.pendingQueue.size()) {
    CAM_LOGE("index %d is out of outgoing buffer queue range (%zu)", target_idx,
             seqCtrl.pendingQueue.size());
    return -1;
  }

  /* time to enque */
  if (target_idx == seqCtrl.pendingTarget.load()) {
    q2drv(pImage, target_idx);                      // enque to driver
    seqCtrl.pendingQueue.at(target_idx) = nullptr;  // clear

    /* checks incoming buffer */
    for (int i = (target_idx + 1) % META_BUF_COUNT; i != target_idx;
         i = (i + 1) % META_BUF_COUNT) {
      /* not dequeued yet, stopping enqueuing */
      if (nullptr == seqCtrl.pendingQueue.at(i)) {
        seqCtrl.pendingTarget.store(i, std::memory_order_release);
        break;
      }

      /* enque to driver and marked as enqueued */
      q2drv(seqCtrl.pendingQueue.at(i), i);
      seqCtrl.pendingQueue.at(i) = nullptr;
    }
  } else {
    /* not the target index, saves it wait wait for being enqued */
    seqCtrl.pendingQueue.at(target_idx) = pImage;
  }

  return 0;
}

void V4L2SttPipeMgr::job() {
  constexpr const size_t TIMEOUTMS = 200;

  /*
   * The job of V4L2SttPipeMgr is:
   *  1. To enqueue META1 to 3A framework, if V4L2SttPipeMgr dequeued
   *     any buffer from driver.
   *  2. To ask 3A framework, if there are buffers(META1) to
   *     return to driver.
   *
   *  Basically, each run must invoke these 2 tasks.
   */

  /* dequeue meta1 from driver, this method is blocking call. */
  mtk_p1_metabuf_meta1* pBuffer = nullptr;
  SttBufInfo info;
  int err = dequeFromDrv<mtk_p1_metabuf_meta1>(&pBuffer, &info, TIMEOUTMS);
  //  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  //  this method may fail, before enqueue to 3A framework, need to check if
  //  pBuffer is still nullptr.

  /* Task #1: if dequeued ok, enqueue to 3A framework */
  if (err == 0) {
    m_dqErrCntMeta1 = 0;  // clear
    if (CC_UNLIKELY(pBuffer == nullptr)) {
      CAM_LOGE("dequeFromDrv ok but pBuffer is nullptr");
    } else {
      IPC_Metabuf1_T cmd;

      cmd.magicnum = info.sequence_num;
      cmd.cmd = IPC_Metabuf1_T::cmdENQUE_FROM_DRV;
      cmd.bufVa = reinterpret_cast<uint64_t>(pBuffer);
      cmd.bufFd = reinterpret_cast<uint64_t>(info.fd);

      m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_SttControl,
                           reinterpret_cast<MUINTPTR>(&cmd), 0);

      /* if enque to 3A failed, this buffer is supposed to be enqueued to driver
       */
      if (cmd.response != IPC_Metabuf1_T::responseOK) {
        enqueToDrv<mtk_p1_metabuf_meta1>(pBuffer);
      }
    }
  } else {
    ++m_dqErrCntMeta1;
  }

  /* Task #2: return all used buffers */
  do {
    IPC_Metabuf1_T cmd;

    cmd.cmd = IPC_Metabuf1_T::cmdDEQUE_FROM_3A;

    m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_SttControl,
                         reinterpret_cast<MUINTPTR>(&cmd), 0);

    std::atomic_thread_fence(std::memory_order_acquire);

    /* if 3A returns not ok, there's no buffer to return, break loop */
    if (cmd.response != IPC_Metabuf1_T::responseOK) {
      break;
    }

    /* enqueue buffer back to driver, and do it again. */
    enqueToDrv<mtk_p1_metabuf_meta1>(
        reinterpret_cast<mtk_p1_metabuf_meta1*>(cmd.bufVa));
  } while (true);
}

void V4L2SttPipeMgr::job2() {
  constexpr const size_t TIMEOUTMS = 200;

  mtk_p1_metabuf_meta2* pBuffer2 = nullptr;
  SttBufInfo info;
  auto err = dequeFromDrv<mtk_p1_metabuf_meta2>(&pBuffer2, &info, TIMEOUTMS);
  if (err == 0) {
    m_dqErrCntMeta2 = 0;  // reset counter

    IPC_Metabuf2_T cmd;
    cmd.magicnum = info.sequence_num;
    cmd.cmd = IPC_Metabuf2_T::cmdENQUE_FROM_DRV;
    cmd.bufVa = reinterpret_cast<uint64_t>(pBuffer2);
    cmd.bufFd = reinterpret_cast<uint64_t>(info.fd);

    m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_Stt2Control,
                         reinterpret_cast<MUINTPTR>(&cmd), 0);

    // if enque to 3A failed, this buffer is supposed to be enqueued to driver
    if (cmd.response != IPC_Metabuf2_T::responseOK) {
      enqueToDrv<mtk_p1_metabuf_meta2>(pBuffer2);
    }
  } else {
    ++m_dqErrCntMeta2;
  }

  do {
    /* Step 2: check if there's any unused buffer */
    IPC_Metabuf2_T cmd;

    cmd.cmd = IPC_Metabuf2_T::cmdDEQUE_FROM_3A;

    m_pHal3A->send3ACtrl(E3ACtrl_IPC_P1_Stt2Control,
                         reinterpret_cast<MUINTPTR>(&cmd), 0);

    std::atomic_thread_fence(std::memory_order_acquire);

    /* if it's empty or others, break loop. */
    if (cmd.response != IPC_Metabuf2_T::responseOK) {
      break;
    }

    // enqueue buffer back to driver, and do it again.
    enqueToDrv<mtk_p1_metabuf_meta2>(
        reinterpret_cast<mtk_p1_metabuf_meta2*>(cmd.bufVa));
  } while (true);
}

template <class BUF_TYPE>
int V4L2SttPipeMgr::dequeFromDrv(BUF_TYPE** ppBuffer,
                                 SttBufInfo* pInfo,
                                 size_t timeoutms) {
  /* get related stt pipe instance */
  V4L2IIOPipe* pSttPipe = getSttPipe<BUF_TYPE>();
  if (pSttPipe == nullptr) {
    CAM_LOGE("stt pipe is nullptr");
    return -ENOENT;
  }

  /* get related port index */
  const auto port_idx = getPortIndex<BUF_TYPE>();

  const QPortID ports = [&]() {
    QPortID ports;
    ports.mvPortId.push_back(NSCam::NSIoPipe::PortID(port_idx));
    return ports;
  }();
  QBufInfo q_buf_info;
  /* deque */
  MBOOL ret = pSttPipe->deque(ports, &q_buf_info, timeoutms);
  if (!ret) {  // deque failed
    if (m_worker_status)
      CAM_LOGW("sttpipe returned failed");
    return -ENOENT;
  }
  CAM_LOGD("dequeue OK, q_buf_info.mvOut size=%zu", q_buf_info.mvOut.size());

  /* only keeps the last buffer for every port ID, return others to driver */
  BufInfo* pMeta = nullptr;
  auto info_itr = q_buf_info.mvOut.rbegin();
  while (info_itr != q_buf_info.mvOut.rend()) {
    /* check if buffer exists */
    if (info_itr->mBuffer == nullptr) {
      CAM_LOGW("dequeued but mBuffer is nullptr, index=%d",
               info_itr->mPortID.index);
      ++info_itr;
      continue;
    }

    CAM_LOGD("dequed index=%d, img=%p", info_itr->mPortID.index,
             info_itr->mBuffer);

    if (info_itr->mPortID.index == port_idx) {
      /* if pMeta doesn't exists, point it */
      if (pMeta == nullptr) {
        pMeta = &(*info_itr);
      } else {
        /* directly enque back to driver */
        enqueIImageBufferToDrv<BUF_TYPE>(info_itr->mBuffer);
      }
    } else {
      /* buffer not belongs to this module */
      CAM_LOGE(
          "dequeued out but the buffer doesn't belong to STT. "
          "port index=%d",
          info_itr->mPortID.index);
    }
    ++info_itr;
  }

  /* check pMeta */
  if (CC_UNLIKELY(pMeta == nullptr)) {
    CAM_LOGD("deque failed, no META1 buffer");
    return -ENOENT;
  }

  /* compose mtk_p1_metabuf_meta buffer */
  void* va = reinterpret_cast<void*>(pMeta->mBuffer->getBufVA(0));
  CAM_LOGD("meta1 va=%p", va);
  /* if we can retrieve virtual address of plane 0, compose it */
  if (CC_LIKELY(va != nullptr)) {
    *ppBuffer = reinterpret_cast<BUF_TYPE*>(va);
    /* update magic number */
    pInfo->sequence_num = pMeta->FrameBased.mMagicNum_tuning;
    pInfo->fd = reinterpret_cast<void*>(pMeta->mBuffer->getFD(0));
    /* dump buffer */
    _dump_meta<BUF_TYPE>(pMeta->mBuffer, pMeta->FrameBased.mMagicNum_tuning);
    /* push back into in-using queue */
    std::lock_guard<std::mutex> lk(m_bufInfoLock);
    m_bufInfoMeta.emplace(va, *pMeta);
  } else {
    /* otherwise, cannot use it, return to driver */
    enqueIImageBufferToDrv<BUF_TYPE>(pMeta->mBuffer);
  }

  return 0;  // OK
}

template <class BUF_TYPE>
int V4L2SttPipeMgr::enqueToDrv(BUF_TYPE* pBuffer) {
  /* find related buffer */
  std::unique_ptr<BufInfo> pInfo = [this](void* va) {
    /* find in-using queue, according va. if found it, remove it from queue */
    std::lock_guard<std::mutex> lk(m_bufInfoLock);
    auto itr = m_bufInfoMeta.find(va);
    if (itr == m_bufInfoMeta.end()) {
      return std::unique_ptr<BufInfo>();
    }
    std::unique_ptr<BufInfo> b(new BufInfo(itr->second));
    m_bufInfoMeta.erase(itr);
    return b;
  }(reinterpret_cast<void*>(pBuffer));

  if (CC_LIKELY(pInfo.get() != nullptr)) {
    enqueIImageBufferToDrv<BUF_TYPE>(pInfo->mBuffer);
  } else {
    CAM_LOGW("enque a buffer(%p) but not in records", pBuffer);
  }
  return 0;
}
};  // namespace v4l2
