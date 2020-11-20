/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/reprocess_effect/portrait_mode_effect.h"

#include <linux/videodev2.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/command_line.h>
#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/macros.h>
#include <base/memory/unsafe_shared_memory_region.h>
#include <base/numerics/safe_conversions.h>
#include <base/posix/eintr_wrapper.h>
#include <base/process/launch.h>
#include <base/stl_util.h>
#include <base/values.h>
#include <libyuv.h>
#include <libyuv/convert_argb.h>
#include <system/camera_metadata.h>

#include "cros-camera/camera_gpu_algo_header.h"
#include "cros-camera/common.h"
#include "hal_adapter/scoped_yuv_buffer_handle.h"

namespace cros {

// 1: enable portrait processing
// 0: disable portrait processing; apps should not set this value
const VendorTagInfo kRequestVendorTag[] = {
    {"com.google.effect.portraitMode", TYPE_BYTE, {.u8 = 0}}};

// SegmentationResult::kSuccess: portrait mode segmentation succeeds
// SegmentationResult::kFailure: portrait mode segmentation fails
// SegmentationResult::kTimeout: portrait processing timeout
const VendorTagInfo kResultVendorTag[] = {
    {"com.google.effect.portraitModeSegmentationResult", TYPE_BYTE, {.u8 = 0}}};

PortraitModeEffect::PortraitModeEffect()
    : enable_vendor_tag_(0),
      result_vendor_tag_(0),
      buffer_manager_(CameraBufferManager::GetInstance()),
      gpu_algo_manager_(nullptr),
      condvar_(&lock_) {}

int32_t PortraitModeEffect::InitializeAndGetVendorTags(
    std::vector<VendorTagInfo>* request_vendor_tags,
    std::vector<VendorTagInfo>* result_vendor_tags,
    CameraMojoChannelManagerToken* token) {
  VLOGF_ENTER();
  if (!request_vendor_tags || !result_vendor_tags) {
    return -EINVAL;
  }

  gpu_algo_manager_ = GPUAlgoManager::GetInstance(token);
  if (!gpu_algo_manager_) {
    LOGF(WARNING)
        << "Cannot connect to GPU algorithm service. Disable portrait mode.";
    return 0;
  }
  *request_vendor_tags = {std::begin(kRequestVendorTag),
                          std::end(kRequestVendorTag)};
  *result_vendor_tags = {std::begin(kResultVendorTag),
                         std::end(kResultVendorTag)};
  return 0;
}

int32_t PortraitModeEffect::SetVendorTags(uint32_t request_vendor_tag_start,
                                          uint32_t request_vendor_tag_count,
                                          uint32_t result_vendor_tag_start,
                                          uint32_t result_vendor_tag_count) {
  if (request_vendor_tag_count != base::size(kRequestVendorTag) ||
      result_vendor_tag_count != base::size(kResultVendorTag)) {
    return -EINVAL;
  }
  enable_vendor_tag_ = request_vendor_tag_start;
  result_vendor_tag_ = result_vendor_tag_start;
  LOGF(INFO) << "Allocated vendor tag " << std::hex << enable_vendor_tag_;
  return 0;
}

int32_t PortraitModeEffect::ReprocessRequest(
    const camera_metadata_t& settings,
    ScopedYUVBufferHandle* input_buffer,
    uint32_t width,
    uint32_t height,
    uint32_t orientation,
    uint32_t v4l2_format,
    android::CameraMetadata* result_metadata,
    ScopedYUVBufferHandle* output_buffer) {
  VLOGF_ENTER();

  const uint32_t kPortraitProcessorTimeoutSecs = 15;
  if (!input_buffer || !*input_buffer || !output_buffer || !*output_buffer) {
    return -EINVAL;
  }
  camera_metadata_ro_entry_t entry = {};
  if (find_camera_metadata_ro_entry(&settings, enable_vendor_tag_, &entry) !=
      0) {
    LOGF(ERROR) << "Failed to find portrait mode vendor tag";
    return -EINVAL;
  }
  auto* input_ycbcr = input_buffer->LockYCbCr();
  if (!input_ycbcr) {
    LOGF(ERROR) << "Failed to lock input buffer handle";
    return -EINVAL;
  }
  auto* output_ycbcr = output_buffer->LockYCbCr();
  if (!output_ycbcr) {
    LOGF(ERROR) << "Failed to lock output buffer handle";
    return -EINVAL;
  }

  if (entry.data.u8[0] != 0) {
    const uint32_t kRGBNumOfChannels = 3;
    size_t rgb_buf_size = width * height * kRGBNumOfChannels;
    base::UnsafeSharedMemoryRegion input_rgb_shm_region =
        base::UnsafeSharedMemoryRegion::Create(rgb_buf_size);
    base::WritableSharedMemoryMapping input_rgb_shm_mapping =
        input_rgb_shm_region.Map();
    if (!input_rgb_shm_mapping.IsValid()) {
      LOGF(ERROR) << "Failed to create shared memory for input RGB buffer";
      return -ENOMEM;
    }
    base::UnsafeSharedMemoryRegion output_rgb_shm_region =
        base::UnsafeSharedMemoryRegion::Create(rgb_buf_size);
    base::WritableSharedMemoryMapping output_rgb_shm_mapping =
        output_rgb_shm_region.Map();
    if (!output_rgb_shm_mapping.IsValid()) {
      LOGF(ERROR) << "Failed to create shared memory for output RGB buffer";
      return -ENOMEM;
    }
    uint32_t rgb_buf_stride = width * kRGBNumOfChannels;
    int result = 0;
    base::ScopedClosureRunner result_metadata_runner(
        base::Bind(&PortraitModeEffect::UpdateResultMetadata,
                   base::Unretained(this), result_metadata, &result));
    result = ConvertYUVToRGB(v4l2_format, *input_ycbcr,
                             input_rgb_shm_mapping.memory(), rgb_buf_stride,
                             width, height);
    if (result != 0) {
      LOGF(ERROR) << "Failed to convert from YUV to RGB";
      return result;
    }

    LOGF(INFO) << "Starting portrait processing";
    // Duplicate the file descriptors since shm_open() returns descriptors
    // associated with FD_CLOEXEC, which causes the descriptors to be closed at
    // the call of execve(). Duplicated descriptors do not share the
    // close-on-exec flag.
    base::ScopedFD dup_input_rgb_buf_fd(
        HANDLE_EINTR(dup(input_rgb_shm_region.GetPlatformHandle().fd)));
    base::ScopedFD dup_output_rgb_buf_fd(
        HANDLE_EINTR(dup(output_rgb_shm_region.GetPlatformHandle().fd)));

    class ScopedHandle {
     public:
      explicit ScopedHandle(GPUAlgoManager* algo, int fd)
          : algo_(algo), handle_(-1) {
        if (algo_ != nullptr) {
          handle_ = algo_->RegisterBuffer(fd);
        }
      }
      ~ScopedHandle() {
        if (IsValid()) {
          std::vector<int32_t> handles({handle_});
          algo_->DeregisterBuffers(handles);
        }
      }
      bool IsValid() const { return handle_ >= 0; }
      int32_t Get() const { return handle_; }

     private:
      GPUAlgoManager* algo_;
      int32_t handle_;
    };

    ScopedHandle input_buffer_handle(gpu_algo_manager_,
                                     dup_input_rgb_buf_fd.release());
    ScopedHandle output_buffer_handle(gpu_algo_manager_,
                                      dup_output_rgb_buf_fd.release());
    if (!input_buffer_handle.IsValid() || !output_buffer_handle.IsValid()) {
      LOGF(ERROR) << "Failed to register buffers";
      result = -EINVAL;
      return result;
    }
    std::vector<uint8_t> req_header(sizeof(CameraGPUAlgoCmdHeader));
    auto* header = reinterpret_cast<CameraGPUAlgoCmdHeader*>(req_header.data());
    header->command = CameraGPUAlgoCommand::PORTRAIT_MODE;
    auto& params = header->params.portrait_mode;
    params.input_buffer_handle = input_buffer_handle.Get();
    params.output_buffer_handle = output_buffer_handle.Get();
    params.width = width;
    params.height = height;
    params.orientation = orientation;
    return_status_ = -ETIMEDOUT;
    gpu_algo_manager_->Request(
        req_header, -1 /* buffers are passed in the header */,
        base::Bind(&PortraitModeEffect::ReturnCallback, base::AsWeakPtr(this)));
    base::AutoLock auto_lock(lock_);
    condvar_.TimedWait(
        base::TimeDelta::FromSeconds(kPortraitProcessorTimeoutSecs));
    result = return_status_;

    LOGF(INFO) << "Portrait processing finished, result: " << result;
    if (result != 0) {
      // Portrait processing finishes with non-zero result when there's no human
      // face in the image. Returns 0 here with the status set in the vendor tag
      // by |result_metadata_runner|.
      // TODO(kamesan): make the status returned from portrait library more
      // fine-grained to filter critical errors.
      return 0;
    }

    result = ConvertRGBToYUV(output_rgb_shm_mapping.memory(), rgb_buf_stride,
                             v4l2_format, *output_ycbcr, width, height);
    if (result != 0) {
      LOGF(ERROR) << "Failed to convert from RGB to YUV";
    }
    return result;
  } else {
    // TODO(hywu): add an API to query if an effect want to reprocess this
    // request or not
    LOGF(WARNING) << "Portrait mode is turned off. Just copy the image.";
    switch (v4l2_format) {
      case V4L2_PIX_FMT_NV12:
      case V4L2_PIX_FMT_NV12M:
        libyuv::CopyPlane(static_cast<const uint8_t*>(input_ycbcr->y),
                          input_ycbcr->ystride,
                          static_cast<uint8_t*>(output_ycbcr->y),
                          output_ycbcr->ystride, width, height);
        libyuv::CopyPlane_16(static_cast<const uint16_t*>(input_ycbcr->cb),
                             input_ycbcr->cstride,
                             static_cast<uint16_t*>(output_ycbcr->cb),
                             output_ycbcr->cstride, width, height);
        break;
      case V4L2_PIX_FMT_NV21:
      case V4L2_PIX_FMT_NV21M:
        libyuv::CopyPlane(static_cast<const uint8_t*>(input_ycbcr->y),
                          input_ycbcr->ystride,
                          static_cast<uint8_t*>(output_ycbcr->y),
                          output_ycbcr->ystride, width, height);
        libyuv::CopyPlane_16(static_cast<const uint16_t*>(input_ycbcr->cr),
                             input_ycbcr->cstride,
                             static_cast<uint16_t*>(output_ycbcr->cr),
                             output_ycbcr->cstride, width, height);
        break;
      case V4L2_PIX_FMT_YUV420:
      case V4L2_PIX_FMT_YUV420M:
      case V4L2_PIX_FMT_YVU420:
      case V4L2_PIX_FMT_YVU420M:
        if (libyuv::I420Copy(
                static_cast<const uint8_t*>(input_ycbcr->y),
                input_ycbcr->ystride,
                static_cast<const uint8_t*>(input_ycbcr->cb),
                input_ycbcr->cstride,
                static_cast<const uint8_t*>(input_ycbcr->cr),
                input_ycbcr->cstride, static_cast<uint8_t*>(output_ycbcr->y),
                output_ycbcr->ystride, static_cast<uint8_t*>(output_ycbcr->cb),
                output_ycbcr->cstride, static_cast<uint8_t*>(output_ycbcr->cr),
                output_ycbcr->cstride, width, height) != 0) {
          LOGF(ERROR) << "Failed to copy I420";
          return -ENOMEM;
        }
        break;
      default:
        LOGF(ERROR) << "Unsupported format " << FormatToString(v4l2_format);
        return -EINVAL;
    }
  }
  return 0;
}

void PortraitModeEffect::UpdateResultMetadata(
    android::CameraMetadata* result_metadata, const int* result) {
  SegmentationResult segmentation_result =
      (*result == 0) ? SegmentationResult::kSuccess
                     : (*result == -ETIMEDOUT) ? SegmentationResult::kTimeout
                                               : SegmentationResult::kFailure;
  result_metadata->update(result_vendor_tag_,
                          reinterpret_cast<uint8_t*>(&segmentation_result), 1);
}

void PortraitModeEffect::ReturnCallback(uint32_t status,
                                        int32_t buffer_handle) {
  VLOGF_ENTER();
  base::AutoLock auto_lock(lock_);
  return_status_ = -status;
  condvar_.Signal();
}

int PortraitModeEffect::ConvertYUVToRGB(uint32_t v4l2_format,
                                        const android_ycbcr& ycbcr,
                                        void* rgb_buf_addr,
                                        uint32_t rgb_buf_stride,
                                        uint32_t width,
                                        uint32_t height) {
  switch (v4l2_format) {
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV12M:
      if (libyuv::NV12ToRGB24(
              static_cast<const uint8_t*>(ycbcr.y), ycbcr.ystride,
              static_cast<const uint8_t*>(ycbcr.cb), ycbcr.cstride,
              static_cast<uint8_t*>(rgb_buf_addr), rgb_buf_stride, width,
              height) != 0) {
        LOGF(ERROR) << "Failed to convert from NV12 to RGB";
        return -EINVAL;
      }
      break;
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV21M:
      if (libyuv::NV21ToRGB24(
              static_cast<const uint8_t*>(ycbcr.y), ycbcr.ystride,
              static_cast<const uint8_t*>(ycbcr.cr), ycbcr.cstride,
              static_cast<uint8_t*>(rgb_buf_addr), rgb_buf_stride, width,
              height) != 0) {
        LOGF(ERROR) << "Failed to convert from NV21 to RGB";
        return -EINVAL;
      }
      break;
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YUV420M:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_YVU420M:
      if (libyuv::I420ToRGB24(
              static_cast<const uint8_t*>(ycbcr.y), ycbcr.ystride,
              static_cast<const uint8_t*>(ycbcr.cb), ycbcr.cstride,
              static_cast<const uint8_t*>(ycbcr.cr), ycbcr.cstride,
              static_cast<uint8_t*>(rgb_buf_addr), rgb_buf_stride, width,
              height) != 0) {
        LOGF(ERROR) << "Failed to convert from I420 to RGB";
        return -EINVAL;
      }
      break;
    default:
      LOGF(ERROR) << "Unsupported format " << FormatToString(v4l2_format);
      return -EINVAL;
  }
  return 0;
}

int PortraitModeEffect::ConvertRGBToYUV(void* rgb_buf_addr,
                                        uint32_t rgb_buf_stride,
                                        uint32_t v4l2_format,
                                        const android_ycbcr& ycbcr,
                                        uint32_t width,
                                        uint32_t height) {
  auto convert_rgb_to_nv = [](const uint8_t* rgb_addr,
                              const android_ycbcr& ycbcr, uint32_t width,
                              uint32_t height, uint32_t v4l2_format) {
    // TODO(hywu): convert RGB to NV12/NV21 directly
    auto div_round_up = [](uint32_t n, uint32_t d) {
      return ((n + d - 1) / d);
    };
    const uint32_t kRGBNumOfChannels = 3;
    uint32_t ystride = width;
    uint32_t cstride = div_round_up(width, 2);
    uint32_t total_size =
        width * height + cstride * div_round_up(height, 2) * 2;
    uint32_t uv_plane_size = cstride * div_round_up(height, 2);
    auto i420_y = std::make_unique<uint8_t[]>(total_size);
    uint8_t* i420_cb = i420_y.get() + width * height;
    uint8_t* i420_cr = i420_cb + uv_plane_size;
    if (libyuv::RGB24ToI420(static_cast<const uint8_t*>(rgb_addr),
                            width * kRGBNumOfChannels, i420_y.get(), ystride,
                            i420_cb, cstride, i420_cr, cstride, width,
                            height) != 0) {
      LOGF(ERROR) << "Failed to convert from RGB to I420";
      return -ENOMEM;
    }
    if (v4l2_format == V4L2_PIX_FMT_NV12) {
      if (libyuv::I420ToNV12(i420_y.get(), ystride, i420_cb, cstride, i420_cr,
                             cstride, static_cast<uint8_t*>(ycbcr.y),
                             ycbcr.ystride, static_cast<uint8_t*>(ycbcr.cb),
                             ycbcr.cstride, width, height) != 0) {
        LOGF(ERROR) << "Failed to convert from I420 to NV12";
        return -ENOMEM;
      }
    } else if (v4l2_format == V4L2_PIX_FMT_NV21) {
      if (libyuv::I420ToNV21(i420_y.get(), ystride, i420_cb, cstride, i420_cr,
                             cstride, static_cast<uint8_t*>(ycbcr.y),
                             ycbcr.ystride, static_cast<uint8_t*>(ycbcr.cr),
                             ycbcr.cstride, width, height) != 0) {
        LOGF(ERROR) << "Failed to convert from I420 to NV21";
        return -ENOMEM;
      }
    } else {
      return -EINVAL;
    }
    return 0;
  };
  switch (v4l2_format) {
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV12M:
      if (convert_rgb_to_nv(static_cast<const uint8_t*>(rgb_buf_addr), ycbcr,
                            width, height, V4L2_PIX_FMT_NV12) != 0) {
        return -EINVAL;
      }
      break;
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV21M:
      if (convert_rgb_to_nv(static_cast<const uint8_t*>(rgb_buf_addr), ycbcr,
                            width, height, V4L2_PIX_FMT_NV21) != 0) {
        return -EINVAL;
      }
      break;
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YUV420M:
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_YVU420M:
      if (libyuv::RGB24ToI420(static_cast<const uint8_t*>(rgb_buf_addr),
                              rgb_buf_stride, static_cast<uint8_t*>(ycbcr.y),
                              ycbcr.ystride, static_cast<uint8_t*>(ycbcr.cb),
                              ycbcr.cstride, static_cast<uint8_t*>(ycbcr.cr),
                              ycbcr.cstride, width, height) != 0) {
        LOGF(ERROR) << "Failed to convert from RGB";
        return -EINVAL;
      }
      break;
    default:
      LOGF(ERROR) << "Unsupported format " << FormatToString(v4l2_format);
      return -EINVAL;
  }
  return 0;
}

}  // namespace cros
