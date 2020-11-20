/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_REPROCESS_EFFECT_PORTRAIT_MODE_EFFECT_H_
#define CAMERA_HAL_ADAPTER_REPROCESS_EFFECT_PORTRAIT_MODE_EFFECT_H_

#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/callback_helpers.h>
#include <base/synchronization/condition_variable.h>
#include <base/synchronization/lock.h>
#include <base/process/process.h>

#include "cros-camera/camera_buffer_manager.h"
#include "hal_adapter/reprocess_effect/gpu_algo_manager.h"
#include "hal_adapter/reprocess_effect/reprocess_effect.h"

namespace cros {

class PortraitModeEffect final
    : public ReprocessEffect,
      public base::SupportsWeakPtr<PortraitModeEffect> {
 public:
  PortraitModeEffect();
  PortraitModeEffect(const PortraitModeEffect&) = delete;
  PortraitModeEffect& operator=(const PortraitModeEffect&) = delete;

  ~PortraitModeEffect() = default;

  int32_t InitializeAndGetVendorTags(
      std::vector<VendorTagInfo>* request_vendor_tags,
      std::vector<VendorTagInfo>* result_vendor_tags,
      CameraMojoChannelManagerToken* token);

  int32_t SetVendorTags(uint32_t request_vendor_tag_start,
                        uint32_t request_vendor_tag_count,
                        uint32_t result_vendor_tag_start,
                        uint32_t result_vendor_tag_count);

  int32_t ReprocessRequest(const camera_metadata_t& settings,
                           ScopedYUVBufferHandle* input_buffer,
                           uint32_t width,
                           uint32_t height,
                           uint32_t orientation,
                           uint32_t v4l2_format,
                           android::CameraMetadata* result_metadata,
                           ScopedYUVBufferHandle* output_buffer);

 private:
  void UpdateResultMetadata(android::CameraMetadata* result_metadata,
                            const int* result);

  int ConvertYUVToRGB(uint32_t v4l2_format,
                      const android_ycbcr& ycbcr,
                      void* rgb_buf_addr,
                      uint32_t rgb_buf_stride,
                      uint32_t width,
                      uint32_t height);

  int ConvertRGBToYUV(void* rgb_buf_addr,
                      uint32_t rgb_buf_stride,
                      uint32_t v4l2_format,
                      const android_ycbcr& ycbcr,
                      uint32_t width,
                      uint32_t height);

  void ReturnCallback(uint32_t status, int32_t buffer_handle);

  uint32_t enable_vendor_tag_;

  enum class SegmentationResult : uint8_t { kSuccess, kFailure, kTimeout };

  uint32_t result_vendor_tag_;

  CameraBufferManager* buffer_manager_;

  GPUAlgoManager* gpu_algo_manager_;

  base::Lock lock_;

  base::ConditionVariable condvar_;

  int32_t return_status_;
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_REPROCESS_EFFECT_PORTRAIT_MODE_EFFECT_H_
