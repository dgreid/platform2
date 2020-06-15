/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/reprocess_effect/reprocess_effect_manager.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/logging.h>
#include <hardware/gralloc.h>

#include "common/vendor_tag_manager.h"
#include "cros-camera/common.h"
#include "hal_adapter/reprocess_effect/portrait_mode_effect.h"

namespace cros {

ReprocessEffectManager::ReprocessEffectManager()
    : next_vendor_tag_(kReprocessEffectVendorTagStart),
      buffer_manager_(CameraBufferManager::GetInstance()) {}

int32_t ReprocessEffectManager::Initialize() {
  VLOGF_ENTER();
  portrait_mode_ = std::make_unique<PortraitModeEffect>();
  std::vector<VendorTagInfo> request_vendor_tags;
  std::vector<VendorTagInfo> result_vendor_tags;
  if (portrait_mode_->InitializeAndGetVendorTags(&request_vendor_tags,
                                                 &result_vendor_tags) != 0) {
    LOGF(ERROR) << "Failed to initialize portrait mode effect";
    return -ENODEV;
  }
  if (!request_vendor_tags.empty() || !result_vendor_tags.empty()) {
    uint32_t request_vendor_tag_start = next_vendor_tag_;
    for (const auto& it : request_vendor_tags) {
      vendor_tag_effect_info_map_.emplace(
          next_vendor_tag_, VendorTagEffectInfo(it, portrait_mode_.get()));
      next_vendor_tag_++;
    }
    uint32_t result_vendor_tag_start = next_vendor_tag_;
    for (const auto& it : result_vendor_tags) {
      vendor_tag_effect_info_map_.emplace(next_vendor_tag_,
                                          VendorTagEffectInfo(it, nullptr));
      next_vendor_tag_++;
    }
    if (portrait_mode_->SetVendorTags(
            request_vendor_tag_start,
            result_vendor_tag_start - request_vendor_tag_start,
            result_vendor_tag_start,
            next_vendor_tag_ - result_vendor_tag_start) != 0) {
      LOGF(ERROR) << "Failed to set portrait mode effect vendor tags";
      return -ENODEV;
    }
  }

  DCHECK(next_vendor_tag_ <= kReprocessEffectVendorTagEnd);

  get_tag_count = GetTagCount;
  get_all_tags = GetAllTags;
  get_section_name = GetSectionName;
  get_tag_name = GetTagName;
  get_tag_type = GetTagType;

  return 0;
}

bool ReprocessEffectManager::HasReprocessEffectVendorTag(
    const camera_metadata_t& settings) {
  VLOGF_ENTER();
  for (uint32_t tag = kReprocessEffectVendorTagStart; tag < next_vendor_tag_;
       tag++) {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(&settings, tag, &entry) == 0) {
      DCHECK(vendor_tag_effect_info_map_.find(tag) !=
             vendor_tag_effect_info_map_.end());
      if (!vendor_tag_effect_info_map_.at(tag).effect) {
        LOGF(WARNING) << "Received result vendor tag 0x" << std::hex << tag
                      << " in request";
        continue;
      }
      return true;
    }
  }
  return false;
}

void ReprocessEffectManager::UpdateStaticMetadata(
    android::CameraMetadata* metadata) {
  // Currently, all our vendor tag features are based on YUV reprocessing.
  // Skip exporting vendor tag related keys into |metadata| by simply
  // evaluate YUV reprocessing capability of each cameras.
  // TODO(inker): Move evaluation into per vendor tag effeect class.
  const auto cap_entry = metadata->find(ANDROID_REQUEST_AVAILABLE_CAPABILITIES);
  if (std::find(cap_entry.data.u8, cap_entry.data.u8 + cap_entry.count,
                ANDROID_REQUEST_AVAILABLE_CAPABILITIES_YUV_REPROCESSING) ==
      cap_entry.data.u8 + cap_entry.count) {
    return;
  }

  std::vector<uint32_t> vendor_tags(GetTagCount(this));
  GetAllTags(this, vendor_tags.data());
  std::vector<int32_t> key_tags(
      {ANDROID_REQUEST_AVAILABLE_REQUEST_KEYS,
       ANDROID_REQUEST_AVAILABLE_RESULT_KEYS,
       ANDROID_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS});
  for (const auto& tag : key_tags) {
    auto entry = metadata->find(tag);
    DCHECK_NE(entry.count, 0);
    std::vector<int32_t> keys(entry.data.i32, entry.data.i32 + entry.count);
    keys.insert(keys.end(), vendor_tags.begin(), vendor_tags.end());
    if (metadata->update(tag, keys.data(), keys.size()) != 0) {
      LOGF(ERROR) << "Failed to add vendor tags to "
                  << get_camera_metadata_tag_name(tag);
    }
  }

  // Update vendor tag default values into camera characteristics
  for (const auto& it : vendor_tag_effect_info_map_) {
    const VendorTagInfo& tag_info = it.second.vendor_tag_info;
    switch (tag_info.type) {
      case TYPE_BYTE:
        metadata->update(it.first, &tag_info.data.u8, 1);
        break;
      case TYPE_INT32:
        metadata->update(it.first, &tag_info.data.i32, 1);
        break;
      case TYPE_FLOAT:
        metadata->update(it.first, &tag_info.data.f, 1);
        break;
      case TYPE_INT64:
        metadata->update(it.first, &tag_info.data.i64, 1);
        break;
      case TYPE_DOUBLE:
        metadata->update(it.first, &tag_info.data.d, 1);
        break;
      case TYPE_RATIONAL:
        metadata->update(it.first, &tag_info.data.r, 1);
        break;
      default:
        NOTREACHED() << "Invalid vendor tag type";
    }
  }
}

int32_t ReprocessEffectManager::ReprocessRequest(
    const camera_metadata_t& settings,
    ScopedYUVBufferHandle* input_buffer,
    uint32_t width,
    uint32_t height,
    android::CameraMetadata* result_metadata,
    ScopedYUVBufferHandle* output_buffer) {
  VLOGF_ENTER();
  if (!input_buffer || !*input_buffer || !output_buffer || !*output_buffer) {
    return -EINVAL;
  }
  uint32_t orientation = 0;
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(&settings, ANDROID_JPEG_ORIENTATION,
                                    &entry) == 0) {
    orientation = entry.data.i32[0];
  }
  // TODO(hywu): enable cascading effects
  for (uint32_t tag = kReprocessEffectVendorTagStart; tag < next_vendor_tag_;
       tag++) {
    if (find_camera_metadata_ro_entry(&settings, tag, &entry) == 0) {
      DCHECK(vendor_tag_effect_info_map_.find(tag) !=
             vendor_tag_effect_info_map_.end());
      if (!vendor_tag_effect_info_map_.at(tag).effect) {
        LOGF(WARNING) << "Received result vendor tag 0x" << std::hex << tag
                      << " in request";
        continue;
      }
      int result = 0;
      uint32_t v4l2_format =
          buffer_manager_->GetV4L2PixelFormat(*output_buffer->GetHandle());
      result = vendor_tag_effect_info_map_.at(tag).effect->ReprocessRequest(
          settings, input_buffer, width, height, orientation, v4l2_format,
          result_metadata, output_buffer);
      if (result != 0) {
        LOGF(ERROR) << "Failed to handle reprocess request on vendor tag 0x"
                    << std::hex << tag;
      }
      return result;
    }
  }
  return -ENOENT;
}

// static
int ReprocessEffectManager::GetTagCount(const vendor_tag_ops_t* v) {
  auto self = static_cast<const ReprocessEffectManager*>(v);
  return self->vendor_tag_effect_info_map_.size();
}

// static
void ReprocessEffectManager::GetAllTags(const vendor_tag_ops_t* v,
                                        uint32_t* tag_array) {
  auto self = static_cast<const ReprocessEffectManager*>(v);
  uint32_t* ptr = tag_array;
  for (auto it : self->vendor_tag_effect_info_map_) {
    *ptr++ = it.first;
  }
}

// static
const char* ReprocessEffectManager::GetSectionName(const vendor_tag_ops_t* v,
                                                   uint32_t tag) {
  auto self = static_cast<const ReprocessEffectManager*>(v);
  auto it = self->vendor_tag_effect_info_map_.find(tag);
  if (it == self->vendor_tag_effect_info_map_.end()) {
    return nullptr;
  }
  return kVendorGoogleSectionName;
}

// static
const char* ReprocessEffectManager::GetTagName(const vendor_tag_ops_t* v,
                                               uint32_t tag) {
  auto self = static_cast<const ReprocessEffectManager*>(v);
  auto it = self->vendor_tag_effect_info_map_.find(tag);
  if (it == self->vendor_tag_effect_info_map_.end()) {
    return nullptr;
  }
  return it->second.vendor_tag_info.name;
}

// static
int ReprocessEffectManager::GetTagType(const vendor_tag_ops_t* v,
                                       uint32_t tag) {
  auto self = static_cast<const ReprocessEffectManager*>(v);
  auto it = self->vendor_tag_effect_info_map_.find(tag);
  if (it == self->vendor_tag_effect_info_map_.end()) {
    return -1;
  }
  return it->second.vendor_tag_info.type;
}

}  // namespace cros
