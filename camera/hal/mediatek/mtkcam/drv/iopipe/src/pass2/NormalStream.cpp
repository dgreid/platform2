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

#define LOG_TAG "Iop/P2NStm"

#include "mtkcam/drv/iopipe/src/pass2/NormalStream.h"

#include <mtkcam/utils/std/Log.h>
#include <MtkCameraV4L2API.h>
#include <MediaCtrlConfig.h>
#include <unordered_map>
#include <utility>
#include <cutils/compiler.h>

using NSCam::eCOLORPROFILE_UNKNOWN;
using NSCam::v4l2::FramePackage;
using NSCam::v4l2::NormalStream;

struct ScenarioInfo {
  NSCam::v4l2::ENormalStreamTag streamTag;
  MediaDeviceTag deviceTag;
  std::string streamName;
  std::string deviceName;
  std::string unusedNodeName[MAX_UNUSED_NODE_NUM_OF_TOPOLOGY];
};

static ScenarioInfo Scenario_Mapper[] = {
    {NSCam::v4l2::ENormalStreamTag_Normal_S,
     MEDIA_CONTROLLER_P2New_Preview_FD_3DNR_IN4OUT4,
     "normal",
     "preview-out-1",
     {"mtk-cam-dip preview NR Input", "mtk-cam-dip preview Shading",
      "mtk-cam-dip preview MDP1", "mtk-cam-dip preview IMG2",
      "mtk-cam-dip preview IMG3"}},
    {NSCam::v4l2::ENormalStreamTag_Prv_S,
     MEDIA_CONTROLLER_P2New_Preview_FD_3DNR_IN4OUT4,
     "preview",
     "preview-out-1",
     {"mtk-cam-dip preview NR Input", "mtk-cam-dip preview Shading",
      "mtk-cam-dip preview MDP1", "mtk-cam-dip preview IMG2",
      "mtk-cam-dip preview IMG3"}},
    {NSCam::v4l2::ENormalStreamTag_Cap_S,
     MEDIA_CONTROLLER_P2New_Capture_FD_3DNR_IN4OUT4,
     "capture",
     "capture-out-1",
     {"mtk-cam-dip capture NR Input", "mtk-cam-dip capture Shading", "",
      "mtk-cam-dip capture MDP1", "mtk-cam-dip capture IMG2"}},
    {NSCam::v4l2::ENormalStreamTag_Rec_S,
     MEDIA_CONTROLLER_P2New_Preview_FD_3DNR_IN4OUT4,
     "record",
     "record-out-1",
     {"mtk-cam-dip preview NR Input", "mtk-cam-dip preview Shading",
      "mtk-cam-dip preview MDP1", "mtk-cam-dip preview IMG2",
      "mtk-cam-dip preview IMG3"}},
    {NSCam::v4l2::ENormalStreamTag_Rep_S,
     MEDIA_CONTROLLER_P2New_Reprocessing_FD_3DNR_IN4OUT4,
     "reprocessing",
     "reprocessing-out-1",
     {"mtk-cam-dip reprocess NR Input", "mtk-cam-dip reprocess Shading",
      "mtk-cam-dip reprocess MDP1", "mtk-cam-dip reprocess IMG2",
      "mtk-cam-dip reprocess IMG3"}},
    {NSCam::v4l2::ENormalStreamTag_Normal,
     MEDIA_CONTROLLER_P2New_Preview_FD_3DNR_IN4OUT4,
     "normal",
     "preview-out-2",
     {"mtk-cam-dip preview NR Input", "mtk-cam-dip preview Shading", "",
      "mtk-cam-dip preview IMG2", "mtk-cam-dip preview IMG3"}},
    {NSCam::v4l2::ENormalStreamTag_Prv,
     MEDIA_CONTROLLER_P2New_Preview_FD_3DNR_IN4OUT4,
     "preview",
     "preview-out-2",
     {"mtk-cam-dip preview NR Input", "mtk-cam-dip preview Shading", "",
      "mtk-cam-dip preview IMG2", "mtk-cam-dip preview IMG3"}},
    {NSCam::v4l2::ENormalStreamTag_Cap,
     MEDIA_CONTROLLER_P2New_Capture_FD_3DNR_IN4OUT4,
     "capture",
     "capture-out-2",
     {"mtk-cam-dip capture NR Input", "mtk-cam-dip capture Shading", "",
      "mtk-cam-dip capture IMG2", "mtk-cam-dip capture IMG3"}},
    {NSCam::v4l2::ENormalStreamTag_Rec,
     MEDIA_CONTROLLER_P2New_Preview_FD_3DNR_IN4OUT4,
     "record",
     "record-out-2",
     {"mtk-cam-dip preview NR Input", "mtk-cam-dip preview Shading", "",
      "mtk-cam-dip preview IMG2", "mtk-cam-dip preview IMG3"}},
    {NSCam::v4l2::ENormalStreamTag_Rep,
     MEDIA_CONTROLLER_P2New_Reprocessing_FD_3DNR_IN4OUT4,
     "reprocessing",
     "reprocessing-out-2",
     {"mtk-cam-dip reprocess NR Input", "mtk-cam-dip reprocess Shading", "",
      "mtk-cam-dip reprocess IMG2", "mtk-cam-dip reprocess IMG3"}},
    {NSCam::v4l2::ENormalStreamTag_3DNR,
     MEDIA_CONTROLLER_P2New_Preview_FD_3DNR_IN4OUT4,
     "3dnr",
     "3dnr-out-4",
     {"", "mtk-cam-dip preview Shading", "", "", ""}}};

static std::map<int, const char*> Port_Mapper = {
    // for "mtk-cam-dip preview Raw Input" & "mtk-cam-dip capture Raw Input"
    {NSImageio::NSIspio::EPortIndex_IMGI, "Raw Input"},
    // for "mtk-cam-dip preview Tuning" & "mtk-cam-dip capture Tuning"
    {NSImageio::NSIspio::EPortIndex_TUNING, "Tuning"},
    {NSImageio::NSIspio::EPortIndex_VIPI, "NR Input"},
    {NSImageio::NSIspio::EPortIndex_LCEI, "Shading"},
    // for "mtk-cam-dip preview MDP0", "mtk-cam-dip captureMDP0",
    //     "mtk-cam-dip preview MDP1", & "mtk-cam-dip capture MDP1"
    {NSImageio::NSIspio::EPortIndex_WROTO, "MDP0"},
    {NSImageio::NSIspio::EPortIndex_WDMAO, "MDP1"},  // same as above
    {NSImageio::NSIspio::EPortIndex_IMG2O, "IMG2"},  // same as above
    {NSImageio::NSIspio::EPortIndex_IMG3O, "IMG3"}   // same as above
};

static std::map<int, const char*> Port_Mapper2 = {
    // for "mtk-cam-dip preview Raw Input" & "mtk-cam-dip capture Raw Input"
    {NSImageio::NSIspio::EPortIndex_IMGI, "Raw Input"},
    // for "mtk-cam-dip preview Tuning" & "mtk-cam-dip capture Tuning"
    {NSImageio::NSIspio::EPortIndex_TUNING, "Tuning"},
    {NSImageio::NSIspio::EPortIndex_VIPI, "NR Input"},
    {NSImageio::NSIspio::EPortIndex_LCEI, "Shading"},
    // for "mtk-cam-dip preview MDP0", "mtk-cam-dip capture MDP0",
    //     "mtk-cam-dip preview MDP1", & "mtk-cam-dip capture MDP1"
    {NSImageio::NSIspio::EPortIndex_WROTO, "MDP"},
    {NSImageio::NSIspio::EPortIndex_WDMAO, "MDP"},  // same as above
    {NSImageio::NSIspio::EPortIndex_IMG2O, "MDP"},  // same as above
    {NSImageio::NSIspio::EPortIndex_IMG3O, "MDP"}   // same as above
};

static std::map<int, const char*> Port_Mapper3 = {
    // for "mtk-cam-dip preview Raw Input" & "mtk-cam-dip capture Raw Input"
    {NSImageio::NSIspio::EPortIndex_IMGI, "Raw Input"},
    // for "mtk-cam-dip preview Tuning" & "mtk-cam-dip capture Tuning"
    {NSImageio::NSIspio::EPortIndex_TUNING, "Tuning"},
    {NSImageio::NSIspio::EPortIndex_VIPI, "NR Input"},
    {NSImageio::NSIspio::EPortIndex_LCEI, "Shading"},
    // for "mtk-cam-dip preview MDP0", "mtk-cam-dip capture MDP0",
    //     "mtk-cam-dip preview MDP1", & "mtk-cam-dip capture MDP1"
    {NSImageio::NSIspio::EPortIndex_WROTO, "MDP0"},
    {NSImageio::NSIspio::EPortIndex_WDMAO, "MDP0"},  // same as above
    {NSImageio::NSIspio::EPortIndex_IMG2O, "MDP0"},  // same as above
    {NSImageio::NSIspio::EPortIndex_IMG3O, "IMG3"}   // same as above
};

/******************************************************************************
 *
 ******************************************************************************/
FramePackage::FramePackage() {}

FramePackage::FramePackage(const QParams& params) : mParams(params) {
  auto it = mParams.mvFrameParams.begin();
  for (size_t i = 0; it != mParams.mvFrameParams.end(); ++i, ++it) {
    FrameBitSet bs;
    size_t FrameSize = it->mvIn.size() + it->mvOut.size();
    for (size_t pos = 0; pos < FrameSize; ++pos) {
      if ((pos < it->mvIn.size()) &&
          (it->mvIn[pos].mPortID.index ==
               NSImageio::NSIspio::EPortIndex_IMGCI ||
           it->mvIn[pos].mPortID.index == NSImageio::NSIspio::EPortIndex_DEPI ||
           it->mvIn[pos].mPortID.index == NSImageio::NSIspio::EPortIndex_LCEI))
        bs.reset(pos);
      else
        bs.set(pos);
    }
    mDequeBitSet.insert(std::make_pair(i, bs));
    LOGI("index %zu, bitset %zu", i, bs.count());
  }
}

status_t FramePackage::updateFrame(IImageBuffer* const& frame) {
  if (frame == nullptr) {
    LOGE("invalid buffer");
    return -EINVAL;
  }

  auto it = mParams.mvFrameParams.begin();
  for (size_t i = 0; it != mParams.mvFrameParams.end(); ++i, ++it) {
    auto search = mDequeBitSet.find(i);
    if (search == mDequeBitSet.end()) {
      LOGE("search failed @%d", __LINE__);
      return -EINVAL;
    }
    LOGI("+ frame %p, bitset %zu", frame, search->second.count());

    auto jt = it->mvIn.begin();
    for (size_t j = 0; jt != it->mvIn.end(); ++j, ++jt) {
      if (jt->mPortID.index == NSImageio::NSIspio::EPortIndex_IMGCI ||
          jt->mPortID.index == NSImageio::NSIspio::EPortIndex_DEPI ||
          jt->mPortID.index == NSImageio::NSIspio::EPortIndex_LCEI) {
        search->second.reset(j);
        LOGI("matched bitset %zu (imgci)", search->second.count());
      }

      if (jt->mBuffer != nullptr && jt->mBuffer->getFD() == frame->getFD()) {
        search->second.reset(j);
        LOGI("matched bitset %zu", search->second.count());
        return NO_ERROR;
      }
    }

    auto kt = it->mvOut.begin();
    for (size_t k = 0; kt != it->mvOut.end(); ++k, ++kt) {
      if (kt->mBuffer != nullptr && kt->mBuffer->getFD() == frame->getFD()) {
        search->second.reset(it->mvIn.size() + k);
        LOGI("matched bitset %zu", search->second.count());
        return NO_ERROR;
      }
    }
  }
  return -EINVAL;
}

bool FramePackage::checkFrameDone() {
  for (auto& it : mDequeBitSet) {
    if (it.second.any()) {
      return false;
    }
  }
  return true;
}
/******************************************************************************
 *
 ******************************************************************************/

std::mutex NormalStream::mOpenLock;
std::atomic<int> NormalStream::mUserCount(0);

/******************************************************************************
 *
 ******************************************************************************/
NormalStream::NormalStream(MUINT32 openedSensorIndex,
                           NSCam::NSIoPipe::EStreamPipeID mPipeID)
    : mMediaDevice(-1) {}

/******************************************************************************
 *
 ******************************************************************************/
NormalStream::~NormalStream() {
  status_t status = NO_ERROR;

  LOGI("flush++");
  if (mPoller != nullptr) {
    mPoller->flush(true);
    mPoller = nullptr;
  }
  LOGI("flush--");

  if (!mNodes.empty()) {
    for (auto it = mNodes.begin(); it != mNodes.end(); ++it) {
      if (it->second->isStart()) {
        bool changed = false;
        it->second->setActive(true, &changed);
        if (changed && (mControl != nullptr) && (mMediaDevice >= 0))
          mControl->enableLink(mMediaDevice, DYNAMIC_LINK_BYVIDEONAME,
                               it->second->getName());
      }
    }
  }

  LOGI("clear++");
  mRequestedBuffers.clear();
  mDeviceFdToNode.clear();
  mNodes.clear();
  mAllNodes.clear();
  mMediaEntity.clear();
  LOGI("clear--");

  if (mControl != nullptr && mMediaDevice >= 0) {
    status = mControl->resetAllLinks(mMediaDevice);
    if (status != NO_ERROR)
      LOGE("mControl->resetAllLinks failed");
    status = mControl->closeMediaDevice(mMediaDevice);
    if (status != NO_ERROR)
      LOGE("mControl->closeMediaDevice failed");
    mControl = nullptr;
    mMediaDevice = -1;
  }

  mupReqApiMgr = nullptr;
}

/******************************************************************************
 *
 ******************************************************************************/
status_t NormalStream::_applyPortPolicy(MSize img_size,
                                        MINT img_fmt,
                                        MUINT32 img_rotate,
                                        bool input,
                                        int largest_w,
                                        int* port) {
  if (input) {
    if ((img_fmt == eImgFmt_YV12) || (img_fmt == eImgFmt_YUY2) ||
        (img_fmt == eImgFmt_NV12))
      *port = NSImageio::NSIspio::EPortIndex_VIPI;
    else
      *port = NSImageio::NSIspio::EPortIndex_IMGI;
  } else if (img_rotate > 0) {
    *port = NSImageio::NSIspio::EPortIndex_WROTO;
  } else if (img_size.w <= 192 && img_size.h <= 144) {
    *port = NSImageio::NSIspio::EPortIndex_IMG2O;
  } else if (img_fmt == eImgFmt_YUY2) {
    *port = NSImageio::NSIspio::EPortIndex_IMG2O;
  } else {
    if (largest_w > 0) {
      if (img_size.w < largest_w)
        *port = NSImageio::NSIspio::EPortIndex_WROTO;
      else
        *port = NSImageio::NSIspio::EPortIndex_WDMAO;
    } else {
      *port = NSImageio::NSIspio::EPortIndex_WDMAO;
    }
  }

  /*
    Both WDMAO and WROTO can be assigned to MDPx nodes.
    Selecting another port if the input one is occuppied
    */
  auto it = mPortIdxToFmt.find(*port);
  if (it != mPortIdxToFmt.end()) {
    if (*port == NSImageio::NSIspio::EPortIndex_WDMAO)
      *port = NSImageio::NSIspio::EPortIndex_WROTO;
    else if (*port == NSImageio::NSIspio::EPortIndex_WROTO)
      *port = NSImageio::NSIspio::EPortIndex_WDMAO;
    else
      return BAD_VALUE;
  }
  return OK;
}

MBOOL NormalStream::_is_swap_width_height(int transform) {
  return (transform & eTransform_ROT_90) || (transform & eTransform_ROT_270);
}

status_t NormalStream::_set_format_and_buffers(BufInfo const& buf) {
  status_t status = NO_ERROR;
  std::shared_ptr<V4L2StreamNode> node;
  MINT32 bufBoundaryInBytes[3] = {0, 0, 0};
  MUINT32 bufStridesInBytes[3] = {0, 0, 0};
  MINT32 color_order = 0;
  IImageBuffer* const& b = buf.mBuffer;
  if (b == nullptr) {
    LOGE("invalid buffer");
    return -EFAULT;
  }
  if (buf.mPortID.index == NSImageio::NSIspio::EPortIndex_IMGI ||
      buf.mPortID.index == NSImageio::NSIspio::EPortIndex_VIPI) {
    color_order = b->getColorArrangement();
  }
  for (int i = 0; i < b->getPlaneCount(); ++i) {
    bufStridesInBytes[i] = b->getBufStridesInBytes(i);
    MY_LOGI("plane %d stride %u", i, bufStridesInBytes[i]);
  }

  IImageBufferAllocator::ImgParam imgParam(
      b->getImgFormat(), b->getImgSize(), bufStridesInBytes, bufBoundaryInBytes,
      b->getPlaneCount(), b->getColorProfile(), color_order);

  status = validNode(buf.mPortID.index, &node);
  if (status != NO_ERROR) {
    LOGE("Fail to validNode, s=%d, p=%d", mStreamTag, buf.mPortID.index);
    return status;
  }
  status = node->setBufFormat(&imgParam);
  if (status != NO_ERROR) {
    LOGE("setBufFormat failed, s=%d, p=%d", mStreamTag, buf.mPortID.index);
    return status;
  }
  status = node->setupBuffers();
  if (status != NO_ERROR) {
    LOGE("setupBuffers failed, s=%d, p=%d", mStreamTag, buf.mPortID.index);
    return status;
  }
  return OK;
}

status_t NormalStream::_set_format_and_buffers(int port,
                                               MINT img_fmt,
                                               MSize img_size,
                                               size_t plane_num,
                                               MINT32 color_profile,
                                               MINT32 sensor_order) {
  LOGI("+");
  status_t status = NO_ERROR;
  MINT32 color_pf = eCOLORPROFILE_UNKNOWN;
  MINT32 color_order = 0;
  MINT32 bufBoundaryInBytes[3] = {0, 0, 0};
  MUINT32 bufStridesInBytes[3] = {0, 0, 0};
  std::shared_ptr<V4L2StreamNode> node;
  if (color_profile >= 0)
    color_pf = color_profile;
  if (sensor_order >= 0)
    color_order = sensor_order;
  IImageBufferAllocator::ImgParam imgParam(img_fmt, img_size, bufStridesInBytes,
                                           bufBoundaryInBytes, plane_num,
                                           color_pf, color_order);
  status = validNode(port, &node);
  if (status != NO_ERROR) {
    LOGE("Fail to validNode, s=%d, p=%d", mStreamTag, port);
    return status;
  }
  status = node->setBufFormat(&imgParam);
  if (status != NO_ERROR) {
    LOGE("setBufFormat failed, s=%d, p=%d", mStreamTag, port);
    return status;
  }
  status = node->setupBuffers();
  if (status != NO_ERROR) {
    LOGE("setupBuffers failed, s=%d, p=%d", mStreamTag, port);
    return status;
  }
  return OK;
}

status_t NormalStream::_find_format_and_erase(MSize img_size,
                                              MINT img_fmt,
                                              MINT32 img_rot,
                                              int port) {
  std::vector<int> erase_candidate;

  if (_is_swap_width_height(img_rot))
    std::swap(img_size.w, img_size.h);

  for (auto it = mPortIdxToFmt.begin(); it != mPortIdxToFmt.end(); it++) {
    if (it->second.imgFormat == img_fmt && it->second.imgSize.w == img_size.w &&
        it->second.imgSize.h == img_size.h) {
      erase_candidate.push_back(it->first);
    } else {
      LOGD("go-thru: %x, %d x %d , port = %d", it->second.imgFormat,
           it->second.imgSize.w, it->second.imgSize.h, it->first);
    }
  }
  LOGD("erased count = %d", erase_candidate.size());
  if (erase_candidate.size() == 0) {
    LOGD("In mPortIdxToFmt, port %d is erased, line %d", port, __LINE__);
    mPortIdxToFmt.erase(port);
  } else if (erase_candidate.size() == 1) {
    LOGD("In mPortIdxToFmt, port %d is erased, line %d", erase_candidate[0],
         __LINE__);
    mPortIdxToFmt.erase(erase_candidate[0]);
  } else if (erase_candidate.size() > 1) {
    for (auto it : erase_candidate) {
      if (it == port) {
        LOGD("In mPortIdxToFmt, port %d is erased, line %d", port, __LINE__);
        mPortIdxToFmt.erase(port);
        return OK;
      }
    }
    LOGD("In mPortIdxToFmt, port %d is erased, line %d", erase_candidate[0],
         __LINE__);
    mPortIdxToFmt.erase(erase_candidate[0]);
  }
  return OK;
}

/*
     Layout of this Meta Buffer
     |        28k        |            2k                 |       98k    | 288k |
     | tuning buffer  | LSC+LCE header info |LSC buffer | LCE buffer |
*/
#define META_BUFFER_TUNING_SIZE 1024 * 28
#define META_BUFFER_LSC_LCE_HEADER_OFFSET META_BUFFER_TUNING_SIZE
#define META_BUFFER_LSC_LCE_HEADER_SIZE 1024 * 2
#define META_BUFFER_LSC_DATA_OFFSET \
  (META_BUFFER_LSC_LCE_HEADER_OFFSET + META_BUFFER_LSC_LCE_HEADER_SIZE)
#define META_BUFFER_LSC_DATA_SIZE 1024 * 98
#define META_BUFFER_LCE_DATA_OFFSET \
  (META_BUFFER_LSC_DATA_OFFSET + META_BUFFER_LSC_DATA_SIZE)
#define META_BUFFER_LCE_DATA_SIZE 1024 * 288

status_t NormalStream::_set_meta_buffer(int port,
                                        IImageBuffer* dst_buffer,
                                        IImageBuffer* src_buffer) {
  unsigned char* meta_header_va = nullptr;
  unsigned char* meta_data_va = nullptr;
  const unsigned int header_addr_offset = META_BUFFER_LSC_LCE_HEADER_OFFSET;
  const unsigned int lsc_data_addr_offset = META_BUFFER_LSC_DATA_OFFSET;
  const unsigned int lce_data_addr_offset = META_BUFFER_LCE_DATA_OFFSET;
  DmaConfigHeader meta_header;

  if (dst_buffer == nullptr) {
    LOGE("NULL pointer of dst(tuning) buffer @ %d", __LINE__);
    return BAD_VALUE;
  }

  if (src_buffer == nullptr) {  // clear corresponding header
    meta_header_va = reinterpret_cast<unsigned char*>(dst_buffer->getBufVA(0)) +
                     header_addr_offset;
    if (port == NSImageio::NSIspio::EPortIndex_IMGCI)
      memset(meta_header_va, 0x0, sizeof(meta_header));
    else if (port == NSImageio::NSIspio::EPortIndex_LCEI)
      memset(meta_header_va + sizeof(meta_header), 0x0, sizeof(meta_header));
    return OK;
  }

  meta_header.img_fmt = src_buffer->getImgFormat();
  meta_header.img_width = src_buffer->getImgSize().w;
  meta_header.img_height = src_buffer->getImgSize().h;
  meta_header.bit_per_pixel = src_buffer->getImgBitsPerPixel();
  meta_header.buf_size_bytes = src_buffer->getBufSizeInBytes(0);
  meta_header.buf_stride_bytes = src_buffer->getBufStridesInBytes(0);
  meta_header.buf_stride_pixel = src_buffer->getBufStridesInPixel(0);
  meta_header.enable = 1;

  if (port == NSImageio::NSIspio::EPortIndex_IMGCI) {
    meta_header.port_index = NSImageio::NSIspio::EPortIndex_IMGCI;
    meta_header_va = reinterpret_cast<unsigned char*>(dst_buffer->getBufVA(0)) +
                     header_addr_offset;
    meta_data_va = reinterpret_cast<unsigned char*>(dst_buffer->getBufVA(0)) +
                   lsc_data_addr_offset;
  }

  if (port == NSImageio::NSIspio::EPortIndex_LCEI) {
    meta_header.port_index = NSImageio::NSIspio::EPortIndex_LCEI;
    meta_header_va = reinterpret_cast<unsigned char*>(dst_buffer->getBufVA(0)) +
                     header_addr_offset + sizeof(meta_header);
    meta_data_va = reinterpret_cast<unsigned char*>(dst_buffer->getBufVA(0)) +
                   lce_data_addr_offset;
  }
  memcpy(meta_header_va, &meta_header, sizeof(meta_header));
  memcpy(meta_data_va, reinterpret_cast<void*>(src_buffer->getBufVA(0)),
         src_buffer->getBufSizeInBytes(0));

  return OK;
}

MBOOL NormalStream::_is_single_output(int streamTag) {
  MBOOL is_single_output = MFALSE;

  switch (streamTag) {
    case NSCam::v4l2::ENormalStreamTag_Normal_S:
    case NSCam::v4l2::ENormalStreamTag_Prv_S:
    case NSCam::v4l2::ENormalStreamTag_Cap_S:
    case NSCam::v4l2::ENormalStreamTag_Rec_S:
    case NSCam::v4l2::ENormalStreamTag_Rep_S:
      is_single_output = MTRUE;
      break;
    default:
      break;
  }
  return is_single_output;
}

#define BUF_POOL_SIZE_P1 10
#define BUF_POOL_SIZE_P2 8

MBOOL
NormalStream::init(char const* szCallerName,
                   NSCam::v4l2::ENormalStreamTag streamTag,
                   bool hasTuning) {
  std::vector<std::shared_ptr<V4L2Device>> devices;
  status_t status = NO_ERROR;
  std::string unused_node_name;
  size_t scenario_mapper_id = 0;
  size_t i = 0;
  std::lock_guard<std::mutex> _openlock(mOpenLock);

  for (; i < ARRAY_SIZE(Scenario_Mapper); i++) {
    if (streamTag == Scenario_Mapper[i].streamTag) {
      scenario_mapper_id = i;
      mDeviceTag = Scenario_Mapper[i].deviceTag;
      mStreamTag = Scenario_Mapper[i].streamTag;
      unused_node_name.assign(Scenario_Mapper[i].unusedNodeName[0]);
      LOGI("Device Tag: %s, Stream Tag: %s, %s",
           Scenario_Mapper[i].deviceName.c_str(),
           Scenario_Mapper[i].streamName.c_str(),
           Scenario_Mapper[i].unusedNodeName[0].c_str());
      break;
    }
  }
  if (i == ARRAY_SIZE(Scenario_Mapper)) {
    LOGE("Unsupported StreamTag %d", mStreamTag);
    return MFALSE;
  }

  mControl = std::make_shared<MtkCameraV4L2API>();

  mUserCount++;

  // multiple users open different media devices
  mMediaDevice =
      mControl->openAndsetupAllLinks(mDeviceTag, &mMediaEntity, hasTuning);
  if (mMediaDevice < 0) {
    LOGE("Fail to openAndsetupAllLinks (ret = %d)", mMediaDevice);
    return MFALSE;
  }

  for (auto& it : mMediaEntity) {
    std::shared_ptr<V4L2Device> device;
    std::string const name(it->getName());
    bool changed = false;

    if (it->getType() != DEVICE_VIDEO) {
      it->getDevice(&device);
      mSubDevice = std::static_pointer_cast<V4L2Subdevice>(device);
      continue;
    } else if (it->getDevice(&device) != NO_ERROR || !device) {
      LOGE("getDevice failed");
      return MFALSE;
    }
    std::shared_ptr<V4L2StreamNode> node(new V4L2StreamNode(
        std::static_pointer_cast<V4L2VideoNode>(device), name));
    if (streamTag != NSCam::v4l2::ENormalStreamTag_Cap &&
        streamTag != NSCam::v4l2::ENormalStreamTag_Cap_S) {
      status = node->setBufPoolSize(BUF_POOL_SIZE_P2);
      if (status != NO_ERROR) {
        LOGE("setBufPoolSize(%d) failed", BUF_POOL_SIZE_P2);
        return MFALSE;
      }
    }

    status = node->setActive(true, &changed);
    if (status != NO_ERROR) {
      LOGE("setActive failed");
      return MFALSE;
    }

    // disable un-used nodes
    size_t j = 0;
    for (; j < MAX_UNUSED_NODE_NUM_OF_TOPOLOGY; j++) {
      if (name.compare(Scenario_Mapper[scenario_mapper_id].unusedNodeName[j]) ==
          0) {
        status = node->setActive(false, &changed);
        if (changed) {
          status = mControl->disableLink(mMediaDevice, DYNAMIC_LINK_BYVIDEONAME,
                                         name.c_str());
          if (status != NO_ERROR) {
            LOGE("mControl->disableLink fail, disable [%s]", name.c_str());
            return MFALSE;
          } else {
            LOGI("mControl->disableLink : %s", name.c_str());
          }
        }
        break;
      }
    }

    mDeviceFdToNode.insert(std::make_pair(device->Name(), node));
    devices.push_back(device);
    mAllNodes.push_back(node);
    LOGI("PUSH device %p node %p name: %s", device.get(), node.get(),
         name.c_str());
  }

  // preparing fake mapping  if mPortIdxToFmt is empty
  if (mPortIdxToFmt.empty()) {
    for (auto it = Port_Mapper.begin(); it != Port_Mapper.end(); ++it) {
      MINT32 bufBoundaryInBytes[3] = {0, 0, 0};
      MUINT32 bufStridesInBytes[3] = {0, 0, 0};

      if (it->first == NSImageio::NSIspio::EPortIndex_TUNING) {
        if (hasTuning) {
          IImageBufferAllocator::ImgParam imgParam(0, 0);
          mPortIdxToFmt.insert(std::make_pair(
              NSImageio::NSIspio::EPortIndex_TUNING, std::move(imgParam)));
          LOGD("In mPortIdxToFmt, port %d is inserted",
               NSImageio::NSIspio::EPortIndex_TUNING);
        }
      } else {
        IImageBufferAllocator::ImgParam imgParam(eImgFmt_UNKNOWN, MSize(1, 1),
                                                 bufStridesInBytes,
                                                 bufBoundaryInBytes, 1);
        mPortIdxToFmt.insert(std::make_pair(it->first, std::move(imgParam)));
        LOGD("In mPortIdxToFmt, port %d is inserted", it->first);
      }
    }
  }

  // setup tuning node
  if (hasTuning) {
    std::shared_ptr<V4L2StreamNode> node;
    status = validNode(NSImageio::NSIspio::EPortIndex_TUNING, &node);
    if (status != NO_ERROR) {
      LOGE("Fail to validNode NSImageio::NSIspio::EPortIndex_TUNING @ init");
      return MFALSE;
    }
    mRequestedBuffers.clear();
    auto idx = mPortIdxToFmt.find(NSImageio::NSIspio::EPortIndex_TUNING);
    if (idx != mPortIdxToFmt.end()) {
      status = node->setFormatAnGetdBuffers(&(idx->second), &mRequestedBuffers);
      if ((status != NO_ERROR) || mRequestedBuffers.empty()) {
        LOGE("setFormatAnGetdBuffers failed, EPortIndex_TUNING");
        return MFALSE;
      }
      LOGD("In mPortIdxToFmt, port %d is erased", idx->first);
      mPortIdxToFmt.erase(idx);
    }
  }

  mupReqApiMgr = std::make_unique<ReqApiMgr>(mControl, mMediaDevice, this);
  mPoller = std::make_unique<PollerThread>();

  status = mPoller->init(devices, this, POLLPRI | POLLIN | POLLOUT | POLLERR);
  if (status != NO_ERROR) {
    LOGE("poller init failed (ret = %d)", status);
    return MFALSE;
  }
  return MTRUE;
}

MBOOL
NormalStream::init(char const* szCallerName,
                   const StreamConfigure config,
                   NSCam::v4l2::ENormalStreamTag streamTag,
                   bool hasTuning) {
  LOGI("+, name=%s, tag=%d", szCallerName, streamTag);
  status_t status;
  int port_idx = 0, input_num = 0, output_num = 0, imgi_w = 0, imgi_h = 0;
  MUINT32 largest_w = 0, largest_idx = 0;
  MINT32 bufBoundaryInBytes[3] = {0, 0, 0};
  MUINT32 bufStridesInBytes[3] = {0, 0, 0};
  NSCam::v4l2::ENormalStreamTag stream_tag = streamTag;
  std::string stream_name(szCallerName);

  std::lock_guard<std::mutex> _l(mLock);

  if (mStreamName.size() != 0) {
    LOGE("Re-init:[%s]->[%s],tag:%d", mStreamName.c_str(), szCallerName,
         mStreamTag);
    return MFALSE;
  }
  mStreamName = stream_name;
  mFirstFrame = true;

  // input params correctness
  if (streamTag > NSCam::v4l2::ENormalStreamTag_3DNR) {
    LOGE("Unsupport StreamTag %d", streamTag);
    return MFALSE;
  }
  if (config.mInStreams.size() == 0 || config.mOutStreams.size() == 0) {
    LOGE("Stream [%d] init fail with %d inputs and %d outputs", streamTag,
         config.mInStreams.size(), config.mOutStreams.size());
    return MFALSE;
  }

  if (streamTag != NSCam::v4l2::ENormalStreamTag_3DNR) {
    // stream tag ended with '_S' means 'single output'
    if (streamTag % 2 == 0) {
      input_num = 1;
      output_num = 2;
    } else {
      input_num = 1;
      output_num = 1;
    }
    if (config.mOutStreams.size() < output_num) {
      LOGD(
          "init with %d configs for %d outputs, mismatched stream tag and "
          "output counts",
          config.mOutStreams.size(), output_num);
      stream_tag = static_cast<NSCam::v4l2::ENormalStreamTag>(streamTag + 1);
    }
    if (stream_tag == NSCam::v4l2::ENormalStreamTag_Cap)
      LOGW("Capture stream with more than 1 output is out of expectation. (%d)",
           stream_tag);

    if (config.mInStreams.size() > input_num) {
      LOGE("init with %d configs for %d inputs", config.mInStreams.size(),
           input_num);
      return MFALSE;
    }
    if (config.mOutStreams.size() > output_num) {
      LOGE("init with %d configs for %d outputs", config.mOutStreams.size(),
           output_num);
      return MFALSE;
    }
  }
  LOGD("stream[%d], mInStreams = %d, mOutStreams = %d", streamTag,
       config.mInStreams.size(), config.mOutStreams.size());

  mPortIdxToFmt.clear();
  mMdpIdxToFmt.clear();

  for (auto& it : config.mInStreams) {
    IImageBufferAllocator::ImgParam imgParam(
        it->getImgFormat(), it->getImgSize(), bufStridesInBytes,
        bufBoundaryInBytes, 1);
    status = _applyPortPolicy(it->getImgSize(), it->getImgFormat(),
                              it->getTransform(), true, 0, &port_idx);
    if (status != NO_ERROR) {
      LOGE("Apply port policy failed, w=%d, h=%d, f=0x%x, r=%d",
           it->getImgSize().w, it->getImgSize().h, it->getImgFormat(),
           it->getTransform());
    }
    mPortIdxToFmt.insert(std::make_pair(port_idx, std::move(imgParam)));

    // imgi w and h for vipi and img3o
    if (port_idx == NSImageio::NSIspio::EPortIndex_IMGI) {
      imgi_w = it->getImgSize().w;
      imgi_h = it->getImgSize().h;
    }
    LOGD("stream[%d], In mPortIdxToFmt, port %d is inserted,  %dx%d", streamTag,
         port_idx, it->getImgSize().w, it->getImgSize().h);
  }

  size_t i = 0;
  for (auto& it : config.mOutStreams) {
    if (it->getImgSize().w > largest_w) {
      largest_w = it->getImgSize().w;
      largest_idx = i;
    }
    i++;
  }

  if (stream_tag != NSCam::v4l2::ENormalStreamTag_Cap_S) {
    for (auto& it : config.mOutStreams) {
      MSize img_size = {};
      if (it->getImgSize().w == -1 || it->getImgSize().h == -1) {
        LOGD("fix size issue : -1x-1 -> 176x132");
        img_size = MSize(176, 132);
      } else {
        img_size = it->getImgSize();
      }
      MUINT32 planeCount = it->getBufPlanes().size();
      for (int i = 0; i < planeCount; ++i) {
        bufStridesInBytes[i] = it->getBufPlanes()[i].rowStrideInBytes;
      }
      IImageBufferAllocator::ImgParam imgParam(it->getImgFormat(), img_size,
                                               bufStridesInBytes,
                                               bufBoundaryInBytes, planeCount);
      status =
          _applyPortPolicy(img_size, it->getImgFormat(), it->getTransform(),
                           false, largest_w, &port_idx);
      if (status != NO_ERROR) {
        LOGE("Apply port policy failed, w=%d, h=%d, f=0x%x, r=%d", img_size.w,
             img_size.h, it->getImgFormat(), it->getTransform());
      }

      if (port_idx == NSImageio::NSIspio::EPortIndex_WDMAO ||
          port_idx == NSImageio::NSIspio::EPortIndex_WROTO)
        mMdpIdxToFmt.insert(std::make_pair(port_idx, imgParam));
      mPortIdxToFmt.insert(std::make_pair(port_idx, std::move(imgParam)));

      LOGD("stream[%d], In mPortIdxToFmt, port %d is inserted,  %dx%d",
           streamTag, port_idx, it->getImgSize().w, it->getImgSize().h);
    }
  }

  // tuning
  if (hasTuning) {
    IImageBufferAllocator::ImgParam imgParam(0, 0);
    mPortIdxToFmt.insert(std::make_pair(NSImageio::NSIspio::EPortIndex_TUNING,
                                        std::move(imgParam)));
    LOGD("stream[%d], In mPortIdxToFmt, port %d is inserted", streamTag,
         NSImageio::NSIspio::EPortIndex_TUNING);
  }

  // 3dnr
  if (streamTag == NSCam::v4l2::ENormalStreamTag_3DNR) {
    for (auto it = Port_Mapper.begin(); it != Port_Mapper.end(); ++it) {
      MINT32 bufBoundaryInBytes[3] = {0, 0, 0};
      MUINT32 bufStridesInBytes[3] = {0, 0, 0};

      auto search = mPortIdxToFmt.find(it->first);
      if (search == mPortIdxToFmt.end()) {
        // vipi
        if (it->first == NSImageio::NSIspio::EPortIndex_VIPI) {
          IImageBufferAllocator::ImgParam imgParam(
              eImgFmt_YV12, MSize(imgi_w, imgi_h), bufStridesInBytes,
              bufBoundaryInBytes, 3);
          mPortIdxToFmt.insert(std::make_pair(
              NSImageio::NSIspio::EPortIndex_VIPI, std::move(imgParam)));
          // img3o
        } else if (it->first == NSImageio::NSIspio::EPortIndex_IMG3O) {
          IImageBufferAllocator::ImgParam imgParam(
              eImgFmt_YV12, MSize(imgi_w, imgi_h), bufStridesInBytes,
              bufBoundaryInBytes, 3);
          mPortIdxToFmt.insert(std::make_pair(
              NSImageio::NSIspio::EPortIndex_IMG3O, std::move(imgParam)));
          // other missing types
        } else if (it->first != NSImageio::NSIspio::EPortIndex_LCEI) {
          // LCEI as unused node tagged in table,
          // will be disabled in mNodes insertion flow
          IImageBufferAllocator::ImgParam imgParam(eImgFmt_UNKNOWN, MSize(1, 1),
                                                   bufStridesInBytes,
                                                   bufBoundaryInBytes, 1);
          mPortIdxToFmt.insert(std::make_pair(it->first, std::move(imgParam)));
        }
      }
    }
  }

  init(szCallerName, stream_tag, hasTuning);

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
NormalStream::uninit(char const* szCallerName) {
  LOGI("+, name=%s, tag=%d", szCallerName, mStreamTag);
  status_t status = NO_ERROR;
  std::string stream_name(szCallerName);
  std::lock_guard<std::mutex> _l(mLock);
  std::lock_guard<std::mutex> _openlock(mOpenLock);

  if (mStreamName.size() == 0) {
    LOGE("Re-uninit:[%s]", szCallerName);
    return MFALSE;
  }

  if (mPoller != nullptr) {
    mPoller->flush(true);
    mPoller = nullptr;
  }

  if (!mNodes.empty()) {
    for (auto it = mNodes.begin(); it != mNodes.end(); ++it) {
      if (it->second->isStart()) {
        bool changed = false;
        it->second->setActive(true, &changed);
        if (changed && (mControl != nullptr) && (mMediaDevice >= 0))
          mControl->enableLink(mMediaDevice, DYNAMIC_LINK_BYVIDEONAME,
                               it->second->getName());

        it->second->stop();
      }
    }
  }

  if (!mAllNodes.empty()) {
    for (auto it = mAllNodes.begin(); it != mAllNodes.end(); ++it) {
      if ((*it)->isStart()) {
        bool changed = false;
        (*it)->setActive(true, &changed);
        if (changed && (mControl != nullptr) && (mMediaDevice >= 0))
          mControl->enableLink(mMediaDevice, DYNAMIC_LINK_BYVIDEONAME,
                               (*it)->getName());

        (*it)->stop();
      }
    }
  }

  LOGI("clear++");
  mRequestedBuffers.clear();
  mDeviceFdToNode.clear();
  mNodes.clear();
  mAllNodes.clear();
  mMediaEntity.clear();
  mDeFrameQueue.clear();
  mFrameQueue.clear();
  mPortIdxToFmt.clear();
  mMdpIdxToFmt.clear();
  mFmtKeyToNode.clear();
  mStreamName.clear();
  LOGI("clear--");

  mUserCount--;

  if (mControl != nullptr && mMediaDevice >= 0) {
    if (mUserCount <= 0) {
      status = mControl->resetAllLinks(mMediaDevice);
      if (status != NO_ERROR)
        LOGE("mControl->resetAllLinks failed");
      else
        LOGD("[%s]resetAllLinks done, usrCount=%d", szCallerName,
             mUserCount.load());
    }
    status = mControl->closeMediaDevice(mMediaDevice);
    if (status != NO_ERROR)
      LOGE("mControl->closeMediaDevice failed");
    // resetAllLinks() is used to reset mMediaDevice
    mControl = nullptr;
    mMediaDevice = -1;
  }
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
typedef enum {
  NONE_CROP_GROUP = 0,
  IMG2O_CROP_GROUP,
  WDMAO_CROP_GROUP,
  WROTO_CROP_GROUP
} CROP_GROUP_ENUM;

MBOOL
NormalStream::enque(QParams* pParams) {
  LOGI("+");
  int imgi_w = 0, imgi_h = 0;
  bool vipi_enqued = false, img3o_enqued = false;
  unsigned int unstarted_node_num = 0;
  status_t status = NO_ERROR;
  IImageBuffer* lsc_buffer = nullptr;
  IImageBuffer* lce_buffer = nullptr;
  IImageBuffer* tuning_buffer = nullptr;
  std::vector<BufInfo> all_bufs;
  std::vector<std::shared_ptr<V4L2StreamNode>> all_nodes;
  std::map<int, std::shared_ptr<V4L2StreamNode>> active_nodes;
  std::vector<std::shared_ptr<V4L2StreamNode>> required_nodes;
  std::vector<std::shared_ptr<V4L2Device>> active_devices;
  std::lock_guard<std::mutex> _l(mLock);

  if (!mControl) {
    LOGE("Please use Wrapper->init() before enque.");
    return MFALSE;
  }

  {
    std::lock_guard<std::mutex> _l(mQueueLock);
    FramePackage framePack(*pParams);
    mFrameQueue.push_back(std::move(framePack));
  }

  auto it = pParams->mvFrameParams.begin();
  for (; it != pParams->mvFrameParams.end(); ++it) {
    // remove checking of (it->mStreamTag != mStreamTag)

    for (size_t i = 0; i < it->mvIn.size(); ++i) {
      BufInfo buf;
      std::shared_ptr<V4L2StreamNode> node;
      status = validNode(it->mvIn[i].mPortID.index, &node);
      if (status != NO_ERROR) {
        LOGE("Fail to validNode, port = %d", it->mvIn[i].mPortID.index);
        return MFALSE;
      }

      if (it->mvIn[i].mPortID.index == NSImageio::NSIspio::EPortIndex_IMGCI) {
        lsc_buffer = it->mvIn[i].mBuffer;
      } else if (it->mvIn[i].mPortID.index ==
                 NSImageio::NSIspio::EPortIndex_LCEI) {
        lce_buffer = it->mvIn[i].mBuffer;
      } else if (it->mvIn[i].mPortID.index ==
                 NSImageio::NSIspio::EPortIndex_DEPI) {
        LOGD("DEPI enqued");
      } else {
        if (it->mvIn[i].mPortID.index ==
            NSImageio::NSIspio::EPortIndex_TUNING) {
          tuning_buffer = it->mvIn[i].mBuffer;
        } else if (it->mvIn[i].mPortID.index ==
                   NSImageio::NSIspio::EPortIndex_IMGI) {
          imgi_w = it->mvIn[i].mBuffer->getImgSize().w;
          imgi_h = it->mvIn[i].mBuffer->getImgSize().h;
        } else if (it->mvIn[i].mPortID.index ==
                   NSImageio::NSIspio::EPortIndex_VIPI) {
          vipi_enqued = true;
        }

        buf.mPortID = it->mvIn[i].mPortID;
        buf.mBuffer = it->mvIn[i].mBuffer;
        buf.mTransform = it->mvIn[i].mTransform;

        // Dynamic Link
        if (mFirstFrame && it == pParams->mvFrameParams.begin()) {
          unstarted_node_num++;
          if (it->mvIn[i].mPortID.index !=
              NSImageio::NSIspio::EPortIndex_TUNING) {
            if (!node->isPrepared())
              _set_format_and_buffers(buf);
          }
          // remove info in mPortIdxToFmt
          auto search = mPortIdxToFmt.find(it->mvIn[i].mPortID.index);
          if (search != mPortIdxToFmt.end()) {
            if (search->second.imgFormat == eImgFmt_UNKNOWN) {
              LOGD("In mPortIdxToFmt, port %d is erased", search->first);
              mPortIdxToFmt.erase(search);
            } else {
              if (search->second.imgFormat !=
                      it->mvIn[i].mBuffer->getImgFormat() ||
                  search->second.imgSize.w !=
                      it->mvIn[i].mBuffer->getImgSize().w ||
                  search->second.imgSize.h !=
                      it->mvIn[i].mBuffer->getImgSize().h) {
                _find_format_and_erase(it->mvIn[i].mBuffer->getImgSize(),
                                       it->mvIn[i].mBuffer->getImgFormat(),
                                       it->mvIn[i].mTransform,
                                       it->mvIn[i].mPortID.index);
              } else {
                LOGD("In mPortIdxToFmt, port %d is erased", search->first);
                mPortIdxToFmt.erase(search);
              }
            }
          } else {
            if (it->mvIn[i].mPortID.index !=
                NSImageio::NSIspio::EPortIndex_TUNING) {
              _find_format_and_erase(it->mvIn[i].mBuffer->getImgSize(),
                                     it->mvIn[i].mBuffer->getImgFormat(),
                                     it->mvIn[i].mTransform,
                                     it->mvIn[i].mPortID.index);
            }
          }
        }
        all_bufs.push_back(buf);
        all_nodes.push_back(node);
        active_nodes.insert(std::make_pair(node->getId(), node));

        int port_idx = static_cast<int>(buf.mPortID.index);
        auto check = mFmtKeyToNode.find(port_idx);
        if (check == mFmtKeyToNode.end())
          mFmtKeyToNode.insert(std::make_pair(port_idx, node));
      }
    }

    if (mStreamTag == NSCam::v4l2::ENormalStreamTag_3DNR && mFirstFrame &&
        vipi_enqued == false && it == pParams->mvFrameParams.begin()) {
      int p_vipi = NSImageio::NSIspio::EPortIndex_VIPI;
      std::shared_ptr<V4L2StreamNode> node;
      status = validNode(p_vipi, &node);
      if (status != NO_ERROR) {
        LOGE("Fail to validNode, port = %d", p_vipi);
        return MFALSE;
      }
      if (!node->isPrepared() && imgi_w > 0 && imgi_h > 0) {
        _set_format_and_buffers(p_vipi, eImgFmt_YV12, MSize(imgi_w, imgi_h), 3);
        auto search = mPortIdxToFmt.find(p_vipi);
        if (search != mPortIdxToFmt.end())
          mPortIdxToFmt.erase(search);
      }
    }

    // set or clear LSC header and copy LSC data buffer
    if (tuning_buffer) {
      _set_meta_buffer(NSImageio::NSIspio::EPortIndex_IMGCI, tuning_buffer,
                       lsc_buffer);
      _set_meta_buffer(NSImageio::NSIspio::EPortIndex_LCEI, tuning_buffer,
                       lce_buffer);
    }

    for (size_t i = 0; i < it->mvOut.size(); ++i) {
      BufInfo buf = {};
      std::shared_ptr<V4L2StreamNode> node;
      int p_idx = it->mvOut[i].mPortID.index;
      int c_gid;
      int p_sel_wdmao = NSImageio::NSIspio::EPortIndex_UNKNOWN;
      int p_sel_wroto = NSImageio::NSIspio::EPortIndex_UNKNOWN;

      if (p_idx == NSImageio::NSIspio::EPortIndex_IMG3O)
        img3o_enqued = true;

      if (p_idx == NSImageio::NSIspio::EPortIndex_WDMAO ||
          p_idx == NSImageio::NSIspio::EPortIndex_WROTO) {
        auto f_wdmao = mMdpIdxToFmt.find(NSImageio::NSIspio::EPortIndex_WDMAO);
        if (f_wdmao != mMdpIdxToFmt.end()) {
          if ((f_wdmao->second.imgSize.w ==
               it->mvOut[i].mBuffer->getImgSize().w) &&
              (f_wdmao->second.imgSize.h ==
               it->mvOut[i].mBuffer->getImgSize().h))
            p_sel_wdmao = NSImageio::NSIspio::EPortIndex_WDMAO;
        }
        auto f_wroto = mMdpIdxToFmt.find(NSImageio::NSIspio::EPortIndex_WROTO);
        if (f_wroto != mMdpIdxToFmt.end()) {
          if ((f_wroto->second.imgSize.w ==
               it->mvOut[i].mBuffer->getImgSize().w) &&
              (f_wroto->second.imgSize.h ==
               it->mvOut[i].mBuffer->getImgSize().h))
            p_sel_wroto = NSImageio::NSIspio::EPortIndex_WROTO;
        }
        // --
        if (f_wdmao->second.imgSize.w != f_wroto->second.imgSize.w ||
            f_wdmao->second.imgSize.h != f_wroto->second.imgSize.h) {
          if (p_sel_wdmao != NSImageio::NSIspio::EPortIndex_UNKNOWN)
            p_idx = p_sel_wdmao;
          if (p_sel_wroto != NSImageio::NSIspio::EPortIndex_UNKNOWN)
            p_idx = p_sel_wroto;
        }
      }

      switch (it->mvOut[i].mPortID.index) {
        case NSImageio::NSIspio::EPortIndex_IMG2O:
          c_gid = IMG2O_CROP_GROUP;
          break;
        case NSImageio::NSIspio::EPortIndex_WDMAO:
          c_gid = WDMAO_CROP_GROUP;
          break;
        case NSImageio::NSIspio::EPortIndex_WROTO:
          c_gid = WROTO_CROP_GROUP;
          break;
        default:
          c_gid = NONE_CROP_GROUP;
          break;
      }

      if (c_gid != NONE_CROP_GROUP) {
        for (size_t j = 0; j < it->mvCropRsInfo.size(); ++j) {
          if (it->mvCropRsInfo[j].mGroupID == c_gid) {
            buf.FrameBased.mResizeDst = it->mvCropRsInfo[j].mResizeDst;
            buf.FrameBased.mCropRect = it->mvCropRsInfo[j].mCropRect;
            buf.FrameBased.mCropRect.w_fractional = V4L2StreamNode::PAD_MDP0;
            buf.FrameBased.mCropRect.h_fractional = V4L2StreamNode::PAD_MDP1;
            if ((buf.FrameBased.mResizeDst.w !=
                 it->mvOut[i].mBuffer->getImgSize().w) ||
                (buf.FrameBased.mResizeDst.h !=
                 it->mvOut[i].mBuffer->getImgSize().h))
              LOGW("Invalid Dest Crop: (%d, %d), (%d, %d)",
                   it->mvOut[i].mBuffer->getImgSize().w,
                   it->mvOut[i].mBuffer->getImgSize().h,
                   buf.FrameBased.mResizeDst.w, buf.FrameBased.mResizeDst.h);
            break;
          }
        }
      }

      status = validNode(p_idx, &node);
      if (status != NO_ERROR) {
        LOGE("Fail to validNode, port = %d", p_idx);
        return MFALSE;
      }

      buf.mPortID = it->mvOut[i].mPortID;
      buf.mPortID.index = p_idx;
      buf.mBuffer = it->mvOut[i].mBuffer;
      buf.mTransform = it->mvOut[i].mTransform;

      // Dynamic Link
      if (mFirstFrame && it == pParams->mvFrameParams.begin()) {
        unstarted_node_num++;

        if (!node->isPrepared())
          _set_format_and_buffers(buf);

        if (mStreamTag != NSCam::v4l2::ENormalStreamTag_Cap_S) {
          // remove info in mPortIdxToFmt
          auto search = mPortIdxToFmt.find(p_idx);
          if (search != mPortIdxToFmt.end()) {
            if (search->second.imgFormat == eImgFmt_UNKNOWN) {
              LOGD("In mPortIdxToFmt, port %d is erased", search->first);
              mPortIdxToFmt.erase(search);
            } else {
              if (search->second.imgFormat !=
                      it->mvOut[i].mBuffer->getImgFormat() ||
                  search->second.imgSize.w !=
                      it->mvOut[i].mBuffer->getImgSize().w ||
                  search->second.imgSize.h !=
                      it->mvOut[i].mBuffer->getImgSize().h) {
                _find_format_and_erase(it->mvOut[i].mBuffer->getImgSize(),
                                       it->mvOut[i].mBuffer->getImgFormat(),
                                       it->mvOut[i].mTransform, p_idx);
              } else {
                LOGD("In mPortIdxToFmt, port %d is erased", search->first);
                mPortIdxToFmt.erase(search);
              }
            }
          } else {
            _find_format_and_erase(it->mvOut[i].mBuffer->getImgSize(),
                                   it->mvOut[i].mBuffer->getImgFormat(),
                                   it->mvOut[i].mTransform, p_idx);
          }
        }
        // capture single output use img3o or wdmao
        if (mStreamTag == NSCam::v4l2::ENormalStreamTag_Cap_S) {
          bool changed = false;
          int another_pid;
          std::shared_ptr<V4L2StreamNode> another_node;

          if (p_idx == NSImageio::NSIspio::EPortIndex_IMG3O)
            another_pid = NSImageio::NSIspio::EPortIndex_WDMAO;
          else
            another_pid = NSImageio::NSIspio::EPortIndex_IMG3O;
          status = validNode(another_pid, &another_node);
          if (status == NO_ERROR) {
            status = another_node->setActive(false, &changed);
            if (changed) {
              status =
                  mControl->disableLink(mMediaDevice, DYNAMIC_LINK_BYVIDEONAME,
                                        another_node->getName());
              if (status != NO_ERROR) {
                LOGE("mControl->disableLink fail, disable [%s], line %d",
                     another_node->getName(), __LINE__);
                return MFALSE;
              } else {
                LOGI("mControl->disableLink : %s, line %d",
                     another_node->getName(), __LINE__);
              }
            }
          } else {
            LOGE("Fail to validNode, port = %d", another_pid);
            return MFALSE;
          }
        }
      }

      all_bufs.push_back(buf);
      all_nodes.push_back(node);
      active_nodes.insert(std::make_pair(node->getId(), node));

      int port_idx = static_cast<int>(buf.mPortID.index);
      auto check = mFmtKeyToNode.find(port_idx);
      if (check == mFmtKeyToNode.end())
        mFmtKeyToNode.insert(std::make_pair(port_idx, node));
    }
    //---
    if (mStreamTag == NSCam::v4l2::ENormalStreamTag_3DNR && mFirstFrame &&
        img3o_enqued == false && it == pParams->mvFrameParams.begin()) {
      int p_img3o = NSImageio::NSIspio::EPortIndex_IMG3O;
      std::shared_ptr<V4L2StreamNode> node;
      status = validNode(p_img3o, &node);
      if (status != NO_ERROR) {
        LOGE("Fail to validNode, port = %d", p_img3o);
        return MFALSE;
      }

      if (!node->isPrepared() && imgi_w > 0 && imgi_h > 0) {
        _set_format_and_buffers(p_img3o, eImgFmt_YV12, MSize(imgi_w, imgi_h),
                                3);
        auto search = mPortIdxToFmt.find(p_img3o);
        if (search != mPortIdxToFmt.end())
          mPortIdxToFmt.erase(search);
      }
    }

    // remove fake mapping
    if (!mPortIdxToFmt.empty()) {
      for (auto it = mPortIdxToFmt.begin(); it != mPortIdxToFmt.end();) {
        if (it->second.imgFormat == eImgFmt_UNKNOWN) {
          bool changed = false;
          std::shared_ptr<V4L2StreamNode> node;

          status = validNode(it->first, &node);
          if (status == NO_ERROR) {
            status = node->setActive(false, &changed);
            if (changed) {
              status = mControl->disableLink(
                  mMediaDevice, DYNAMIC_LINK_BYVIDEONAME, node->getName());
              if (status != NO_ERROR) {
                LOGE("mControl->disableLink fail, disable [%s], line %d",
                     node->getName(), __LINE__);
                return MFALSE;
              } else {
                LOGI("mControl->disableLink : %s, line %d", node->getName(),
                     __LINE__);
              }
            }
          }
          LOGD("In mPortIdxToFmt, port %d is erased", it->first);
          mPortIdxToFmt.erase(it++);
        } else {
          it++;
        }
      }
    }

    if (mFirstFrame &&
        it == pParams->mvFrameParams.begin()) {  // first enque frame
      // check
      if (mPortIdxToFmt.empty()) {
        LOGD("Best case: size of mPortIdxToFmt == 0");
        for (auto it2 = mAllNodes.begin(); it2 != mAllNodes.end(); ++it2) {
          if ((*it2)->isActive()) {
            LOGE("No more config for this active node: %s", (*it2)->getName());
          }
        }
      } else {
        for (auto it2 = mAllNodes.begin(); it2 != mAllNodes.end(); ++it2) {
          if ((*it2)->isActive()) {
            required_nodes.push_back(*it2);
            LOGD("required nodes : %s  is pushed", (*it2)->getName());
          }
        }
        LOGD("size of mPortIdxToFmt = %d, size of requiredNodes =  %d",
             mPortIdxToFmt.size(), required_nodes.size());
        if (mPortIdxToFmt.size() == 1 && required_nodes.size() == 1) {
          auto iter1 = mPortIdxToFmt.begin();
          auto iter2 = required_nodes.begin();

          status = (*iter2)->setBufFormat(&(iter1->second));
          if (status != NO_ERROR) {
            LOGE("setBufFormat failed @line %d, %d", __LINE__, iter1->first);
            return MFALSE;
          }
          status = (*iter2)->setupBuffers();
          if (status != NO_ERROR) {
            LOGE("setupBuffers failed, @line %d, %d", __LINE__, iter1->first);
            return MFALSE;
          }
        } else {
          if (mPortIdxToFmt.size() == required_nodes.size()) {
            for (auto it2 = mPortIdxToFmt.begin(); it2 != mPortIdxToFmt.end();
                 ++it2) {
              auto search = Port_Mapper.find(it2->first);
              if (search != Port_Mapper.end()) {
                for (auto it3 = required_nodes.begin();
                     it3 != required_nodes.end(); ++it3) {
                  // partially matching the suffix of name is enough,
                  // since the prefix is the same
                  if (strstr((*it3)->getName(), search->second)) {
                    status = (*it3)->setBufFormat(&(it2->second));
                    if (status != NO_ERROR) {
                      LOGE("setBufFormat failed @line %d, %d", __LINE__,
                           it2->first);
                      return MFALSE;
                    }
                    status = (*it3)->setupBuffers();
                    if (status != NO_ERROR) {
                      LOGE("setupBuffers failed, @line %d, %d", __LINE__,
                           it2->first);
                      return MFALSE;
                    }
                  } else {
                    LOGD("Cannot match NODE names: %s != %s", (*it3)->getName(),
                         search->second);
                  }
                }
              }
            }

          } else {
            LOGE(
                "[%d]Cannot setting::size of mPortIdxToFmt = %d, size of "
                "requiredNodes =  %d",
                mStreamTag, mPortIdxToFmt.size(), required_nodes.size());
            return MFALSE;
          }
        }
      }

      mPortIdxToFmt.clear();

      // enable VIPI manually before start it
      status = mControl->enableLink(mMediaDevice, DYNAMIC_LINK_BYVIDEONAME,
                                    "mtk-cam-dip preview NR Input");
      if (status != NO_ERROR)
        LOGE("mControl->enableLink fail, disable [Vipi Input]");
      else
        LOGI("mControl->enableLink : Vipi Input");

      // start stream
      for (size_t i = 0; i < all_nodes.size(); ++i) {
        if (all_nodes[i]->start() != NO_ERROR) {
          LOGE("start failed of node: %s", all_nodes[i]->getName());
        }
      }
      for (size_t i = 0; i < required_nodes.size(); ++i) {
        if (required_nodes[i]->start() != NO_ERROR) {
          LOGE("start failed of node: %s", required_nodes[i]->getName());
        }
      }
    }
    //--Start Node VIPI for 3DNR--
    if (mStreamTag == NSCam::v4l2::ENormalStreamTag_3DNR) {
        std::shared_ptr<V4L2StreamNode> node_vipi;
        status = validNode(NSImageio::NSIspio::EPortIndex_VIPI, &node_vipi);
        if (status == NO_ERROR && node_vipi->isPrepared())
            node_vipi->start();
    }
    //--Start Node IMG3O for 3DNR or Capture output via IMG3O --
    if ((mStreamTag == NSCam::v4l2::ENormalStreamTag_3DNR) ||
    (mStreamTag == NSCam::v4l2::ENormalStreamTag_Cap_S && img3o_enqued)) {
        std::shared_ptr<V4L2StreamNode> node_img3o;
        status = validNode(NSImageio::NSIspio::EPortIndex_IMG3O, &node_img3o);
        if (status == NO_ERROR && node_img3o->isPrepared())
            node_img3o->start();
    }
    //----
    if (mFirstFrame)
      mFirstFrame = false;

    // nodes in mNodes may be active or inactive nodes
    for (auto it = mNodes.begin(); it != mNodes.end(); ++it) {
      bool changed = false;
      std::shared_ptr<V4L2StreamNode> node;
      node = it->second;
      auto idx = active_nodes.find(node->getId());
      if (idx != active_nodes.end()) {
        std::shared_ptr<V4L2Device> device;
        device = std::static_pointer_cast<V4L2Device>(node->getVideoNode());
        active_devices.push_back(device);
        node->setActive(true, &changed);
        if (changed)
          LOGD("[%d] %s State: inactive -> active", mStreamTag,
               node->getName());
      } else {
        node->setActive(false, &changed);
        if (changed)
          LOGD("[%d] %s  State: active -> inactive", mStreamTag,
               node->getName());
      }
    }
    // enque
    if (all_nodes.size() != all_bufs.size()) {
      LOGE("Number of nodes mismatches number of buffers");
      return MFALSE;
    }

    /** get request api from reqapimgr and should call notifyenque after all
     * enque request was delivered */
    int request_api = -1;
    uint32_t sync_id = ReqApiMgr::SYNC_NONE;
    if (CC_LIKELY(mupReqApiMgr != nullptr)) {
      request_api = mupReqApiMgr->retainAvlReqApi();
      if (CC_UNLIKELY(!request_api)) {
        LOGE("retain request api fail, stop enque");
        return MFALSE;
      }
    } else {
      LOGE("ReqApiMgr is missing, something was wrong");
      return MFALSE;
    }

    for (size_t i = 0; i < all_nodes.size(); ++i) {
      if (all_nodes[i]->isActive())
        LOGI("node active while enque, %s", all_nodes[i]->getName());
      else
        LOGD("node inactive while enque, %s", all_nodes[i]->getName());

      all_bufs[i].mRequestFD = request_api;

      if (all_bufs[i].mPortID.index != NSImageio::NSIspio::EPortIndex_TUNING) {
        int port_idx = static_cast<int>(all_bufs[i].mPortID.index);
        auto the_node = mFmtKeyToNode.find(port_idx);
        if (the_node != mFmtKeyToNode.end()) {
          if (the_node->second != all_nodes[i])
            LOGE("[%d]the_node->second (%s) != all_nodes[i] (%s)", mStreamTag,
                 the_node->second->getName(), all_nodes[i]->getName());
          if (the_node->second->enque(all_bufs[i], true, mSubDevice) !=
              NO_ERROR) {
            LOGE("[%d]enque failed @%d, bufs[%d], port=%d,  w=%d,h=%d,fmt=0x%x",
                 mStreamTag, __LINE__, i, all_bufs[i].mPortID.index,
                 all_bufs[i].mBuffer->getImgSize().w,
                 all_bufs[i].mBuffer->getImgSize().h,
                 all_bufs[i].mBuffer->getImgFormat());
            return MFALSE;
          }
        } else {
          LOGE("[%d]Cannot find the node from mFmtKeyToNode!", mStreamTag);
          return MFALSE;
        }
      } else {
        if (all_nodes[i]->enque(all_bufs[i], true, mSubDevice) != NO_ERROR) {
          LOGE("[%d]enque failed @%d, bufs[%d], port=%d,  w=%d,h=%d,fmt=0x%x",
               mStreamTag, __LINE__, i, all_bufs[i].mPortID.index,
               all_bufs[i].mBuffer->getImgSize().w,
               all_bufs[i].mBuffer->getImgSize().h,
               all_bufs[i].mBuffer->getImgFormat());
          return MFALSE;
        }
      }
      sync_id |= mupReqApiMgr->getSyncIdByNodeId(all_nodes[i]->getId());
    }

    int ret = mupReqApiMgr->notifyEnque(
        static_cast<ReqApiMgr::SYNC_ID>(sync_id), request_api);
    if (ret) {
      LOGE("request api notify enque fail with error %s", strerror(ret));
      return MFALSE;
    }
  }

  status = mPoller->queueRequest(0, EVENT_POLL_TIMEOUT, &active_devices);
  if (status != NO_ERROR) {
    LOGE("Poller->queueRequest failed");
    return MFALSE;
  }
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
NormalStream::deque(QParams* p_rParams, MINT64 i8TimeoutNs) {
  QParams& rParams = *p_rParams;
  LOGI("+");
  std::lock_guard<std::mutex> _ll(mLock);

  {
    FramePackage framePack;
    std::unique_lock<std::mutex> _l(mDeQueueLock);
    if (i8TimeoutNs < 0) {
      mCondition.wait(_l, [&] { return mDeFrameQueue.size() > 0; });
    } else {
      mCondition.wait_for(_l, std::chrono::nanoseconds(i8TimeoutNs),
                          [&] { return mDeFrameQueue.size() > 0; });
    }

    rParams = mDeFrameQueue.front().mParams;
    mDeFrameQueue.pop_front();
    LOGI("deque success frame size %zu", rParams.mvFrameParams.size());
  }

  return MTRUE;
}

MBOOL NormalStream::requestBuffers(
    int type,
    IImageBufferAllocator::ImgParam imgParam,
    std::vector<std::shared_ptr<IImageBuffer>>* p_buffers,
    int buf_pool_size) {
  LOGI("+");
  std::lock_guard<std::mutex> _l(mLock);
  status_t status = NO_ERROR;
  std::vector<std::shared_ptr<IImageBuffer>>& buffers = *p_buffers;

  std::shared_ptr<V4L2StreamNode> node;
  status = validNode(type, &node);
  if (status != NO_ERROR) {
    LOGE("Fail to validNode");
    return MFALSE;
  }

  if (buf_pool_size > 0) {
    status = node->setBufPoolSize(buf_pool_size);
    if (status != NO_ERROR) {
      LOGE("Fail to setBufPoolSize = %d", buf_pool_size);
      return MFALSE;
    }
  }

  if (node->isStart()) {
    LOGD("requestBuffers while node is started, type=%d", type);
    if (mRequestedBuffers.empty()) {
      LOGE("mRequestedBuffers is empty. type=%d", type);
      return MFALSE;
    } else {
      buffers.swap(mRequestedBuffers);
    }
  } else {
    LOGD("requestBuffers while node is NOT started, type=%d", type);
    if (mRequestedBuffers.empty()) {
      status = node->setFormatAnGetdBuffers(&imgParam, &buffers);
      if (status != NO_ERROR) {
        LOGE("Fail to setFormatAnGetdBuffers");
        return MFALSE;
      }
    } else {
      buffers.swap(mRequestedBuffers);
    }
  }

  for (auto& el : buffers) {
    if (CC_LIKELY(!!el)) {
      el->lockBuf(LOG_TAG, NSCam::eBUFFER_USAGE_HW_CAMERA_READWRITE |
                               NSCam::eBUFFER_USAGE_SW_READ_OFTEN);
    }
  }

  return MTRUE;
}

MBOOL NormalStream::sendCommand(ESDCmd cmd,
                                MINTPTR arg1,
                                MINTPTR arg2,
                                MINTPTR arg3) {
  LOGI("+");
  std::lock_guard<std::mutex> _l(mLock);
  status_t status = NO_ERROR;

  struct v4l2_control control = {};

  int type, cmd_id;
  std::shared_ptr<V4L2StreamNode> node = nullptr;
  switch (cmd) {
    case ENormalStreamCmd_Debug:
      LOGI("set debug mode");
      if (mSubDevice != nullptr) {
        control.id = V4L2_CID_PRIVATE_SET_CTX_MODE_NUM;
        control.value = MTK_ISP_CTX_MODE_DEBUG_BYPASS_ALL;
        if (mSubDevice->SetControl(control.id, control.value) != NO_ERROR) {
          LOGE("set control failed @%d, id:%u, value:%d", __LINE__, control.id,
               control.value);
          return MFALSE;
        }
      }
      break;
    case ENormalStreamCmd_ISPOnly:
      cmd_id = reinterpret_cast<int>(arg1);
      LOGI("ENormalStreamCmd_ISPOnly type %d", cmd_id);
      switch (cmd_id) {
        case EOUTBUF_USAGE_FD:
          type = NSImageio::NSIspio::EPortIndex_IMG2O;
          break;
        case EOUTBUF_USAGE_POSTPROC:
          type = NSImageio::NSIspio::EPortIndex_IMG3O;
          break;
        case EINBUF_USAGE_NR3D:
          type = NSImageio::NSIspio::EPortIndex_VIPI;
          break;
        case EINBUF_USAGE_LCEI:
          type = NSImageio::NSIspio::EPortIndex_LCEI;
          break;
        case EINBUF_USAGE_LSC:
          type = NSImageio::NSIspio::EPortIndex_IMGCI;
          break;
        default:
          type = NSImageio::NSIspio::EPortIndex_UNKNOWN;
          LOGE("Unknown buffer usage ID:%d", (int)arg1);
          return MFALSE;
      }
      status = validNode(type, &node);
      if (status != NO_ERROR) {
        LOGE("Fail to validNode");
        return MFALSE;
      }

      control.id = V4L2_CID_PRIVATE_SET_BUFFER_USAGE;
      control.value = cmd_id;
      if (node->setControl(&control) != NO_ERROR) {
        LOGE("set control failed @%d, id=%u, value=%d", __LINE__, control.id,
             control.value);
        return MFALSE;
      }
      break;
    default:
      LOGE("sendCommand: invalid command, %d", cmd);
      return MFALSE;
  }
  return MTRUE;
}

status_t NormalStream::notifyPollEvent(PollEventMessage* pollMsg) {
  LOGI("+");

  if (pollMsg == nullptr) {
    LOGE("Invalid poll message");
    return BAD_VALUE;
  }

  if (pollMsg->data.activeDevices == nullptr ||
      pollMsg->data.inactiveDevices == nullptr ||
      pollMsg->data.polledDevices == nullptr) {
    LOGE("Invalid devices within poll message");
    return BAD_VALUE;
  }

  if (pollMsg->id == POLL_EVENT_ID_EVENT) {
    if (pollMsg->data.activeDevices->empty() &&
        pollMsg->data.inactiveDevices->empty()) {
      LOGW("Devices flushed?");
      return OK;
    }
    if (pollMsg->data.polledDevices->empty()) {
      LOGW("No devices Polled?");
      return OK;
    }

    if (pollMsg->data.activeDevices->size() !=
        pollMsg->data.polledDevices->size()) {
      LOGD("%zu inactive nodes for request %u, retry poll",
           pollMsg->data.inactiveDevices->size(), pollMsg->data.reqId);
      LOGD("active devs = %d, polled devs = %d, mAllNodes = %d",
           pollMsg->data.activeDevices->size(),
           pollMsg->data.polledDevices->size(), mAllNodes.size());

      *pollMsg->data.polledDevices =
          *pollMsg->data.inactiveDevices;  // retry with inactive devices

      return -EAGAIN;
    } else {
      LOGD("poll success : inact=%d,act=%d,pol=%d, mAllNodes=%d, req-id=%d",
           pollMsg->data.inactiveDevices->size(),
           pollMsg->data.activeDevices->size(),
           pollMsg->data.polledDevices->size(), mAllNodes.size(),
           pollMsg->data.reqId);
    }
  } else if (pollMsg->id == POLL_EVENT_ID_ERROR) {
    LOGE("Device poll failed");
  }

  for (const auto& it : *pollMsg->data.requestedDevices) {
    BufInfo buf;
    std::shared_ptr<V4L2StreamNode> node;

    auto idx = mDeviceFdToNode.find(it->Name());
    if (idx != mDeviceFdToNode.end()) {
      node = idx->second;
    } else {
      LOGE("Cannot find node by device name");
      continue;  // loop next device
    }

    if (node->isActive())
      LOGI("node is Active for Deque, %s", node->getName());
    else
      LOGW("node is Inactive while Deque %s", node->getName());

    if (node->deque(&buf) != NO_ERROR) {
      LOGE("deque failed");
      return -EINVAL;
    }

    if (CC_LIKELY(mupReqApiMgr != nullptr)) {
      ReqApiMgr::SYNC_ID sync_id =
          mupReqApiMgr->getSyncIdByNodeId(node->getId());
      int ret = mupReqApiMgr->releaseUsedReqApi(sync_id, buf.mRequestFD);
      if (CC_UNLIKELY(ret))
        LOGE("release request api fail with error %s, leakage will happen",
             strerror(ret));
    } else {
      LOGE("ReqApiMgr is missing bypass release flow, something was wrong");
    }

    {
      std::unique_lock<std::mutex> _l(mQueueLock);
      auto it = mFrameQueue.begin();
      for (; it != mFrameQueue.end(); ++it) {
        status_t ret = it->updateFrame(buf.mBuffer);
        if (ret != NO_ERROR) {
          LOGD("fail of updateFrame() : %s, %d", node->getName(), ret);
          continue;  // loop next item in the queue
        }
        if (it->checkFrameDone() == true) {
          LOGI("Frame done : %s", node->getName());
          it->mParams.mDequeSuccess = MTRUE;
          if (it->mParams.mpfnCallback == NULL) {
            std::lock_guard<std::mutex> _l2(mDeQueueLock);
            mDeFrameQueue.push_back(*it);
            mFrameQueue.erase(it);
            mCondition.notify_one();
          } else {
            _l.unlock();
            it->mParams.mpfnCallback(&(it->mParams));
            _l.lock();
            mFrameQueue.erase(it);
          }
        }
        break;
      }
    }
  }
  return OK;
}

status_t NormalStream::validNode(int type,
                                 std::shared_ptr<V4L2StreamNode>* p_node) {
  std::shared_ptr<V4L2StreamNode>& node = *p_node;
  char* mapped_node_name = NULL;
  char* mapped_node_name2 = NULL;
  int corrected_type = type;

  if (type == NSImageio::NSIspio::EPortIndex_IMGCI ||
      type == NSImageio::NSIspio::EPortIndex_DEPI ||
      type == NSImageio::NSIspio::EPortIndex_LCEI)
    return OK;

  auto idx = mNodes.find(type);
  if (idx != mNodes.end()) {
    node = idx->second;
    LOGI("validNode port-id %d, node %p name %s", type, node.get(),
         node->getName());
  } else {
    auto search2 = Port_Mapper2.find(type);
    auto end2 = Port_Mapper2.end();
    auto search = Port_Mapper.find(type);
    auto end = Port_Mapper.end();
    if (_is_single_output(mStreamTag)) {
      if (mStreamTag != NSCam::v4l2::ENormalStreamTag_Cap_S &&
          type == NSImageio::NSIspio::EPortIndex_IMG3O)
        corrected_type = NSImageio::NSIspio::EPortIndex_WDMAO;
      else
        corrected_type = type;
      search = Port_Mapper3.find(corrected_type);
      end = Port_Mapper3.end();
    }

    if (search != end) {
      mapped_node_name = const_cast<char*>(search->second);
    } else {
      LOGE("search failed from Port_Mapper[] @ %d, port=%d", __LINE__, type);
      return -EINVAL;
    }
    if (search2 != end2) {
      mapped_node_name2 = const_cast<char*>(search2->second);
    } else {
      LOGE("search failed from Port_Mapper2[] @ %d, port=%d", __LINE__, type);
      return -EINVAL;
    }

    bool match = false;
    auto find_node = [this, type, &node](char* node_name) -> bool {
      for (auto it = mAllNodes.begin(); it != mAllNodes.end(); ++it) {
        // partially matching the suffix of name is enough,
        // since the prefix is the same
        if (strstr((*it)->getName(), node_name)) {
          LOGI("valid id %d, node %s, @line %d", type, (*it)->getName(),
               __LINE__);
          node = *it;
          mAllNodes.erase(it);
          mNodes.insert(std::make_pair(type, node));
          LOGI("validNode, %d, %s from PortMapper", type, node->getName());
          return true;
        }
      }
      return false;
    };

    match = find_node(mapped_node_name);

    if (match == false && (_is_single_output(mStreamTag) == MFALSE))
      match = find_node(mapped_node_name2);

    if (match == false) {
      LOGE("validNode, search failed @%d, type=%d", __LINE__, type);
      return -EINVAL;
    }
  }

  return OK;
}
