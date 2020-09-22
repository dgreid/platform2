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

#include "camera/hal/mediatek/mtkcam/pipeline/hwnode/p2/P2_Request.h"

namespace P2 {

#include <mtkcam/utils/debug/P2_DebugControl.h>
#define P2_CLASS_TAG P2FrameHolder
#define P2_TRACE TRACE_P2_FRAME_HOLDER
#include "P2_LogHeader.h"

#include <memory>
#include <utility>

P2FrameHolder::P2FrameHolder(const std::shared_ptr<IP2Frame>& frame)
    : mFrame(frame) {}

P2FrameHolder::~P2FrameHolder() {}

MVOID P2FrameHolder::beginBatchRelease() {
  if (mFrame != nullptr) {
    mFrame->beginBatchRelease();
  }
}

MVOID P2FrameHolder::endBatchRelease() {
  if (mFrame != nullptr) {
    mFrame->endBatchRelease();
  }
}

MVOID P2FrameHolder::notifyNextCapture() {
  if (mFrame != nullptr) {
    mFrame->notifyNextCapture();
  }
}

std::shared_ptr<IP2Frame> P2FrameHolder::getIP2Frame() const {
  return mFrame;
}

#undef P2_CLASS_TAG
#undef P2_TRACE
#define P2_CLASS_TAG P2Request
#define P2_TRACE TRACE_P2_REQUEST
#include "P2_LogHeader.h"

P2Request::P2Request(const ILog& log,
                     const std::shared_ptr<IP2Frame>& frameHolder,
                     const P2Pack& p2Pack,
                     const std::shared_ptr<P2InIDMap>& p2IdMap)
    : P2FrameHolder(frameHolder),
      mLog(log),
      mP2Pack(p2Pack),
      mInIDMap(p2IdMap) {
  mSensorID = p2Pack.getConfigInfo().mMainSensorID;
}

P2Request::P2Request(const std::shared_ptr<P2Request>& request)
    : P2FrameHolder(request != nullptr ? request->getIP2Frame() : nullptr),
      mLog(request != nullptr ? request->mLog : ILog()),
      mP2Pack(request != nullptr ? request->mP2Pack : P2Pack()) {
  if (!request) {
    return;
  }
  mDumpType = request->mDumpType;
  mInIDMap = request->mInIDMap;
  mSensorID = request->mSensorID;
}

P2Request::P2Request(const std::shared_ptr<P2Request>& request,
                     MUINT32 sensorID)
    : P2FrameHolder(request != nullptr ? request->getIP2Frame() : nullptr),
      mLog(makeSubSensorLogger(spToILog(request), sensorID)),
      mP2Pack(request != nullptr ? request->mP2Pack.getP2Pack(mLog, sensorID)
                                 : P2Pack()) {
  mSensorID = sensorID;

  if (!request) {
    return;
  }
  mDumpType = request->mDumpType;
  mInIDMap = request->mInIDMap;

  ID_META inMeta[] = {IN_APP, IN_P1_APP, IN_P1_HAL};
  ID_IMG inImg[] = {IN_FULL, IN_RESIZED, IN_LCSO, IN_RSSO};
  for (ID_META meta : inMeta) {
    mMeta[meta] = request->mMeta[mInIDMap->getMetaID(sensorID, meta)];
  }

  for (ID_IMG img : inImg) {
    mImg[img] = request->mImg[mInIDMap->getImgID(sensorID, img)];
  }

  if (sensorID == mP2Pack.getFrameData().mMasterSensorID) {
    mMeta[OUT_APP] = std::move(request->mMeta[OUT_APP]);
    mMeta[OUT_HAL] = std::move(request->mMeta[OUT_HAL]);
    mImg[OUT_FD] = std::move(request->mImg[OUT_FD]);
    mImg[OUT_JPEG_YUV] = std::move(request->mImg[OUT_JPEG_YUV]);
    mImg[OUT_THN_YUV] = std::move(request->mImg[OUT_THN_YUV]);
    mImg[OUT_POSTVIEW] = std::move(request->mImg[OUT_POSTVIEW]);
    mImgOutArray = std::move(request->mImgOutArray);
  }
}

P2Request::~P2Request() {}

MVOID P2Request::updateSensorID() {
  for (auto sensorId : mP2Pack.getConfigInfo().mAllSensorID) {
    if (isValid(mImg[mInIDMap->getImgID(sensorId, IN_RESIZED)]) ||
        isValid(mImg[mInIDMap->getImgID(sensorId, IN_FULL)])) {
      mSensorID = sensorId;
      break;
    }
  }
}

MVOID P2Request::initIOInfo() {
  mIsResized = mImg.count(IN_RESIZED);
  mIsReprocess = mImg.count(IN_REPROCESS);
  for (const auto& it : mImgOutArray) {
    if (it->isPhysicalStream()) {
      updateSensorID();
      mIsPhysic = MTRUE;
      break;
    }
  }
}

MUINT32 P2Request::getSensorID() const {
  return mSensorID;
}

std::shared_ptr<Cropper> P2Request::getCropper() const {
  return mP2Pack.getSensorData().mCropper;
}

std::shared_ptr<Cropper> P2Request::getCropper(MUINT32 sensorID) const {
  return mP2Pack.getSensorData(sensorID).mCropper;
}

MBOOL P2Request::hasInput() const {
  MBOOL ret = MFALSE;
  ret = isValidImg(IN_FULL) || isValidImg(IN_RESIZED) ||
        isValidImg(IN_FULL_2) || isValidImg(IN_RESIZED_2);
  return ret;
}

MBOOL P2Request::hasOutput() const {
  MBOOL ret = MFALSE;
  for (const auto& it : mImgOutArray) {
    if (isValid(it)) {
      ret = MTRUE;
      break;
    }
  }
  ret = ret || isValidImg(OUT_FD) || isValidImg(OUT_JPEG_YUV) ||
        isValidImg(OUT_THN_YUV) || isValidImg(OUT_POSTVIEW);
  return ret;
}

MBOOL P2Request::isResized() const {
  return mIsResized;
}

MBOOL P2Request::isReprocess() const {
  return mIsReprocess;
}

MBOOL P2Request::isPhysic() const {
  return mIsPhysic;
}

MBOOL P2Request::isLarge() const {
  return MFALSE;
}

MVOID P2Request::releaseResource(MUINT32 res) {
  TRACE_S_FUNC_ENTER(mLog, "res=0x%x", res);
  P2_CAM_TRACE_NAME(TRACE_ADVANCED, "P2Request::releaseResource");
  if (res & RES_META) {
    mMeta.clear();
  }
  if (res & RES_IMG) {
    mImg.clear();
    mImgOutArray.clear();
  }
  if ((res & RES_IN_IMG) && !mImg.empty()) {
    for (auto&& info : P2Img::InfoMap) {
      if (info.second.dir & IO_DIR_IN) {
        mImg[info.second.id] = nullptr;
      }
    }
  }
  if ((res & RES_OUT_IMG) && !mImg.empty()) {
    for (auto&& info : P2Img::InfoMap) {
      if (info.second.dir & IO_DIR_OUT) {
        mImg[info.second.id] = nullptr;
      }
    }
    mImgOutArray.clear();
  }
  if ((res & RES_IN_META) && !mMeta.empty()) {
    for (auto&& info : P2Meta::InfoMap) {
      if (info.second.dir & IO_DIR_IN) {
        mMeta[info.second.id] = nullptr;
      }
    }
  }
  if ((res & RES_OUT_META) && !mMeta.empty()) {
    for (auto&& info : P2Meta::InfoMap) {
      if (info.second.dir & IO_DIR_OUT) {
        mMeta[info.second.id] = nullptr;
      }
    }
  }
  TRACE_S_FUNC_EXIT(mLog);
}

MVOID P2Request::releaseImg(ID_IMG id) {
  TRACE_S_FUNC_ENTER(mLog);
  if (id == OUT_YUV) {
    mImgOutArray.clear();
  } else {
    auto it = mImg.find(id);
    if (it != mImg.end()) {
      mImg.erase(it);
    }
  }
  TRACE_S_FUNC_EXIT(mLog);
}

MVOID P2Request::releaseMeta(ID_META id) {
  TRACE_S_FUNC_ENTER(mLog);
  auto it = mMeta.find(id);
  if (it != mMeta.end()) {
    mMeta.erase(it);
  }
  TRACE_S_FUNC_EXIT(mLog);
}

P2MetaSet P2Request::getMetaSet() const {
  P2MetaSet set;
  std::shared_ptr<P2Meta> inApp = getMeta(IN_APP);
  std::shared_ptr<P2Meta> inHal = getMeta(IN_P1_HAL);
  if (isValid(inApp)) {
    IMetadata* meta = inApp->getIMetadataPtr();
    set.mInApp = (*meta);
  }
  if (isValid(inHal)) {
    IMetadata* meta = inHal->getIMetadataPtr();
    set.mInHal = (*meta);
  }
  if (isValidMeta(OUT_APP) || isValidMeta(OUT_HAL)) {
    set.mHasOutput = MTRUE;
  }
  return set;
}

MVOID P2Request::updateMetaSet(const P2MetaSet& set) {
  TRACE_S_FUNC_ENTER(mLog);
  if (set.mHasOutput) {
    if (isValidMeta(OUT_APP)) {
      IMetadata* meta = mMeta[OUT_APP]->getIMetadataPtr();
      (*meta) = set.mOutApp;
      this->mMeta[OUT_APP]->updateResult(MTRUE);
    }
    if (isValidMeta(OUT_HAL)) {
      IMetadata* meta = mMeta[OUT_HAL]->getIMetadataPtr();
      (*meta) = set.mInHal;
      (*meta) += set.mOutHal;
      this->mMeta[OUT_HAL]->updateResult(MTRUE);
    }
  }
  TRACE_S_FUNC_EXIT(mLog);
}

MVOID P2Request::updateResult(MBOOL result) {
  TRACE_S_FUNC_ENTER(mLog);
  for (const auto& it : mImgOutArray) {
    if (isValid(it)) {
      it->updateResult(result);
    }
  }
  TRACE_S_FUNC_EXIT(mLog);
}

MVOID P2Request::updateMetaResult(MBOOL result) {
  TRACE_S_FUNC_ENTER(mLog);

  if (isValidMeta(OUT_APP)) {
    this->mMeta[OUT_APP]->updateResult(result);
  }
  if (isValidMeta(OUT_HAL)) {
    this->mMeta[OUT_HAL]->updateResult(result);
  }

  TRACE_S_FUNC_EXIT(mLog);
}

MVOID P2Request::dump() const {
  TRACE_S_FUNC_ENTER(mLog);
  for (auto&& info : P2Meta::InfoMap) {
    std::shared_ptr<P2Meta> meta = getMeta(info.second.id);
    MY_S_LOGD(mLog, "Meta %s=%p", info.second.name.c_str(), meta.get());
  }
  for (auto&& info : P2Img::InfoMap) {
    std::shared_ptr<P2Img> img = getImg(info.second.id);
    MSize size = (img == nullptr) ? MSize(0, 0) : img->getImgSize();
    MY_S_LOGD(mLog, "Img %s=%p, size(%dx%d)", info.second.name.c_str(),
              img.get(), size.w, size.h);
  }
  MY_S_LOGD(mLog, "mImgOutArray.size() = %zu", mImgOutArray.size());
  size_t n = mImgOutArray.size();
  for (size_t i = 0; i < n; i++) {
    MY_S_LOGD(mLog, "ImgOut[%zu/%zu] size(%dx%d)", i, n,
              mImgOutArray[i]->getImgSize().w, mImgOutArray[i]->getImgSize().h);
  }
  TRACE_S_FUNC_EXIT(mLog);
}

std::shared_ptr<P2Meta> P2Request::getMeta(ID_META id) const {
  TRACE_S_FUNC_ENTER(mLog);
  auto it = mMeta.find(id);
  std::shared_ptr<P2Meta> meta = (it != mMeta.end()) ? it->second : nullptr;
  TRACE_S_FUNC_EXIT(mLog);
  return meta;
}

IMetadata* P2Request::getMetaPtr(ID_META id) const {
  TRACE_S_FUNC_ENTER(mLog);
  std::shared_ptr<P2Meta> spMeta = this->getMeta(id);
  TRACE_S_FUNC_EXIT(mLog);
  return ((spMeta != nullptr) ? spMeta->getIMetadataPtr() : nullptr);
}

std::shared_ptr<P2Meta> P2Request::getMeta(ID_META id, MUINT32 sensorID) const {
  TRACE_S_FUNC_ENTER(mLog);
  auto it = mMeta.find(mInIDMap->getMetaID(sensorID, id));
  std::shared_ptr<P2Meta> meta = (it != mMeta.end()) ? it->second : nullptr;
  TRACE_S_FUNC_EXIT(mLog);
  return meta;
}

IMetadata* P2Request::getMetaPtr(ID_META id, MUINT32 sensorID) const {
  TRACE_S_FUNC_ENTER(mLog);

  std::shared_ptr<P2Meta> spMeta = this->getMeta(id, sensorID);
  if (spMeta == nullptr) {
    return nullptr;
  }

  TRACE_S_FUNC_EXIT(mLog);
  return spMeta->getIMetadataPtr();
}

std::shared_ptr<P2Img> P2Request::getImg(ID_IMG id) const {
  TRACE_S_FUNC_ENTER(mLog);
  auto it = mImg.find(id);
  std::shared_ptr<P2Img> img = (it != mImg.end()) ? it->second : nullptr;
  TRACE_S_FUNC_EXIT(mLog);
  return img;
}

MBOOL P2Request::isValidMeta(ID_META id) const {
  TRACE_S_FUNC_ENTER(mLog);
  TRACE_S_FUNC_EXIT(mLog);
  return isValid(getMeta(id));
}

MBOOL P2Request::isValidImg(ID_IMG id) const {
  TRACE_S_FUNC_ENTER(mLog);
  TRACE_S_FUNC_EXIT(mLog);
  return isValid(getImg(id));
}

std::shared_ptr<P2InIDMap> P2Request::getIDMap() const {
  return mInIDMap;
}

#undef P2_CLASS_TAG
#undef P2_TRACE
#define P2_CLASS_TAG P2FrameRequest
#define P2_TRACE TRACE_P2_FRAME_REQUEST
#include "P2_LogHeader.h"

P2FrameRequest::P2FrameRequest(const ILog& log,
                               const P2Pack& pack,
                               const std::shared_ptr<P2InIDMap>& p2IdMap)
    : mLog(log), mP2Pack(pack), mInIDMap(p2IdMap) {}

P2FrameRequest::~P2FrameRequest() {}

MUINT32 P2FrameRequest::getFrameID() const {
  return mP2Pack.getFrameData().mP2FrameNo;
}

MVOID P2FrameRequest::registerImgPlugin(
    const std::shared_ptr<P2ImgPlugin>& plugin, MBOOL needSWRW) {
  mImgPlugin.push_back(plugin);
  mNeedImageSWRW |= needSWRW;
}

ID_META P2FrameRequest::mapID(MUINT32 sensorID, ID_META metaId) {
  return mInIDMap->getMetaID(sensorID, metaId);
}

ID_IMG P2FrameRequest::mapID(MUINT32 sensorID, ID_IMG imgId) {
  return mInIDMap->getImgID(sensorID, imgId);
}

}  // namespace P2
