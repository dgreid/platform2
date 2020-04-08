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

#define LOG_TAG "MtkCam/BaseHeap"
//
#include <mtkcam/utils/imgbuf/BaseImageBuffer.h>
#include <mtkcam/utils/imgbuf/BaseImageBufferHeap.h>
#include <memory>
#include <property_service/property_lib.h>
#include <sys/time.h>
#include <vector>

using NSCam::IImageBuffer;
using NSCam::ImgBufCreator;
using NSCam::NSImageBuffer::BaseImageBuffer;
using NSCam::NSImageBufferHeap::BaseImageBufferHeap;

/******************************************************************************
 *
 ******************************************************************************/
BaseImageBufferHeap::~BaseImageBufferHeap() {
  CAM_LOGD("~BaseImageBufferHeap");
  if (0 != mLockCount) {
    MY_LOGE(
        "Not unlock before release heap - LockCount:%d, username:%s, "
        "va=%#" PRIxPTR "",
        mLockCount, mCallerName.c_str(), getBufVA(0));
  }

  if (mCreator != NULL) {
    delete mCreator;
  }
}

/******************************************************************************
 *
 ******************************************************************************/
BaseImageBufferHeap::BaseImageBufferHeap(char const* szCallerName)
    : IImageBufferHeap()
      //
      ,
      mLockCount(0),
      mLockUsage(0)
      //
      ,
      mImgSize(),
      mImgFormat(0),
      mPlaneCount(0),
      mBitstreamSize(0),
      mColorArrangement(-1)
      //
      ,
      mEnableLog(MTRUE),
      mCallerName(szCallerName),
      mCreator(NULL) {
  NSCam::Utils::LogTool::get()->getCurrentLogTime(&mCreationTimestamp);
  mCreator = new ImgBufCreator();
}

/******************************************************************************
 *
 ******************************************************************************/
void BaseImageBufferHeap::onFirstRef() {}
/******************************************************************************
 *
 ******************************************************************************/
void BaseImageBufferHeap::onLastStrongRef(const void* /*id*/) {
  std::lock_guard<std::mutex> _l(mInitMtx);
  uninitLocked();
  if (0 != mLockCount) {
    MY_LOGE(
        "Not unlock before release heap - LockCount:%d, username:%s, "
        "va=%#" PRIxPTR "",
        mLockCount, mCallerName.c_str(), getBufVA(0));
  }
}
/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::onCreate(MSize const& imgSize,
                              MINT const imgFormat,
                              size_t const bitstreamSize,
                              MBOOL const enableLog) {
  struct timeval t;
  gettimeofday(&t, NULL);
  int64_t const startTime = (t.tv_sec) * 1000000000LL + (t.tv_usec) * 1000LL;
  if (CC_UNLIKELY(!NSCam::Utils::Format::checkValidFormat(imgFormat))) {
    CAM_LOGE("Unsupported Image Format!!");
    return MFALSE;
  }
  if (CC_UNLIKELY(!imgSize)) {
    CAM_LOGE("Invalid Image Size(%dx%d)", imgSize.w, imgSize.h);
    return MFALSE;
  }
  //
  std::lock_guard<std::mutex> _l(mInitMtx);
  //
  mImgSize = imgSize;
  mImgFormat = imgFormat;
  mBitstreamSize = bitstreamSize;
  mPlaneCount = NSCam::Utils::Format::queryPlaneCount(imgFormat);
  mEnableLog = enableLog;
  //
  MBOOL ret = initLocked();
  gettimeofday(&t, NULL);
  int64_t const end_time = (t.tv_sec) * 1000000000LL + (t.tv_usec) * 1000LL;
  mCreationTimeCost = end_time - startTime;
  //
  MY_LOGD_IF(0, "[%s] this:%p %dx%d format:%#x init:%d cost(ns):%zu",
             mCallerName.c_str(), this, imgSize.w, imgSize.h, imgFormat, ret,
             mCreationTimeCost);
  return ret;
}

MBOOL
BaseImageBufferHeap::onCreate(std::shared_ptr<BaseImageBufferHeap> heap,
                              MSize const& imgSize,
                              MINT const imgFormat,
                              size_t const bitstreamSize,
                              MBOOL const enableLog) {
  MBOOL ret = onCreate(imgSize, imgFormat, bitstreamSize, enableLog);
  dummy(heap);
  //
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
size_t BaseImageBufferHeap::getPlaneBitsPerPixel(size_t index) const {
  return NSCam::Utils::Format::queryPlaneBitsPerPixel(getImgFormat(), index);
}

/******************************************************************************
 *
 ******************************************************************************/
size_t BaseImageBufferHeap::getImgBitsPerPixel() const {
  return NSCam::Utils::Format::queryImageBitsPerPixel(getImgFormat());
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::setBitstreamSize(size_t const bitstreamsize) {
  switch (getImgFormat()) {
    case eImgFmt_JPEG:
    case eImgFmt_BLOB:
      break;
    default:
      MY_LOGE("%s@ bad format:%#x", getMagicName(), getImgFormat());
      return MFALSE;
      break;
  }
  //
  if (bitstreamsize > getBufSizeInBytes(0)) {
    MY_LOGE("%s@ bitstreamSize:%zu > heap buffer size:%zu", getMagicName(),
            bitstreamsize, getBufSizeInBytes(0));
    return MFALSE;
  }
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  mBitstreamSize = bitstreamsize;
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
void BaseImageBufferHeap::setColorArrangement(MINT32 const colorArrangement) {
  std::lock_guard<std::mutex> _l(mLockMtx);
  mColorArrangement = colorArrangement;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::setImgDesc(ImageDescId id, MINT64 value, MBOOL overwrite) {
  static_assert(static_cast<size_t>(eIMAGE_DESC_ID_MAX) < 20U,
                "Too many IDs, we had better review or use a more economical "
                "data structure");

  if (static_cast<size_t>(id) >= static_cast<size_t>(eIMAGE_DESC_ID_MAX)) {
    MY_LOGE("Invalid ImageDescId: %d", id);
    return MFALSE;
  }

  std::lock_guard<std::mutex> _l(mLockMtx);

  ImageDescItem* item = &mImageDesc[static_cast<size_t>(id)];
  if (item->isValid && !overwrite) {
    return MFALSE;
  }

  item->isValid = MTRUE;
  item->value = value;

  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::getImgDesc(ImageDescId id, MINT64* value) const {
  if (static_cast<size_t>(id) >= static_cast<size_t>(eIMAGE_DESC_ID_MAX)) {
    MY_LOGE("Invalid ImageDescId: %d", id);
    return MFALSE;
  }

  std::lock_guard<std::mutex> _l(mLockMtx);

  const ImageDescItem* item = &mImageDesc[static_cast<size_t>(id)];
  if (!item->isValid) {
    return MFALSE;
  }

  *value = item->value;
  return MTRUE;
}

/******************************************************************************
 * Heap ID could be ION fd, PMEM fd, and so on.
 * Legal only after lock().
 ******************************************************************************/
MINT32
BaseImageBufferHeap::getHeapID(size_t index) const {
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  if (0 >= mLockCount) {
    MY_LOGE("This call is legal only after lock()");
    return 0;
  }
  //
  HeapInfoVect_t const& rvHeapInfo = impGetHeapInfo();
  if (index >= rvHeapInfo.size()) {
    MY_LOGE("this:%p Invalid index:%zu >= %zu", this, index, rvHeapInfo.size());
    return 0;
  }
  //
  return rvHeapInfo[index]->heapID;
}

/******************************************************************************
 * 0 <= Heap ID count <= plane count.
 * Legal only after lock().
 ******************************************************************************/
size_t BaseImageBufferHeap::getHeapIDCount() const {
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  if (0 >= mLockCount) {
    MY_LOGE("This call is legal only after lock()");
    return 0;
  }
  //
  return impGetHeapInfo().size();
}

/******************************************************************************
 * Buffer physical address; legal only after lock() with HW usage.
 ******************************************************************************/
MINTPTR
BaseImageBufferHeap::getBufPA(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  if (0 == mLockCount || 0 == (mLockUsage & eBUFFER_USAGE_HW_MASK)) {
    MY_LOGE(
        "This call is legal only after lockBuf() with HW usage - LockCount:%d "
        "Usage:%#x",
        mLockCount, mLockUsage);
    return 0;
  }
  //
  return mvBufInfo[index]->pa;
}

/******************************************************************************
 * Buffer virtual address; legal only after lock() with SW usage.
 ******************************************************************************/
MINTPTR
BaseImageBufferHeap::getBufVA(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  if (0 == mLockCount || 0 == (mLockUsage & eBUFFER_USAGE_SW_MASK)) {
    MY_LOGE(
        "This call is legal only after lockBuf() with SW usage - LockCount:%d "
        "Usage:%#x",
        mLockCount, mLockUsage);
    return 0;
  }
  //
  return mvBufInfo[index]->va;
}

/******************************************************************************
 * Buffer size in bytes; always legal.
 ******************************************************************************/
size_t BaseImageBufferHeap::getBufSizeInBytes(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  return mvBufInfo[index]->sizeInBytes;
}

/******************************************************************************
 * Buffer Strides in bytes; always legal.
 ******************************************************************************/
size_t BaseImageBufferHeap::getBufStridesInBytes(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  return mvBufInfo[index]->stridesInBytes;
}

/******************************************************************************
 * Buffer Strides in bytes; always legal.
 ******************************************************************************/
off_t BaseImageBufferHeap::getBufOffsetInBytes(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  return mvBufInfo[index]->offsetInBytes;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::lockBuf(char const* szCallerName, MINT usage) {
  std::lock_guard<std::mutex> _l(mLockMtx);
  return lockBufLocked(szCallerName, usage);
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::unlockBuf(char const* szCallerName) {
  std::lock_guard<std::mutex> _l(mLockMtx);
  return unlockBufLocked(szCallerName);
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::lockBufLocked(char const* szCallerName, MINT usage) {
  auto add_lock_info = [this, szCallerName]() {
    auto iter = mLockInfoList.emplace(mLockInfoList.begin());
    iter->mUser = szCallerName;
    iter->mTid = gettid();
    NSCam::Utils::LogTool::get()->getCurrentLogTime(&iter->mTimestamp);
  };

  if (0 < mLockCount) {
    MINT const readUsage = eBUFFER_USAGE_SW_READ_MASK |
                           eBUFFER_USAGE_HW_CAMERA_READ |
                           eBUFFER_USAGE_HW_TEXTURE;
    if (!(usage & ~readUsage) && mLockUsage == usage) {
      mLockCount++;
      add_lock_info();
      return MTRUE;
    } else {
      MY_LOGE("%s@ count:%d, usage:%#x, can't lock with usage:%#x",
              szCallerName, mLockCount, mLockUsage, usage);
      return MFALSE;
    }
  }
  //
  if (!impLockBuf(szCallerName, usage, mvBufInfo)) {
    MY_LOGE("%s@ impLockBuf() usage:%#x", szCallerName, usage);
    return MFALSE;
  }
  //
  //  Check Buffer Info.
  if (getPlaneCount() != mvBufInfo.size()) {
    MY_LOGE("%s@ BufInfo.size(%zu) != PlaneCount(%zu)", szCallerName,
            mvBufInfo.size(), getPlaneCount());
    return MFALSE;
  }
  //
  for (size_t i = 0; i < mvBufInfo.size(); i++) {
    if (0 != (usage & eBUFFER_USAGE_SW_MASK) && 0 == mvBufInfo[i]->va) {
      MY_LOGE("%s@ Bad result at %zu-th plane: va=0 with SW usage:%#x",
              szCallerName, i, usage);
      return MFALSE;
    }
  }
  //
  mLockUsage = usage;
  mLockCount++;
  add_lock_info();
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::unlockBufLocked(char const* szCallerName) {
  auto del_lock_info = [this, szCallerName]() {
    // in stack order
    auto itToDelete = mLockInfoList.begin();
    for (; itToDelete != mLockInfoList.end(); itToDelete++) {
      if (itToDelete->mUser == szCallerName) {
        // delete the 1st one whose user name & tid match.
        if (itToDelete->mTid == gettid()) {
          break;
        }

        auto it = itToDelete;
        it++;
        for (; it != mLockInfoList.end(); it++) {
          if (it->mUser == szCallerName && it->mTid == gettid()) {
            itToDelete = it;
            break;
          }
        }

        // no tid matches...
        // delete the 1st one whose user name matches.
        break;
      }
    }
    if (itToDelete != mLockInfoList.end()) {
      mLockInfoList.erase(itToDelete);
    }
  };

  if (1 < mLockCount) {
    mLockCount--;
    MY_LOGD("%s@ still locked (%d)", szCallerName, mLockCount);
    del_lock_info();
    return MTRUE;
  }
  //
  if (0 == mLockCount) {
    MY_LOGW("%s@ Never lock", szCallerName);
    return MFALSE;
  }
  //
  if (!impUnlockBuf(szCallerName, mLockUsage, mvBufInfo)) {
    MY_LOGE("%s@ impUnlockBuf() usage:%#x", szCallerName, mLockUsage);
    return MFALSE;
  }
  //
  mLockUsage = 0;
  mLockCount--;
  del_lock_info();
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::initLocked() {
  MBOOL ret = MFALSE;
  //
  mBufMap.clear();
  //
  mvBufInfo.clear();
  mvBufInfo.reserve(getPlaneCount());
  for (size_t i = 0; i < getPlaneCount(); i++) {
    std::shared_ptr<BufInfo> b = std::make_shared<BufInfo>();
    if (b == nullptr) {
      MY_LOGE("%s@ initLocked()", getMagicName());
      goto lbExit;
    }
    mvBufInfo.push_back(b);
  }
  //
  if (!impInit(mvBufInfo)) {
    MY_LOGE("%s@ impInit()", getMagicName());
    goto lbExit;
  }
  //
  for (size_t i = 0; i < mvBufInfo.size(); i++) {
    if (mvBufInfo[i]->stridesInBytes <= 0) {
      MY_LOGE("%s@ Bad result at %zu-th plane: strides:%zu", getMagicName(), i,
              mvBufInfo[i]->stridesInBytes);
      goto lbExit;
    }
  }
  //
  ret = MTRUE;
lbExit:
  if (!ret) {
    uninitLocked();
  }
  //
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::uninitLocked() {
  mvBufInfo.clear();
  //
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::syncCache(eCacheCtrl const ctrl) {
  return MFALSE;
}

MINT ImgBufCreator::generateFormat(IImageBufferHeap* heap) {
  if (mCreatorFormat != eImgFmt_UNKNOWN) {
    return mCreatorFormat;
  }
  if (heap == NULL) {
    return mCreatorFormat;
  }
  switch ((EImageFormat)heap->getImgFormat()) {
    case eImgFmt_UFO_BAYER8:
    case eImgFmt_UFO_BAYER10:
    case eImgFmt_UFO_BAYER12:
    case eImgFmt_UFO_BAYER14:
    case eImgFmt_UFO_FG_BAYER8:
    case eImgFmt_UFO_FG_BAYER10:
    case eImgFmt_UFO_FG_BAYER12:
    case eImgFmt_UFO_FG_BAYER14: {
      heap->lockBuf("ImgBufCreator", GRALLOC_USAGE_SW_READ_OFTEN);
      MINT format = *(reinterpret_cast<MINT32*>(heap->getBufVA(2)));
      heap->unlockBuf("ImgBufCreator");
      return format;
    }
    default:
      break;
  }
  return heap->getImgFormat();
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<IImageBuffer> BaseImageBufferHeap::createImageBuffer(
    ImgBufCreator* pCreator) {
  size_t bufStridesInBytes[3] = {0, 0, 0};
  MINT format = eImgFmt_UNKNOWN;

  for (size_t i = 0; i < getPlaneCount(); ++i) {
    bufStridesInBytes[i] = getBufStridesInBytes(i);
  }

  if (pCreator != NULL) {
    format = pCreator->generateFormat(this);
  } else {
    format =
        ((mCreator != NULL) ? mCreator->generateFormat(this) : getImgFormat());
  }
  auto msmImgBuffer = std::make_shared<BaseImageBuffer>(
      shared_from_this(), getImgSize(), format, getBitstreamSize(),
      bufStridesInBytes);
  //
  if (!msmImgBuffer->onCreate()) {
    CAM_LOGE("onCreate");
    return nullptr;
  }
  return msmImgBuffer;
  //
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<IImageBuffer>
BaseImageBufferHeap::createImageBuffer_FromBlobHeap(size_t offsetInBytes,
                                                    size_t sizeInBytes) {
  if (getImgFormat() != eImgFmt_BLOB && getImgFormat() != eImgFmt_RAW_OPAQUE &&
      getImgFormat() != eImgFmt_JPEG) {
    CAM_LOGE("Heap format(0x%x) is illegal.", getImgFormat());
    return NULL;
  }
  //
  MSize imgSize(sizeInBytes, getImgSize().h);
  size_t bufStridesInBytes[] = {sizeInBytes, 0, 0};
  auto psmImgBuffer = std::make_shared<BaseImageBuffer>(
      shared_from_this(), imgSize, getImgFormat(), getBitstreamSize(),
      bufStridesInBytes, offsetInBytes);
  //
  if (!psmImgBuffer) {
    CAM_LOGE("Fail to new");
    return nullptr;
  }
  //
  if (!psmImgBuffer->onCreate()) {
    CAM_LOGE("onCreate");
    return nullptr;
  }
  return psmImgBuffer;
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<IImageBuffer>
BaseImageBufferHeap::createImageBuffer_FromBlobHeap(
    size_t offsetInBytes,
    MINT32 imgFormat,
    MSize const& imgSize,
    size_t const bufStridesInBytes[3]) {
  if (getImgFormat() != eImgFmt_BLOB && getImgFormat() != eImgFmt_RAW_OPAQUE &&
      getImgFormat() != eImgFmt_JPEG) {
    CAM_LOGE("Heap format(0x%x) is illegal.", getImgFormat());
    return NULL;
  }
  //
  auto psmImgBuffer = std::make_shared<BaseImageBuffer>(
      shared_from_this(), imgSize, imgFormat, getBitstreamSize(),
      bufStridesInBytes, offsetInBytes);
  //
  if (!psmImgBuffer) {
    CAM_LOGE("Fail to new");
    return NULL;
  }
  //
  if (!psmImgBuffer->onCreate()) {
    CAM_LOGE("onCreate");
    return nullptr;
  }
  return psmImgBuffer;
  //
}

/******************************************************************************
 *
 ******************************************************************************/
std::vector<std::shared_ptr<IImageBuffer> >
BaseImageBufferHeap::createImageBuffers_FromBlobHeap(
    const ImageBufferInfo& info, const char* callerName __unused) {
  std::vector<std::shared_ptr<IImageBuffer> > vpImageBuffer;

  MINT32 bufCount = info.bufOffset.size();
  if (CC_UNLIKELY(getImgFormat() != eImgFmt_BLOB)) {
    MY_LOGE("Heap format(0x%x) is illegal.", getImgFormat());
    return vpImageBuffer;
  }

  if (CC_UNLIKELY(bufCount == 0)) {
    MY_LOGE("buffer count is Zero");
    return vpImageBuffer;
  }

  size_t bufStridesInBytes[3] = {0};
  auto& bufPlanes = info.bufPlanes;
  for (size_t i = 0; i < bufPlanes.size(); i++) {
    bufStridesInBytes[i] = bufPlanes[i].rowStrideInBytes;
  }

  vpImageBuffer.reserve(bufCount);
  for (MINT32 i = 0; i < bufCount; i++) {
    vpImageBuffer[i] = createImageBuffer_FromBlobHeap(
        info.bufOffset[i], info.imgForamt, info.imgSize, bufStridesInBytes);
    if (CC_UNLIKELY(vpImageBuffer[i] == nullptr)) {
      MY_LOGE("create ImageBuffer fail!!");
      // ensure to free vpImageBuffer since it's raw pointers
      for (size_t j = 0; j < vpImageBuffer.size(); j++) {
        std::shared_ptr<IImageBuffer> p = vpImageBuffer[j];
        p = nullptr;
      }
      vpImageBuffer.clear();
      return vpImageBuffer;
    }
  }

  return vpImageBuffer;
}
std::shared_ptr<IImageBuffer> BaseImageBufferHeap::createImageBuffer_SideBySide(
    MBOOL isRightSide) {
  size_t imgWidth = getImgSize().w >> 1;
  size_t imgHeight = getImgSize().h;
  size_t offset = (isRightSide) ? (imgWidth * getPlaneBitsPerPixel(0)) >> 3 : 0;
  MSize SBSImgSize(imgWidth, imgHeight);
  size_t bufStridesInBytes[3] = {0, 0, 0};
  for (size_t i = 0; i < getPlaneCount(); i++) {
    bufStridesInBytes[i] = (eImgFmt_BLOB == getImgFormat())
                               ? getBufStridesInBytes(i) >> 1
                               : getBufStridesInBytes(i);
  }
  //
  auto psmImgBuffer = std::make_shared<BaseImageBuffer>(
      shared_from_this(), SBSImgSize, getImgFormat(), getBitstreamSize(),
      bufStridesInBytes, offset);
  if (!psmImgBuffer) {
    CAM_LOGE("Fail to new");
    return NULL;
  }
  //
  if (!psmImgBuffer->onCreate()) {
    CAM_LOGE("onCreate");
    return NULL;
  }
  return psmImgBuffer;
  //
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::helpCheckBufStrides(
    size_t const planeIndex, size_t const planeBufStridesInBytes) const {
  if (NSCam::Utils::Format::checkValidBufferInfo(getImgFormat())) {
    size_t const planeImgWidthInPixels =
        NSCam::Utils::Format::queryPlaneWidthInPixels(
            getImgFormat(), planeIndex, getImgSize().w);
    size_t const planeBitsPerPixel = getPlaneBitsPerPixel(planeIndex);
    size_t const roundUpValue =
        ((planeBufStridesInBytes << 3) % planeBitsPerPixel > 0) ? 1 : 0;
    size_t const planeBufStridesInPixels =
        (planeBufStridesInBytes << 3) / planeBitsPerPixel + roundUpValue;
    //
    if (planeBufStridesInPixels < planeImgWidthInPixels) {
      MY_LOGE(
          "[%dx%d image @ %zu-th plane] Bad width stride in pixels: given "
          "buffer stride:%zu < image stride:%zu. stride in bytes(%zu) bpp(%zu)",
          getImgSize().w, getImgSize().h, planeIndex, planeBufStridesInPixels,
          planeImgWidthInPixels, planeBufStridesInBytes, planeBitsPerPixel);
      return MFALSE;
    }
  }
  //
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
size_t BaseImageBufferHeap::helpQueryBufSizeInBytes(
    size_t const planeIndex, size_t const planeStridesInBytes) const {
  MY_LOGD_IF(planeIndex >= getPlaneCount(), "Bad index:%zu >= PlaneCount:%zu",
             planeIndex, getPlaneCount());
  //
  size_t const planeImgHeight = NSCam::Utils::Format::queryPlaneHeightInPixels(
      getImgFormat(), planeIndex, getImgSize().h);
  return planeStridesInBytes * planeImgHeight;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBufferHeap::updateImgInfo(MSize const& imgSize,
                                   MINT const imgFormat,
                                   size_t const sizeInBytes[3],
                                   size_t const rowStrideInBytes[3],
                                   size_t const bufPlaneSize) {
  if (eImgFmt_JPEG == imgFormat) {
    CAM_LOGE("Cannnot create JPEG format heap");
    return MFALSE;
  }
  if (!NSCam::Utils::Format::checkValidFormat(imgFormat)) {
    CAM_LOGE("Unsupported Image Format!!");
    return MFALSE;
  }
  if (!imgSize) {
    CAM_LOGE("Unvalid Image Size(%dx%d)", imgSize.w, imgSize.h);
    return MFALSE;
  }
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  mImgSize = imgSize;
  mImgFormat = imgFormat;
  mPlaneCount = NSCam::Utils::Format::queryPlaneCount(imgFormat);
  //
  MY_LOGD("[%s] this:%p %dx%d format:%#x planes:%zu", mCallerName.c_str(), this,
          imgSize.w, imgSize.h, imgFormat, mPlaneCount);

  mBufMap.clear();
  mvBufInfo.clear();
  mvBufInfo.reserve(getPlaneCount());
  for (size_t i = 0; i < getPlaneCount(); i++) {
    if (i >= bufPlaneSize) {
      MY_LOGE("bufInfo[%zu] over the bufPlaneSize:%zu", i, bufPlaneSize);
      break;
    }
    std::shared_ptr<BufInfo> bufInfo = std::make_shared<BufInfo>();
    bufInfo->stridesInBytes = rowStrideInBytes[i];
    bufInfo->sizeInBytes = sizeInBytes[i];
    mvBufInfo.push_back(bufInfo);
    MY_LOGD("stride:%zu, sizeInBytes:%zu", bufInfo->stridesInBytes,
            bufInfo->sizeInBytes);
  }
  //
  if (!impReconfig(mvBufInfo)) {
    MY_LOGE("%s@ impReconfig()", getMagicName());
  }
  //
  for (size_t i = 0; i < mvBufInfo.size(); i++) {
    if (mvBufInfo[i]->stridesInBytes <= 0) {
      MY_LOGE("%s@ Bad result at %zu-th plane: strides:%zu", getMagicName(), i,
              mvBufInfo[i]->stridesInBytes);
    }
  }
  return MTRUE;
}

void BaseImageBufferHeap::dummy(std::shared_ptr<BaseImageBufferHeap> p) {
  static std::shared_ptr<BaseImageBufferHeap> ptr = p;
}
