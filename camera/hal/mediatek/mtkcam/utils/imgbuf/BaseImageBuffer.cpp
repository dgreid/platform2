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

#define LOG_TAG "MtkCam/ImgBuf"
//
#include <cutils/compiler.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <memory>
#include <mtkcam/utils/imgbuf/BaseImageBuffer.h>
#include <property_service/property_lib.h>
#include <string>
#include <sys/stat.h>

using NSCam::NSImageBuffer::BaseImageBuffer;
/******************************************************************************
 *
 ******************************************************************************/
#define BYTE2PIXEL(BPP, BYTE) ((BYTE << 3) / BPP)
#define PIXEL2BYTE(BPP, PIXEL) ((PIXEL * BPP) >> 3)
MUINT32 BaseImageBuffer::mfgFileCacheEn = 0;

/******************************************************************************
 *
 ******************************************************************************/
BaseImageBuffer::~BaseImageBuffer() {
  MY_LOGD("deconstruction");
  mvImgBufInfo.clear();
  mvBufHeapInfo.clear();
  //
  if (0 != mLockCount) {
    MY_LOGE("Not unlock before release heap - LockCount:%d", mLockCount);
  }
  //
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::onCreate() {
  mfgFileCacheEn = property_get_int32("vendor.debug.camera.imgBuf.enFC", 0);

  MY_LOGI(
      "%s@ this:%p %dx%d, offset(%zu) fmt(0x%x), plane(%zu),mspImgBufHeap:%p, "
      "%ld",
      getMagicName(), this, getImgSize().w, getImgSize().h, mOffset,
      getImgFormat(), getPlaneCount(), mspImgBufHeap.get(),
      mspImgBufHeap.use_count());
  //
  mvBufHeapInfo.clear();
  mvBufHeapInfo.reserve(mspImgBufHeap->getPlaneCount());
  std::string str;
  for (size_t i = 0; i < mspImgBufHeap->getPlaneCount(); i++) {
    mvBufHeapInfo.push_back(std::make_shared<BaseImageBufferHeap::BufInfo>());
    mvBufHeapInfo[i]->stridesInBytes = mspImgBufHeap->getBufStridesInBytes(i);
    mvBufHeapInfo[i]->sizeInBytes = mspImgBufHeap->getBufSizeInBytes(i);
  }
  //
  mvImgBufInfo.clear();
  mvImgBufInfo.reserve(getPlaneCount());
  for (size_t i = 0; i < getPlaneCount(); ++i) {
    mvImgBufInfo.push_back(std::make_shared<ImgBufInfo>());
  }
  //
  size_t imgBufSize = 0;  // buffer size of n planes.
  //
  for (size_t i = 0; i < mvImgBufInfo.size(); ++i) {
    bool bCheckValid =
        NSCam::Utils::Format::checkValidBufferInfo(getImgFormat());
    // (plane) strides in bytes
    mvImgBufInfo[i]->stridesInBytes = mStrides[i];
    //
    // (plane) offset in bytes
    size_t const planeOffsetInPixels =
        NSCam::Utils::Format::queryPlaneWidthInPixels(
            getImgFormat(), i, BYTE2PIXEL(getPlaneBitsPerPixel(i), mOffset));
    mvImgBufInfo[i]->offsetInBytes = PIXEL2BYTE(
        getPlaneBitsPerPixel(i), planeOffsetInPixels);  // size in bytes.

    if (bCheckValid) {
      MY_LOGW_IF(mOffset != mvImgBufInfo[i]->offsetInBytes,
                 "%s@ Bad offset at %zu-th plane: mOffset(%zu) -> (%f)(pixels) "
                 "-> offsetInBytes(%zu)",
                 getMagicName(), i, mOffset,
                 (MFLOAT)mOffset * 8 / getPlaneBitsPerPixel(i),
                 mvImgBufInfo[i]->offsetInBytes);
    }
    //
    // (plane) size in bytes
    size_t const imgWidthInPixels =
        NSCam::Utils::Format::queryPlaneWidthInPixels(getImgFormat(), i,
                                                      (size_t)getImgSize().w);
    size_t const imgHeightInPixels =
        NSCam::Utils::Format::queryPlaneHeightInPixels(getImgFormat(), i,
                                                       (size_t)getImgSize().h);
    size_t const planeBitsPerPixel = getPlaneBitsPerPixel(i);
    size_t const roundUpValue =
        (imgWidthInPixels * planeBitsPerPixel % 8 > 0) ? 1 : 0;
    size_t const imgWidthInBytes =
        (imgWidthInPixels * planeBitsPerPixel / 8) + roundUpValue;
    //
    if (bCheckValid) {
      if (mvImgBufInfo[i]->stridesInBytes <= 0 ||
          mvImgBufInfo[i]->stridesInBytes < imgWidthInBytes) {
        MY_LOGE(
            "%s@ Bad result at %zu-th plane: bpp(%zu), width(%zu pixels/%zu "
            "bytes), strides(%zu bytes)",
            getMagicName(), i, planeBitsPerPixel, imgWidthInPixels,
            imgWidthInBytes, mvImgBufInfo[i]->stridesInBytes);
        return MFALSE;
      }
    }
    switch (getImgFormat()) {
        // [NOTE] create JPEG image buffer from BLOB heap.
      case eImgFmt_JPEG:
      case eImgFmt_BLOB:
        mvImgBufInfo[i]->sizeInBytes = mvImgBufInfo[i]->stridesInBytes;
        break;
      default:
        if (mspImgBufHeap->getBufCustomSizeInBytes(i) != 0) {
          mvImgBufInfo[i]->sizeInBytes =
              (0 == mvImgBufInfo[i]->offsetInBytes)
                  ? mspImgBufHeap->getBufSizeInBytes(i)
                  : mvImgBufInfo[i]->stridesInBytes * (imgHeightInPixels - 1) +
                        imgWidthInBytes;
          if (mvImgBufInfo[i]->stridesInBytes * imgHeightInPixels !=
                  mspImgBufHeap->getBufSizeInBytes(i) ||
              0 != mvImgBufInfo[i]->offsetInBytes) {
            MY_LOGI(
                "special case, fmt(%d), plane(%d), s(%d), w(%d), h(%d), "
                "offset(%d), s*h(%d), heap size(%d),",
                getImgFormat(), i, mvImgBufInfo[i]->stridesInBytes,
                imgWidthInBytes, imgHeightInPixels,
                mvImgBufInfo[i]->offsetInBytes,
                mvImgBufInfo[i]->stridesInBytes * imgHeightInPixels,
                mspImgBufHeap->getBufSizeInBytes(i));
          }
        } else {
          mvImgBufInfo[i]->sizeInBytes =
              (0 == mvImgBufInfo[i]->offsetInBytes)
                  ? mvImgBufInfo[i]->stridesInBytes * imgHeightInPixels
                  : mvImgBufInfo[i]->stridesInBytes * (imgHeightInPixels - 1) +
                        imgWidthInBytes;
        }
        break;
    }
    imgBufSize += mvImgBufInfo[i]->sizeInBytes;
    //
    if (eImgFmt_BLOB != mspImgBufHeap->getImgFormat()) {
      // check  ROI(x,y) + ROI(w,h) <= heap stride(w,h)
      if (bCheckValid) {
        size_t const planeStartXInBytes =
            mvImgBufInfo[i]->offsetInBytes %
            mspImgBufHeap->getBufStridesInBytes(i);
        size_t const planeStartYInBytes =
            mvImgBufInfo[i]->offsetInBytes /
            mspImgBufHeap->getBufStridesInBytes(i);
        size_t const planeStartXInPixels =
            BYTE2PIXEL(getPlaneBitsPerPixel(i), planeStartXInBytes);
        size_t const planeStartYInPixels =
            BYTE2PIXEL(getPlaneBitsPerPixel(i), planeStartYInBytes);
        size_t const planeStridesInPixels = BYTE2PIXEL(
            getPlaneBitsPerPixel(i), mspImgBufHeap->getBufStridesInBytes(i));
        size_t const planeHeightInPixels =
            NSCam::Utils::Format::queryPlaneHeightInPixels(getImgFormat(), i,
                                                           getImgSize().h);
        MRect roi(MPoint(planeStartXInPixels, planeStartYInPixels),
                  MSize(imgWidthInPixels, imgHeightInPixels));
        if ((size_t)roi.leftTop().x + (size_t)roi.width() >
                planeStridesInPixels ||
            (size_t)roi.leftTop().y + (size_t)roi.height() >
                planeHeightInPixels) {
          MY_LOGE(
              "%s@ Bad image buffer at %zu-th plane: strides:%zux%zu(pixels), "
              "roi:(%d,%d,%d,%d)",
              getMagicName(), i, planeStridesInPixels, planeHeightInPixels,
              roi.leftTop().x, roi.leftTop().y, roi.width(), roi.height());
          return MFALSE;
        }
        if (getBufOffsetInBytes(i) + getBufSizeInBytes(i) >
            mspImgBufHeap->getBufSizeInBytes(i)) {
          MY_LOGE(
              "%s@ Bad image buffer at %zu-th plane: offset(%zu) + "
              "bufSize(%zu) > heap bufSize(%zu)",
              getMagicName(), i, getBufOffsetInBytes(i), getBufSizeInBytes(i),
              mspImgBufHeap->getBufSizeInBytes(i));
          return MFALSE;
        }
      }
    } else if (eImgFmt_BLOB == getImgFormat() ||
               eImgFmt_JPEG == getImgFormat()) {
      // check BLOB buffer size <= BLOB heap size
      if (getBufOffsetInBytes(i) + getBufSizeInBytes(i) >
          mspImgBufHeap->getBufSizeInBytes(i)) {
        MY_LOGE(
            "%s@ blob buffer offset(%zu)(bytes) + size(%zu) > blob heap buffer "
            "size(%zu)",
            getMagicName(), getBufOffsetInBytes(i), getBufSizeInBytes(i),
            mspImgBufHeap->getBufSizeInBytes(i));
        return MFALSE;
      }
    }
  }
  //
  if (eImgFmt_BLOB == mspImgBufHeap->getImgFormat() &&
      eImgFmt_BLOB != getImgFormat()) {
    // create non-BLOB image buffer from BLOB heap.
    if (imgBufSize > mspImgBufHeap->getBufSizeInBytes(0)) {
      for (size_t i = 0; i < getPlaneCount(); i++) {
        MY_LOGE("plane(%zu) bit(%zu), buf stride(%zu), bufSize(%zu)", i,
                getPlaneBitsPerPixel(i), getBufStridesInBytes(i),
                getBufSizeInBytes(i));
      }
      MY_LOGE("%s@ buffer size(%zu) > blob heap buffer size(%zu)",
              getMagicName(), imgBufSize, mspImgBufHeap->getBufSizeInBytes(0));
      return MFALSE;
    }
  }
  //
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
void BaseImageBuffer::setColorArrangement(MINT32 const colorArrangement) {
  std::lock_guard<std::mutex> _l(mLockMtx);
  mColorArrangement = colorArrangement;
  mspImgBufHeap->setColorArrangement(colorArrangement);
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::setImgDesc(ImageDescId id, MINT64 value, MBOOL overwrite) {
  return mspImgBufHeap->setImgDesc(id, value, overwrite);
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::getImgDesc(ImageDescId id, MINT64* value) const {
  return mspImgBufHeap->getImgDesc(id, value);
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::setBitstreamSize(size_t const bitstreamsize) {
  if (eImgFmt_JPEG != getImgFormat()) {
    MY_LOGE("%s@ wrong format(0x%x), can not set bitstream size",
            getMagicName(), getImgFormat());
    return MFALSE;
  }
  if (bitstreamsize > mspImgBufHeap->getBufSizeInBytes(0)) {
    MY_LOGE("%s@ bitstream size(%zu) > heap buffer size(%zu)", getMagicName(),
            bitstreamsize, mspImgBufHeap->getBufSizeInBytes(0));
    return MFALSE;
  }
  //
  mBitstreamSize = bitstreamsize;
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
size_t BaseImageBuffer::getPlaneBitsPerPixel(size_t index) const {
  return NSCam::Utils::Format::queryPlaneBitsPerPixel(getImgFormat(), index);
}

/******************************************************************************
 *
 ******************************************************************************/
size_t BaseImageBuffer::getImgBitsPerPixel() const {
  return NSCam::Utils::Format::queryImageBitsPerPixel(getImgFormat());
}

/******************************************************************************
 *
 ******************************************************************************/
size_t BaseImageBuffer::getBufOffsetInBytes(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  return mvImgBufInfo[index]->offsetInBytes;
}

/******************************************************************************
 * Buffer physical address; legal only after lock() with HW usage.
 ******************************************************************************/
MINTPTR
BaseImageBuffer::getBufPA(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  MUINT32 offset = getBufOffsetInBytes(index);
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  if (0 == mLockCount) {
    MY_LOGE(
        "This call is legal only after lockBuf() with HW usage - LockCount:%d "
        "Usage:%#x",
        mLockCount, mLockUsage);
    return 0;
  }
  //
  // Buf PA(i) = Heap PA(i) + Buf Offset(i)
  return mvImgBufInfo[index]->pa + offset;
}

/******************************************************************************
 * Buffer virtual address; legal only after lock() with SW usage.
 ******************************************************************************/
MINTPTR
BaseImageBuffer::getBufVA(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  size_t offset = getBufOffsetInBytes(index);
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  if (0 == mLockCount) {
    MY_LOGE(
        "This call is legal only after lockBuf() with SW usage - LockCount:%d "
        "Usage:%#x",
        mLockCount, mLockUsage);
    return 0;
  }
  //
  // Buf VA(i) = Heap VA(i) + Buf Offset(i)
  return mvImgBufInfo[index]->va + offset;
}

/******************************************************************************
 * Buffer size in bytes; always legal.
 ******************************************************************************/
size_t BaseImageBuffer::getBufSizeInBytes(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  return mvImgBufInfo[index]->sizeInBytes;
}

/******************************************************************************
 * Buffer Strides in bytes; always legal.
 ******************************************************************************/
size_t BaseImageBuffer::getBufStridesInBytes(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  return mvImgBufInfo[index]->stridesInBytes;
}

/******************************************************************************
 * Buffer Strides in pixel; always legal.
 ******************************************************************************/
size_t BaseImageBuffer::getBufStridesInPixel(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  const auto bpp = getPlaneBitsPerPixel(index);
  if (CC_UNLIKELY(bpp == 0)) {
    MY_LOGE("Bad BitsPerPixel value(0) at plane %zu", index);
    return 0;
  }
  return (mvImgBufInfo[index]->stridesInBytes * 8) / bpp;  // unit of bbp is bit
}

/******************************************************************************
 * Buffer Scanlines; always legal.
 ******************************************************************************/
size_t BaseImageBuffer::getBufScanlines(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  if (CC_UNLIKELY(mvImgBufInfo[index]->stridesInBytes == 0)) {
    MY_LOGE("Bad stridesInBytes value (0), index=%zu", index);
    return 0;
  }

  return mvImgBufInfo[index]->sizeInBytes / mvImgBufInfo[index]->stridesInBytes;
}
/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::setExtParam(MSize const& imgSize, size_t offsetInBytes) {
  MBOOL ret = MFALSE;
  size_t const sizeInBytes = getBufSizeInBytes(0) + getBufOffsetInBytes(0);
  size_t const bitsPerPixel = getPlaneBitsPerPixel(0);
  size_t const strideInBytes = getBufStridesInBytes(0);
  size_t const strideInPixels = BYTE2PIXEL(bitsPerPixel, strideInBytes);
  size_t const roundUpValue = (imgSize.w * bitsPerPixel % 8 > 0) ? 1 : 0;
  size_t const imgWidthInBytes = (imgSize.w * bitsPerPixel / 8) + roundUpValue;
  size_t const imgSizeInBytes =
      (0 == offsetInBytes) ? strideInBytes * imgSize.h
                           : strideInBytes * (imgSize.h - 1) + imgWidthInBytes;

  if ((size_t)imgSize.w > strideInPixels || imgSize.h > mBufHeight) {
    MY_LOGE("invalid image size(%dx%d)>(%zux%d), strideInBytes(%zu)", imgSize.w,
            imgSize.h, strideInPixels, mBufHeight, strideInBytes);
    goto lbExit;
  }
  if (imgSizeInBytes + offsetInBytes > sizeInBytes) {
    MY_LOGE("oversize S(%dx%d):(%zu) + Offset(%zu) > original size(%zu)",
            imgSize.w, imgSize.h, imgSizeInBytes, offsetInBytes, sizeInBytes);
    goto lbExit;
  }
  //
  //
  {
    std::lock_guard<std::mutex> _l(mLockMtx);
    //
    if (mImgSize != imgSize ||
        mvImgBufInfo[0]->extOffsetInBytes != offsetInBytes) {
      MY_LOGD("update imgSize(%dx%d -> %dx%d), offset(%zu->%zu) @0-plane",
              mImgSize.w, mImgSize.h, imgSize.w, imgSize.h,
              mvImgBufInfo[0]->extOffsetInBytes, offsetInBytes);
      //
      mImgSize = imgSize;
      for (size_t i = 0; i < getPlaneCount(); i++) {
        mvImgBufInfo[i]->extOffsetInBytes =
            NSCam::Utils::Format::queryPlaneWidthInPixels(
                getImgFormat(), i,
                BYTE2PIXEL(getPlaneBitsPerPixel(i), offsetInBytes));
      }
    }
  }
  ret = MTRUE;
lbExit:
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
size_t BaseImageBuffer::getExtOffsetInBytes(size_t index) const {
  if (index >= getPlaneCount()) {
    MY_LOGE("Bad index(%zu) >= PlaneCount(%zu)", index, getPlaneCount());
    return 0;
  }
  //
  //
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  return mvImgBufInfo[index]->extOffsetInBytes;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::lockBuf(char const* szCallerName, MINT usage) {
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  MBOOL ret = lockBufLocked(szCallerName, usage);
  //
  if (getPlaneCount() == mspImgBufHeap->getPlaneCount()) {
    for (size_t i = 0; i < mvImgBufInfo.size(); ++i) {
      mvImgBufInfo[i]->pa = mvBufHeapInfo[i]->pa;
      mvImgBufInfo[i]->va = mvBufHeapInfo[i]->va;
    }
  } else {
    // non-BLOB image buffer created from BLOB heap.
    mvImgBufInfo[0]->pa = mvBufHeapInfo[0]->pa;
    mvImgBufInfo[0]->va = mvBufHeapInfo[0]->va;
    for (size_t i = 1; i < mvImgBufInfo.size(); ++i) {
      mvImgBufInfo[i]->pa =
          (0 == mvImgBufInfo[0]->pa)
              ? 0
              : mvImgBufInfo[i - 1]->pa + mvImgBufInfo[i - 1]->sizeInBytes;
      mvImgBufInfo[i]->va =
          (0 == mvImgBufInfo[0]->va)
              ? 0
              : mvImgBufInfo[i - 1]->va + mvImgBufInfo[i - 1]->sizeInBytes;
    }
  }
  //
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::unlockBuf(char const* szCallerName) {
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  MBOOL ret = unlockBufLocked(szCallerName);
  //
  if (getPlaneCount() == mspImgBufHeap->getPlaneCount()) {
    for (size_t i = 0; i < mvImgBufInfo.size(); ++i) {
      mvImgBufInfo[i]->pa = mvBufHeapInfo[i]->pa;
      mvImgBufInfo[i]->va = mvBufHeapInfo[i]->va;
    }
  } else {
    // non-BLOB image buffer created from BLOB heap.
    mvImgBufInfo[0]->pa = mvBufHeapInfo[0]->pa;
    mvImgBufInfo[0]->va = mvBufHeapInfo[0]->va;
    for (size_t i = 1; i < mvImgBufInfo.size(); ++i) {
      mvImgBufInfo[i]->pa =
          (0 == mvImgBufInfo[0]->pa) ? 0 : mvImgBufInfo[i]->pa;
      mvImgBufInfo[i]->va =
          (0 == mvImgBufInfo[0]->va) ? 0 : mvImgBufInfo[i]->va;
    }
  }
  //
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::lockBufLocked(char const* szCallerName, MINT usage) {
  MY_LOGE_IF(0 < mLockCount, "%s@ Has locked - LockCount:%d,%p,%ld",
             szCallerName, mLockCount, mspImgBufHeap.get(),
             mspImgBufHeap.use_count());
  //
  if (!mspImgBufHeap->lockBuf(szCallerName, usage)) {
    MY_LOGE("%s@ impLockBuf() usage:%#x", szCallerName, usage);
    return MFALSE;
  }
  //
  //  Check Buffer Info.
  if (mspImgBufHeap->getPlaneCount() != mvBufHeapInfo.size()) {
    MY_LOGE("%s@ BufInfo.size(%zu) != PlaneCount(%zu)", szCallerName,
            mvBufHeapInfo.size(), mspImgBufHeap->getPlaneCount());
    return MFALSE;
  }
  //
  for (size_t i = 0; i < mvBufHeapInfo.size(); i++) {
    mvBufHeapInfo[i]->va =
        (0 != (usage & eBUFFER_USAGE_SW_MASK)) ? mspImgBufHeap->getBufVA(i) : 0;
    mvBufHeapInfo[i]->pa =
        (0 != (usage & eBUFFER_USAGE_HW_MASK)) ? mspImgBufHeap->getBufPA(i) : 0;
    //
    if (0 != (usage & eBUFFER_USAGE_SW_MASK) && 0 == mvBufHeapInfo[i]->va) {
      MY_LOGE("%s@ Bad result at %zu-th plane: va=0 with SW usage:%#x",
              szCallerName, i, usage);
      return MFALSE;
    }
    //
  }
  //
  mLockUsage = usage;
  mLockCount++;
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::unlockBufLocked(char const* szCallerName) {
  if (0 == mLockCount) {
    MY_LOGW("%s@ Never lock", szCallerName);
    return MFALSE;
  }
  //
  if (!mspImgBufHeap->unlockBuf(szCallerName)) {
    MY_LOGE("%s@ impUnlockBuf() usage:%#x", szCallerName, mLockUsage);
    return MFALSE;
  }
  for (size_t i = 0; i < mvBufHeapInfo.size(); i++) {
    mvBufHeapInfo[i]->va = 0;
    mvBufHeapInfo[i]->pa = 0;
  }
  //
  mLockUsage = 0;
  mLockCount--;
  return MTRUE;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::saveToFile(char const* filepath) {
  MBOOL ret = MFALSE;
  int fd[3] = {-1, -1, -1};
  char tmp[512];

  MBOOL bUFO =
      mImgFormat == eImgFmt_UFO_BAYER8 || mImgFormat == eImgFmt_UFO_BAYER10 ||
      mImgFormat == eImgFmt_UFO_BAYER12 || mImgFormat == eImgFmt_UFO_BAYER14 ||
      mImgFormat == eImgFmt_UFO_FG_BAYER8 ||
      mImgFormat == eImgFmt_UFO_FG_BAYER10 ||
      mImgFormat == eImgFmt_UFO_FG_BAYER12 ||
      mImgFormat == eImgFmt_UFO_FG_BAYER14;

  if (!mfgFileCacheEn) {
    MY_LOGD("save to %s", filepath);
    fd[0] = ::open(filepath, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
    if (fd[0] < 0) {
      MY_LOGE("fail to open %s: %s", filepath, ::strerror(errno));
      goto lbExit;
    }
    if (bUFO) {
      ::snprintf(tmp, sizeof(tmp), "%s.ltbl", filepath);
      fd[1] = ::open(tmp, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
      if (fd[1] < 0) {
        MY_LOGE("fail to open %s: %s", tmp, ::strerror(errno));
        goto lbExit;
      }
      ::snprintf(tmp, sizeof(tmp), "%s.meta", filepath);
      fd[2] = ::open(tmp, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU);
      if (fd[2] < 0) {
        MY_LOGE("fail to open %s: %s", tmp, ::strerror(errno));
        goto lbExit;
      }
    }

    if (!bUFO) {
      for (size_t i = 0; i < getPlaneCount(); i++) {
        MUINT8* pBuf = reinterpret_cast<MUINT8*>(getBufVA(i));
        size_t size = getBufSizeInBytes(i);
        size_t nw = ::write(fd[0], pBuf, size);
        if (nw != size) {
          MY_LOGE("fail to write %s, %zu-th plane (err=%s)", filepath, i,
                  ::strerror(errno));
          break;
        }
      }
    } else {
      for (size_t i = 0; i < getPlaneCount(); i++) {
        MUINT8* pBuf = reinterpret_cast<MUINT8*>(getBufVA(i));
        size_t size = getBufSizeInBytes(i);
        size_t nw = ::write(fd[i], pBuf, size);
        if (nw != size) {
          MY_LOGE("fail to write %s (UFO format), %zu-th plane (err=%s)",
                  filepath, i, ::strerror(errno));
        }
      }
    }
  }

  ret = MTRUE;
lbExit:
  for (size_t i = 0; i < sizeof(fd) / sizeof(fd[0]); i++) {
    if (fd[i] >= 0) {
      ::close(fd[i]);
    }
  }

  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::loadFromFile(char const* filepath) {
  MBOOL ret = MFALSE;
  MBOOL isLocked = MFALSE;
  int fd = -1;
  int filesize = 0;
  //
  isLocked = lockBuf(filepath, eBUFFER_USAGE_SW_WRITE_OFTEN);
  if (!isLocked) {
    MY_LOGE("lockBuf fail");
    goto lbExit;
  }
  //
  MY_LOGD("load from %s", filepath);
  fd = ::open(filepath, O_RDONLY);
  if (fd < 0) {
    MY_LOGE("fail to open %s: %s", filepath, ::strerror(errno));
    goto lbExit;
  }
  //
  filesize = ::lseek(fd, 0, SEEK_END);
  ::lseek(fd, 0, SEEK_SET);
  //
  for (MUINT i = 0; i < getPlaneCount(); i++) {
    MUINT8* pBuf = reinterpret_cast<MUINT8*>(getBufVA(i));
    MUINT bytesToRead = getBufSizeInBytes(i);
    MUINT bytesRead = 0;
    int nr = 0, cnt = 0;
    while (0 < bytesToRead) {
      nr = ::read(fd, pBuf + bytesRead, bytesToRead - bytesRead);
      if (nr < 0) {
        MY_LOGE(
            "fail to read from %s, %d-th plane, read-count:%d, read-bytes:%d : "
            "%s",
            filepath, i, cnt, bytesRead, ::strerror(errno));
        goto lbExit;
      }
      bytesToRead -= nr;
      bytesRead += nr;
      cnt++;
    }
  }
  //
  ret = MTRUE;
lbExit:
  //
  if (fd >= 0) {
    ::close(fd);
  }
  //
  if (isLocked) {
    unlockBuf(filepath);
  }
  //
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MBOOL
BaseImageBuffer::updateInfo(MSize const& imgSize) {
  MBOOL ret = MFALSE;
  std::lock_guard<std::mutex> _l(mLockMtx);
  //
  if (mImgSize != imgSize) {
    MY_LOGD("update imgSize(%dx%d -> %dx%d)@0-plane", mImgSize.w, mImgSize.h,
            imgSize.w, imgSize.h);
    //
    mImgSize = imgSize;
  }
  return ret;
}
