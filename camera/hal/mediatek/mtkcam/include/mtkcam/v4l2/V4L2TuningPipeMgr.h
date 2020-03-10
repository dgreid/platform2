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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_V4L2_V4L2TUNINGPIPEMGR_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_V4L2_V4L2TUNINGPIPEMGR_H_

// MTKCAM
#include <mtkcam/aaa/IHal3A.h>
#include <mtkcam/utils/imgbuf/IImageBuffer.h>
#ifdef __ANDROID__
#include <mtkcam/drv/tuning_mgr.h>
#else
#include <mtkcam/drv/iopipe/CamIO/V4L2IIOPipe.h>
#endif

// MTKCAM/V4L2
#include <mtkcam/v4l2/ipc_queue.h>
#include <mtkcam/v4l2/mtk_p1_metabuf.h>
#include <mtkcam/v4l2/V4L2DriverWorker.h>

// STL
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

namespace v4l2 {

using NS3Av3::E3ACtrl_IPC_P1_ExchangeTuningBuf;
using NS3Av3::E3ACtrl_IPC_P1_WaitTuningReq;
using NS3Av3::IHal3A;
using NS3Av3::IPC_IspTuningMgr_T;
using NSCam::IImageBuffer;
using NSCam::NSIoPipe::NSCamIOPipe::BufInfo;
using NSCam::NSIoPipe::NSCamIOPipe::IV4L2PipeFactory;
using NSCam::NSIoPipe::NSCamIOPipe::kPipeTuning;
using NSCam::NSIoPipe::NSCamIOPipe::PipeTag;
using NSCam::NSIoPipe::NSCamIOPipe::PortInfo;
using NSCam::NSIoPipe::NSCamIOPipe::QBufInfo;
using NSCam::NSIoPipe::NSCamIOPipe::QInitParam;
using NSCam::NSIoPipe::NSCamIOPipe::QPortID;
using NSCam::NSIoPipe::NSCamIOPipe::V4L2IIOPipe;

class VISIBILITY_PUBLIC V4L2TuningPipeMgr : public V4L2DriverWorker {
 public:
  V4L2TuningPipeMgr(PipeTag pipe_tag, uint32_t sensorIdx);
  virtual ~V4L2TuningPipeMgr();

  // re-implementations of V4L2DriverWorker
 public:
  virtual int startWorker();
  virtual int startPipe();
  int stop() override;
  virtual void waitUntilEnqued();

 private:
  void terminate();
  void revive();

 protected:
  void job() override;

 protected:
  virtual int dequeFromDrv(uint32_t magicNum,
                           mtk_p1_metabuf_tuning** ppBuffer,
                           int* pr_buffer_fd);

  virtual int enqueToDrv(uint32_t magicNum, mtk_p1_metabuf_tuning* pBuffer);

 private:
  int configurePipe();  // configure V4L2IIOPipe (Tuning) and enque default
                        // buffer

  // check the V4L2TuningPipeMgr state
  bool isValidState() const;

  // enque TUNING to driver
  int enqueTuningBufToDrv(IImageBuffer* pImage);

  // compose un-used tuning buffer to mtk_p1_metabuf_tuning
  int composeTuningBuffer(BufInfo* pInfo,
                          mtk_p1_metabuf_tuning** ppBuffer,
                          int* pr_buffer_fd);

 private:
  int m_sensorIdx;
  int m_logLevel;

  /* hal 3A instance and tuning pipe must exists */
  std::shared_ptr<IHal3A> m_pHal3A;
  std::shared_ptr<V4L2IIOPipe> m_pTuningPipe;

  /* saves BufInfo, key is address of BufInfo::mBuffer->getVA(0) */
  std::map<void*, BufInfo> m_bufInfoTuning;
  std::mutex m_bufInfoLock;

  /* buffer dequeued from driver, not used yet */
  std::queue<BufInfo> m_unusedBufs;
  std::mutex m_unusedBufsLock;

  /* sequence number (magic number) */
  std::atomic<uint32_t> m_seqCnt;

  /* enque count and cond */
  std::atomic<uint32_t> m_enqCount;
  std::mutex m_enqMutex;
  std::condition_variable m_enqCond;
  /* hold life cycle of buffers from driver */
  std::vector<std::shared_ptr<IImageBuffer> > m_driverBuffers;
};

};  // namespace v4l2

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_V4L2_V4L2TUNINGPIPEMGR_H_
