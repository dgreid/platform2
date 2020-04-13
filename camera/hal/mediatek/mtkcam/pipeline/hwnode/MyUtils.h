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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_MYUTILS_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_MYUTILS_H_

#include <algorithm>
#include <isp_tuning/isp_tuning.h>  // EIspProfile_*
#include <memory>
#include <mtkcam/def/common.h>
#include <mtkcam/utils/std/Trace.h>
#include <mtkcam/utils/std/Log.h>
#include <mtkcam/utils/std/Sync.h>
#include <mtkcam/utils/imgbuf/IImageBuffer.h>
#include <mtkcam/aaa/IHal3A.h>
#include <mtkcam/drv/IHalSensor.h>  // MAKE_HalSensorList
#include <sys/prctl.h>
#include <sys/resource.h>
#include <vector>

/******************************************************************************
 *
 ******************************************************************************/
namespace NSCam {
namespace v3 {

// utilities for crop

inline MINT32 div_round(MINT32 const numerator, MINT32 const denominator) {
  return ((numerator < 0) ^ (denominator < 0))
             ? ((numerator - denominator / 2) / denominator)
             : ((numerator + denominator / 2) / denominator);
}

struct vector_f {  // vector with floating point
  MPoint p;
  MPoint pf;

  explicit vector_f(MPoint const& rP = MPoint(), MPoint const& rPf = MPoint())
      : p(rP), pf(rPf) {}
};

struct simpleTransform {
  // just support translation than scale, not a general formulation
  // translation
  MPoint tarOrigin;
  // scale
  MSize oldScale;
  MSize newScale;

  simpleTransform(MPoint rOrigin = MPoint(),
                  MSize rOldScale = MSize(),
                  MSize rNewScale = MSize())
      : tarOrigin(rOrigin), oldScale(rOldScale), newScale(rNewScale) {}
};

// transform MPoint
inline MPoint transform(simpleTransform const& trans, MPoint const& p) {
  return MPoint(
      div_round((p.x - trans.tarOrigin.x) * trans.newScale.w, trans.oldScale.w),
      div_round((p.y - trans.tarOrigin.y) * trans.newScale.h,
                trans.oldScale.h));
}

inline MPointF transform(simpleTransform const& trans, MPointF const& p) {
  return MPointF(
      ((p.x - trans.tarOrigin.x) * trans.newScale.w / trans.oldScale.w),
      ((p.y - trans.tarOrigin.y) * trans.newScale.h / trans.oldScale.h));
}

inline MPoint inv_transform(simpleTransform const& trans, MPoint const& p) {
  return MPoint(
      div_round(p.x * trans.oldScale.w, trans.newScale.w) + trans.tarOrigin.x,
      div_round(p.y * trans.oldScale.h, trans.newScale.h) + trans.tarOrigin.y);
}

inline int int_floor(float x) {
  int i = static_cast<int>(x);
  return i - (i > x);
}

// transform vector_f
inline vector_f transform(simpleTransform const& trans, vector_f const& p) {
  MFLOAT const x = (p.p.x + (p.pf.x / (MFLOAT)(1u << 31))) * trans.newScale.w /
                   trans.oldScale.w;
  MFLOAT const y = (p.p.y + (p.pf.y / (MFLOAT)(1u << 31))) * trans.newScale.h /
                   trans.oldScale.h;
  int const x_int = int_floor(x);
  int const y_int = int_floor(y);
  return vector_f(MPoint(x_int, y_int),
                  MPoint((x - x_int) * (1u << 31), (y - y_int) * (1u << 31)));
}

inline vector_f inv_transform(simpleTransform const& trans, vector_f const& p) {
  MFLOAT const x = (p.p.x + (p.pf.x / (MFLOAT)(1u << 31))) * trans.oldScale.w /
                   trans.newScale.w;
  MFLOAT const y = (p.p.y + (p.pf.y / (MFLOAT)(1u << 31))) * trans.oldScale.h /
                   trans.newScale.h;
  int const x_int = int_floor(x);
  int const y_int = int_floor(y);
  return vector_f(MPoint(x_int, y_int),
                  MPoint((x - x_int) * (1u << 31), (y - y_int) * (1u << 31)));
}

// transform MSize
inline MSize transform(simpleTransform const& trans, MSize const& s) {
  return MSize(div_round(s.w * trans.newScale.w, trans.oldScale.w),
               div_round(s.h * trans.newScale.h, trans.oldScale.h));
}

inline MSizeF transform(simpleTransform const& trans, MSizeF const& s) {
  return MSizeF((s.w * trans.newScale.w / trans.oldScale.w),
                (s.h * trans.newScale.h / trans.oldScale.h));
}

inline MSize inv_transform(simpleTransform const& trans, MSize const& s) {
  return MSize(div_round(s.w * trans.oldScale.w, trans.newScale.w),
               div_round(s.h * trans.oldScale.h, trans.newScale.h));
}

// transform MRect
inline MRect transform(simpleTransform const& trans, MRect const& r) {
  return MRect(transform(trans, r.p), transform(trans, r.s));
}

inline MRectF transform(simpleTransform const& trans, MRectF const& r) {
  return MRectF(transform(trans, r.p), transform(trans, r.s));
}

inline MRect inv_transform(simpleTransform const& trans, MRect const& r) {
  return MRect(inv_transform(trans, r.p), inv_transform(trans, r.s));
}

/******************************************************************************
 *  Metadata Access
 ******************************************************************************/
template <typename T>
inline MBOOL tryGetMetadata(IMetadata const* pMetadata,
                            MUINT32 const tag,
                            T* rVal) {
  if (pMetadata == nullptr) {
    CAM_LOGW("pMetadata == NULL");
    return MFALSE;
  }
  //
  IMetadata::IEntry entry = pMetadata->entryFor(tag);
  if (!entry.isEmpty()) {
    *rVal = entry.itemAt(0, Type2Type<T>());
    return MTRUE;
  }
  //
  return MFALSE;
}

template <typename T>
inline MBOOL trySetMetadata(IMetadata* pMetadata,
                            MUINT32 const tag,
                            T const& val) {
  if (pMetadata == nullptr) {
    CAM_LOGW("pMetadata == NULL");
    return MFALSE;
  }
  //
  IMetadata::IEntry entry(tag);
  entry.push_back(val, Type2Type<T>());
  if (0 == pMetadata->update(tag, entry)) {
    return MTRUE;
  }
  //
  return MFALSE;
}

/******************************************************************************
 *  Simulate HAL 3A.
 ******************************************************************************/
/******************************************************************************
 *  Hardware Related
 ******************************************************************************/
inline MBOOL isPostProcRawSupported() {
  MBOOL bSupport = MTRUE;
#if (MTKCAM_HW_NODE_WITH_LEGACY_SUPPORT > 0)
  bSupport = MFALSE;
#endif
  return bSupport;
}

/******************************************************************************
 *  Opaque Reprocessing Utility
 ******************************************************************************/
#define MAX_METADATA_SIZE (200000)

class OpaqueReprocUtil {
 private:
  typedef struct opaque_reproc_info_t {
    MINT8 aligned_byte;
    MSize raw_size;
    MINT raw_format;
    size_t stride_in_bytes;
    size_t payload_offset;
    size_t payload_length;
    size_t app_meta_offset;
    size_t app_meta_length;
    size_t hal_meta_offset;
    size_t hal_meta_length;
    size_t lcso_image_offset;
    size_t lcso_image_length;

    MVOID dump() {
      CAM_LOGD(
          "[opaque] aligned_byte(0x%x) raw_size(%d,%d) raw_format(0x%x) "
          "stride_in_bytes(%zu) "
          "payload(%zu-%zu) app_meta(%zu-%zu) hal_meta(%zu-%zu)"
          "lcso_image(%zu-%zu)",
          aligned_byte, raw_size.w, raw_size.h, raw_format, stride_in_bytes,
          payload_offset, payload_length, app_meta_offset, app_meta_length,
          hal_meta_offset, hal_meta_length, lcso_image_offset,
          lcso_image_length);
    }
  } opaque_reproc_info_t;

 public:
  static MERROR setOpaqueInfoToHeap(
      std::shared_ptr<IImageBufferHeap> const& pImageBufferHeap,
      MSize const rawSize,
      MINT const rawFormat,
      size_t const rawStrideInBytes,
      size_t const rawSizeInBytes) {
    size_t infoOffset = (ssize_t)pImageBufferHeap->getBufSizeInBytes(0) -
                        sizeof(opaque_reproc_info_t);
    if (infoOffset < rawSizeInBytes) {
      return -EINVAL;
    }

    MINTPTR ptrOpaqueBuf = (MINTPTR)pImageBufferHeap->getBufVA(0);
    opaque_reproc_info_t* pInfo =
        reinterpret_cast<opaque_reproc_info_t*>(ptrOpaqueBuf + infoOffset);

    pInfo->aligned_byte = 0x00;
    pInfo->raw_size = rawSize;
    pInfo->raw_format = rawFormat;
    pInfo->stride_in_bytes = rawStrideInBytes;
    pInfo->payload_offset = rawSizeInBytes;
    pInfo->payload_length = infoOffset - rawSizeInBytes;
    pInfo->app_meta_offset = 0;
    pInfo->app_meta_length = 0;
    pInfo->hal_meta_offset = 0;
    pInfo->hal_meta_length = 0;
    pInfo->lcso_image_offset = 0;
    pInfo->lcso_image_length = 0;

    return 0;
  }

  static MERROR getImageBufferFromHeap(
      std::shared_ptr<IImageBufferHeap> pImageBufferHeap,
      std::shared_ptr<IImageBuffer>* rpImageBuffer) {
    size_t infoOffset =
        pImageBufferHeap->getBufSizeInBytes(0) - sizeof(opaque_reproc_info_t);
    MINTPTR ptrOpaqueBuf = (MINTPTR)pImageBufferHeap->getBufVA(0);
    opaque_reproc_info_t* pInfo =
        reinterpret_cast<opaque_reproc_info_t*>(ptrOpaqueBuf + infoOffset);

    if (pInfo->aligned_byte != 0x00) {
      return NO_INIT;
    }

    size_t strideInBytes[3]{pInfo->stride_in_bytes, 0, 0};

    *rpImageBuffer = pImageBufferHeap->createImageBuffer_FromBlobHeap(
        0, pInfo->raw_format, pInfo->raw_size, strideInBytes);

    return 0;
  }

  static MERROR setAppMetadataToHeap(
      std::shared_ptr<IImageBufferHeap> const& pImageBufferHeap,
      IMetadata* appMeta) {
    size_t infoOffset =
        pImageBufferHeap->getBufSizeInBytes(0) - sizeof(opaque_reproc_info_t);
    MINTPTR ptrOpaqueBuf = (MINTPTR)pImageBufferHeap->getBufVA(0);
    opaque_reproc_info_t* pInfo =
        reinterpret_cast<opaque_reproc_info_t*>(ptrOpaqueBuf + infoOffset);

    if (pInfo->aligned_byte != 0x00) {
      return NO_INIT;
    } else if (pInfo->app_meta_length != 0) {
      return ALREADY_EXISTS;
    }

    // calculate the offset of app meta where hal meta/lcso is probably empty
    pInfo->app_meta_offset = pInfo->payload_offset + pInfo->hal_meta_length +
                             pInfo->lcso_image_length;

    void* pAppMetaBuf =
        reinterpret_cast<void*>(ptrOpaqueBuf + pInfo->app_meta_offset);
    MINT32 max_size = pInfo->payload_length - pInfo->hal_meta_length -
                      pInfo->lcso_image_length;
    ssize_t ret = appMeta->flatten(pAppMetaBuf, max_size);

    if (ret < 0) {
      CAM_LOGE("[opaque] oversized payload: ret=%zd, hal=%zu, lcso=%zu", ret,
               pInfo->hal_meta_length, pInfo->lcso_image_length);
      return ret;
    }
    pInfo->app_meta_length = ret;

    return 0;
  }

  static MERROR setHalMetadataToHeap(
      std::shared_ptr<IImageBufferHeap> const& pImageBufferHeap,
      IMetadata* halMeta) {
    size_t infoOffset =
        pImageBufferHeap->getBufSizeInBytes(0) - sizeof(opaque_reproc_info_t);
    MINTPTR ptrOpaqueBuf = (MINTPTR)pImageBufferHeap->getBufVA(0);
    opaque_reproc_info_t* pInfo =
        reinterpret_cast<opaque_reproc_info_t*>(ptrOpaqueBuf + infoOffset);

    if (pInfo->aligned_byte != 0x00) {
      return NO_INIT;
    } else if (pInfo->hal_meta_length != 0) {
      return ALREADY_EXISTS;
    }

    // calculate the offset of app meta where app meta/lcso is probably empty
    pInfo->hal_meta_offset = pInfo->payload_offset + pInfo->app_meta_length +
                             pInfo->lcso_image_length;
    void* pHalMetaBuf =
        reinterpret_cast<void*>(ptrOpaqueBuf + pInfo->hal_meta_offset);
    MINT32 max_size = pInfo->payload_length - pInfo->app_meta_length -
                      pInfo->lcso_image_length;
    ssize_t ret = halMeta->flatten(pHalMetaBuf, max_size);

    if (ret < 0) {
      CAM_LOGE("[opaque] oversized payload: ret =%zd, app=%zu, lcso=%zu", ret,
               pInfo->app_meta_length, pInfo->lcso_image_length);
      return ret;
    }
    pInfo->hal_meta_length = ret;

    return 0;
  }

  static MERROR setLcsoImageToHeap(
      std::shared_ptr<IImageBufferHeap> const& pImageBufferHeap,
      std::shared_ptr<IImageBuffer> pLcsoBuffer) {
    size_t infoOffset =
        pImageBufferHeap->getBufSizeInBytes(0) - sizeof(opaque_reproc_info_t);
    MINTPTR ptrOpaqueBuf = (MINTPTR)pImageBufferHeap->getBufVA(0);
    opaque_reproc_info_t* pInfo =
        reinterpret_cast<opaque_reproc_info_t*>(ptrOpaqueBuf + infoOffset);

    if (pInfo->aligned_byte != 0x00) {
      return NO_INIT;
    } else if (pInfo->lcso_image_length != 0) {
      return ALREADY_EXISTS;
    }

    // calculate the offset of lcso image where app/hal meta is probably empty
    pInfo->lcso_image_offset =
        pInfo->payload_offset + pInfo->app_meta_length + pInfo->hal_meta_length;
    void* pLsocBuf =
        reinterpret_cast<void*>(ptrOpaqueBuf + pInfo->lcso_image_offset);
    MINT32 max_size =
        pInfo->payload_length - pInfo->app_meta_length - pInfo->hal_meta_length;
    if (pLcsoBuffer->getBufSizeInBytes(0) > max_size) {
      CAM_LOGE("[opaque] oversized payload: lcso=%zu, app=%zu, hal=%zu",
               pLcsoBuffer->getBufSizeInBytes(0), pInfo->app_meta_length,
               pInfo->hal_meta_length);
      return BAD_VALUE;
    }
    memcpy(pLsocBuf, reinterpret_cast<void*>(pLcsoBuffer->getBufVA(0)),
           pLcsoBuffer->getBufSizeInBytes(0));

    pInfo->lcso_image_length = pLcsoBuffer->getBufSizeInBytes(0);

    return 0;
  }

  static MERROR getAppMetadataFromHeap(
      std::shared_ptr<IImageBufferHeap> const& pImageBufferHeap,
      IMetadata* appMeta) {
    size_t infoOffset =
        pImageBufferHeap->getBufSizeInBytes(0) - sizeof(opaque_reproc_info_t);
    MINTPTR ptrOpaqueBuf = (MINTPTR)pImageBufferHeap->getBufVA(0);
    opaque_reproc_info_t* pInfo =
        reinterpret_cast<opaque_reproc_info_t*>(ptrOpaqueBuf + infoOffset);

    if (pInfo->aligned_byte != 0x00) {
      return NO_INIT;
    }
    void* pAppMetaBuf =
        reinterpret_cast<void*>(ptrOpaqueBuf + pInfo->app_meta_offset);
    ssize_t ret = appMeta->unflatten(pAppMetaBuf, pInfo->app_meta_length);
    CAM_LOGD_IF(0, "[opaque] app meta unflatten from size: %zd", ret);
    if (ret < 0) {
      return ret;
    }

    return 0;
  }

  static MERROR getHalMetadataFromHeap(
      std::shared_ptr<IImageBufferHeap> const& pImageBufferHeap,
      IMetadata* halMeta) {
    size_t infoOffset =
        pImageBufferHeap->getBufSizeInBytes(0) - sizeof(opaque_reproc_info_t);
    MINTPTR ptrOpaqueBuf = (MINTPTR)pImageBufferHeap->getBufVA(0);
    opaque_reproc_info_t* pInfo =
        reinterpret_cast<opaque_reproc_info_t*>(ptrOpaqueBuf + infoOffset);

    if (pInfo->aligned_byte != 0x00) {
      return NO_INIT;
    }

    void* pHalMetaBuf =
        reinterpret_cast<void*>(ptrOpaqueBuf + pInfo->hal_meta_offset);
    ssize_t ret = halMeta->unflatten(pHalMetaBuf, pInfo->hal_meta_length);
    CAM_LOGD_IF(0, "[opaque] hal meta unflatten from size: %zd", ret);
    if (ret < 0) {
      return ret;
    }

    return 0;
  }

  static MERROR getLcsoImageFromHeap(
      std::shared_ptr<IImageBufferHeap> const& pImageBufferHeap,
      std::shared_ptr<IImageBuffer> pLcsoBuffer) {
    size_t infoOffset =
        pImageBufferHeap->getBufSizeInBytes(0) - sizeof(opaque_reproc_info_t);
    MINTPTR ptrOpaqueBuf = (MINTPTR)pImageBufferHeap->getBufVA(0);
    opaque_reproc_info_t* pInfo =
        reinterpret_cast<opaque_reproc_info_t*>(ptrOpaqueBuf + infoOffset);

    if (pInfo->aligned_byte != 0x00) {
      return NO_INIT;
    }

    void* pLsocBuf =
        reinterpret_cast<void*>(ptrOpaqueBuf + pInfo->lcso_image_offset);

    if (pLcsoBuffer->getBufSizeInBytes(0) != pInfo->lcso_image_length) {
      CAM_LOGE("[opaque] invalid lcso size: %zu - %zu",
               pLcsoBuffer->getBufSizeInBytes(0), pInfo->lcso_image_length);
      return BAD_VALUE;
    }

    memcpy(reinterpret_cast<void*>(pLcsoBuffer->getBufVA(0)), pLsocBuf,
           pInfo->lcso_image_length);

    return 0;
  }
};

};  // namespace v3
};  // namespace NSCam

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_MYUTILS_H_
