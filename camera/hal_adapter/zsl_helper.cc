/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/zsl_helper.h"

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <camera/camera_metadata.h>
#include <sync/sync.h>
#include <system/camera_metadata.h>

#include "cros-camera/common.h"

namespace cros {

ZslBuffer::ZslBuffer()
    : metadata_ready(false), buffer_ready(false), selected(false) {}
ZslBuffer::ZslBuffer(uint32_t frame_number,
                     const camera3_stream_buffer_t& buffer)
    : frame_number(frame_number),
      buffer(buffer),
      metadata_ready(false),
      buffer_ready(false),
      selected(false) {}

ZslBufferManager::ZslBufferManager()
    : initialized_(false),
      buffer_manager_(CameraBufferManager::GetInstance()) {}

ZslBufferManager::~ZslBufferManager() {
  if (free_buffers_.size() != buffer_pool_.size()) {
    LOGF(WARNING) << "Not all ZSL buffers have been released";
  }
  for (auto& buffer : buffer_pool_) {
    buffer_manager_->Free(buffer);
  }
}

bool ZslBufferManager::Initialize(size_t pool_size,
                                  camera3_stream_t* output_stream) {
  output_stream_ = output_stream;

  buffer_pool_.reserve(pool_size);
  base::AutoLock buffer_pool_lock(buffer_pool_lock_);
  for (size_t i = 0; i < pool_size; ++i) {
    uint32_t stride;
    buffer_handle_t buffer;
    if (buffer_manager_->Allocate(output_stream_->width, output_stream_->height,
                                  ZslHelper::kZslPixelFormat,
                                  GRALLOC_USAGE_HW_CAMERA_ZSL |
                                      GRALLOC_USAGE_SW_READ_OFTEN |
                                      GRALLOC_USAGE_SW_WRITE_OFTEN,
                                  cros::GRALLOC, &buffer, &stride) != 0) {
      LOGF(ERROR) << "Failed to allocate buffer";
      // Free previously-allocated buffers.
      for (auto& buffer : buffer_pool_) {
        buffer_manager_->Free(buffer);
      }
      return false;
    }
    buffer_pool_.push_back(buffer);
    free_buffers_.push(&buffer_pool_.back());
    buffer_to_buffer_pointer_map_[buffer] = &buffer_pool_.back();
  }

  initialized_ = true;
  return true;
}

buffer_handle_t* ZslBufferManager::GetBuffer() {
  base::AutoLock buffer_pool_lock(buffer_pool_lock_);
  if (!initialized_) {
    LOGF(ERROR) << "ZSL buffer manager has not been initialized";
    return nullptr;
  }
  if (free_buffers_.empty()) {
    LOGF(ERROR) << "No more buffer left in the pool. This shouldn't happen";
    return nullptr;
  }

  buffer_handle_t* buffer = free_buffers_.front();
  free_buffers_.pop();
  return buffer;
}

bool ZslBufferManager::ReleaseBuffer(buffer_handle_t buffer_to_release) {
  base::AutoLock buffer_pool_lock(buffer_pool_lock_);
  if (!initialized_) {
    LOGF(ERROR) << "ZSL buffer manager has not been initialized";
    return false;
  }
  auto it = buffer_to_buffer_pointer_map_.find(buffer_to_release);
  if (it == buffer_to_buffer_pointer_map_.end()) {
    LOGF(ERROR) << "The released buffer doesn't belong to ZSL buffer manager";
    return false;
  }
  free_buffers_.push(it->second);
  return true;
}

ZslHelper::ZslHelper(const camera_metadata_t* static_info,
                     FrameNumberMapper* mapper)
    : initialized_(false),
      enabled_(false),
      fence_sync_thread_("FenceSyncThread"),
      frame_number_mapper_(mapper) {
  VLOGF_ENTER();
  if (IsCapabilitySupported(static_info, kZslCapability)) {
    uint32_t bi_width, bi_height;
    if (SelectZslStreamSize(static_info, &bi_width, &bi_height)) {
      LOGF(INFO) << "Selected ZSL stream size: " << bi_width << "x"
                 << bi_height;
      // Create ZSL streams
      bi_stream_ = std::make_unique<camera3_stream_t>();
      memset(bi_stream_.get(), 0, sizeof(*bi_stream_.get()));
      bi_stream_->stream_type = CAMERA3_STREAM_BIDIRECTIONAL;
      bi_stream_->width = bi_width;
      bi_stream_->height = bi_height;
      bi_stream_->format = kZslPixelFormat;

      // Initialize ZSL buffer manager
      uint8_t max_pipeline_depth = [&]() {
        camera_metadata_ro_entry entry;
        if (find_camera_metadata_ro_entry(
                static_info, ANDROID_REQUEST_PIPELINE_MAX_DEPTH, &entry) != 0) {
          LOGF(ERROR) << "ANDROID_REQUEST_PIPELINE_MAX_DEPTH is missing from "
                         "static metadata!";
          // This shouldn't happen, but assigning this a value just in case.
          return static_cast<uint8_t>(20);
        }
        return entry.data.u8[0];
      }();
      if (zsl_buffer_manager_.Initialize(kZslBufferSize + max_pipeline_depth,
                                         bi_stream_.get())) {
        initialized_ = true;
      } else {
        LOGF(ERROR) << "Failed to initialize ZSL buffer manager";
      }
    } else {
      LOGF(ERROR) << "Failed to select stream sizes for ZSL.";
    }
  } else {
    LOGF(INFO) << "Device doesn't support ZSL. ZSL won't be enabled.";
  }
  if (!fence_sync_thread_.Start()) {
    LOGF(ERROR) << "Fence sync thread failed to start";
    initialized_ = false;
  }
  partial_result_count_ = [&]() {
    camera_metadata_ro_entry entry;
    if (find_camera_metadata_ro_entry(
            static_info, ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &entry) != 0) {
      return 1;
    }
    return entry.data.i32[0];
  }();
  max_num_input_streams_ = [&]() {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(
            static_info, ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS, &entry) != 0) {
      LOGF(ERROR) << "Failed to get maximum number of input streams.";
      return 0;
    }
    return entry.data.i32[0];
  }();
  timestamp_source_ = [&]() {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(
            static_info, ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE, &entry) != 0) {
      LOGF(ERROR) << "Failed to get timestamp source. Assuming it's UNKNOWN.";
      return ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN;
    }
    return static_cast<
        camera_metadata_enum_android_sensor_info_timestamp_source_t>(
        entry.data.u8[0]);
  }();
}

ZslHelper::~ZslHelper() {
  fence_sync_thread_.Stop();
}

bool ZslHelper::IsZslEnabled() {
  base::AutoLock enabled_lock(enabled_lock_);
  return enabled_;
}

void ZslHelper::SetZslEnabled(bool enabled) {
  base::AutoLock enabled_lock(enabled_lock_);
  if (enabled != enabled_) {
    LOGF(INFO) << (enabled ? "Enabling" : "Disabling") << " ZSL";
    enabled_ = enabled;
  }
}

bool ZslHelper::CanEnableZsl(const internal::ScopedStreams& streams) {
  size_t num_input_streams = 0;
  bool has_zsl_output_stream = false;
  bool has_blob_output_stream = false;
  for (auto it = streams.begin(); it != streams.end(); it++) {
    camera3_stream_t* stream = it->second.get();
    if (stream->stream_type == CAMERA3_STREAM_INPUT ||
        stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL) {
      num_input_streams++;
    }
    if (stream->stream_type == CAMERA3_STREAM_OUTPUT ||
        stream->stream_type == CAMERA3_STREAM_BIDIRECTIONAL) {
      if (stream->format == HAL_PIXEL_FORMAT_BLOB ||
          (stream->usage & GRALLOC_USAGE_STILL_CAPTURE)) {
        has_blob_output_stream = true;
      }
      if ((stream->usage & GRALLOC_USAGE_HW_CAMERA_ZSL) ==
          GRALLOC_USAGE_HW_CAMERA_ZSL) {
        has_zsl_output_stream = true;
      }
    }
  }
  return initialized_  // Initialized means we have an allocated buffer pool.
         && has_blob_output_stream  // Has a stream for still capture.
         && num_input_streams < max_num_input_streams_  // Has room for an extra
                                                        // input stream for ZSL.
         && !has_zsl_output_stream;  // HAL doesn't support multiple raw output
                                     // streams.
}

void ZslHelper::AttachZslStream(camera3_stream_configuration_t* stream_list,
                                std::vector<camera3_stream_t*>* streams) {
  stream_list->num_streams++;
  streams->push_back(bi_stream_.get());
  // There could be memory reallocation happening after the push_back call.
  stream_list->streams = streams->data();

  for (size_t i = 0; i < stream_list->num_streams; ++i) {
    // GRALLOC_USAGE_STILL_CAPTURE is a private usage from VCD.
    // We set the usage flag here to let VCD know ZSL is enabled.
    if ((*streams)[i]->usage & GRALLOC_USAGE_STILL_CAPTURE) {
      (*streams)[i]->usage |= GRALLOC_USAGE_ZSL_ENABLED;
    }
  }

  VLOGF(1) << "Attached ZSL streams. The list of streams after attaching:";
  for (size_t i = 0; i < stream_list->num_streams; ++i) {
    VLOGF(1) << "i = " << i
             << ", type = " << stream_list->streams[i]->stream_type
             << ", size = " << stream_list->streams[i]->width << "x"
             << stream_list->streams[i]->height
             << ", format = " << stream_list->streams[i]->format;
  }
}

bool ZslHelper::IsZslRequested(camera_metadata_t* settings) {
  bool enable_zsl = [&]() {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(settings, ANDROID_CONTROL_ENABLE_ZSL,
                                      &entry) == 0) {
      return static_cast<bool>(entry.data.u8[0]);
    }
    return false;
  }();
  if (!enable_zsl) {
    return false;
  }
  // We can only enable ZSL when capture intent is also still capture.
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(settings, ANDROID_CONTROL_CAPTURE_INTENT,
                                    &entry) == 0) {
    return entry.data.u8[0] == ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE ||
           entry.data.u8[0] == ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG;
  }
  return false;
}

bool ZslHelper::IsAttachedZslFrame(uint32_t frame_number) {
  base::AutoLock ring_buffer_lock(ring_buffer_lock_);
  return buffer_index_map_.find(frame_number) != buffer_index_map_.end();
}

bool ZslHelper::IsAttachedZslBuffer(const camera3_stream_buffer_t* buffer) {
  return buffer && buffer->stream == bi_stream_.get();
}

bool ZslHelper::IsTransformedZslBuffer(const camera3_stream_buffer_t* buffer) {
  return buffer && buffer->stream == bi_stream_.get();
}

void ZslHelper::ProcessZslCaptureRequest(
    uint32_t framework_frame_number,
    camera3_capture_request_t* request,  // maybe just use *input_buffer?
    std::vector<camera3_stream_buffer_t>* output_buffers,
    internal::ScopedCameraMetadata* settings,
    camera3_capture_request_t* still_request,
    std::vector<camera3_stream_buffer_t>* still_output_buffers,
    SelectionStrategy strategy) {
  auto GetJpegOrientation = [&](const camera_metadata_t* settings) {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(settings, ANDROID_JPEG_ORIENTATION,
                                      &entry) != 0) {
      LOGF(ERROR) << "Failed to find JPEG orientation, defaulting to 0";
      return 0;
    }
    return *entry.data.i32;
  };
  if (request->input_buffer != nullptr) {
    return;
  }
  if (IsZslRequested(settings->get())) {
    for (auto it = output_buffers->begin(); it != output_buffers->end();) {
      if (it->stream->format == HAL_PIXEL_FORMAT_BLOB ||
          (it->stream->usage & GRALLOC_USAGE_STILL_CAPTURE)) {
        still_output_buffers->push_back(std::move(*it));
        it = output_buffers->erase(it);
      } else {
        it++;
      }
    }
    if (still_output_buffers->empty()) {
      LOGF(ERROR) << "ZSL is requested, but we couldn't find any still "
                     "capture output buffers.";
    } else {
      camera_metadata_t* zsl_settings;
      bool transformed =
          TransformRequest(still_request, &zsl_settings,
                           GetJpegOrientation(settings->get()), strategy);
      if (transformed) {
        still_request->frame_number =
            frame_number_mapper_->GetHalFrameNumber(framework_frame_number);
        still_request->settings = zsl_settings;
      } else {
        // TODO(lnishan): Implement 3A stabilization mechanism so that we
        // would attempt to get a buffer in another time.
        // Merging the buffers back for now.
        LOGF(ERROR) << "Not splitting this request because we cannot find a "
                       "suitable ZSL buffer";
        for (size_t i = 0; i < still_output_buffers->size(); ++i) {
          output_buffers->push_back(std::move((*still_output_buffers)[i]));
        }
        still_output_buffers->clear();
      }
    }
    still_request->num_output_buffers = still_output_buffers->size();
    still_request->output_buffers = const_cast<const camera3_stream_buffer_t*>(
        still_output_buffers->data());
  }

  // We might end up moving all output buffers to the added request. So here we
  // unconditionally add a ZSL output buffer. We also need a placeholder request
  // so that we can defer a request if a suitable ZSL buffer is not found.
  AttachRequest(request, output_buffers);
}

void ZslHelper::AttachRequest(
    camera3_capture_request_t* request,
    std::vector<camera3_stream_buffer_t>* output_buffers) {
  VLOGF_ENTER();
  if (!enabled_) {
    LOGF(WARNING) << "Trying to attach a request when ZSL is disabled";
    return;
  }
  // Check if the oldest ZSL buffer is filled and free it if it's filled and
  // not selected for any transformed ZSL requests.
  base::AutoLock l(ring_buffer_lock_);
  if (ring_buffer_.IsFilledIndex(0)) {
    const ZslBuffer& buf = ring_buffer_.ReadBuffer(0);
    if (!buf.selected) {
      // We can free the buffer if it's not selected.
      if (!zsl_buffer_manager_.ReleaseBuffer(*buf.buffer.buffer)) {
        LOGF(ERROR) << "Unable to release the oldest buffer";
      }
    }
    // No need to remember frame-to-index mapping when this buffer is popped.
    buffer_index_map_.erase(buf.frame_number);
  }

  // Attach our ZSL output buffer.
  camera3_stream_buffer_t stream_buffer;
  stream_buffer.buffer = zsl_buffer_manager_.GetBuffer();
  stream_buffer.stream = bi_stream_.get();
  stream_buffer.acquire_fence = stream_buffer.release_fence = -1;

  buffer_index_map_[request->frame_number] = ring_buffer_.CurrentIndex();
  ZslBuffer buffer(request->frame_number, stream_buffer);
  ring_buffer_.SaveToBuffer(std::move(buffer));

  output_buffers->push_back(std::move(stream_buffer));
  request->num_output_buffers++;
}

bool ZslHelper::TransformRequest(camera3_capture_request_t* request,
                                 camera_metadata_t** settings,
                                 int32_t jpeg_orientation,
                                 SelectionStrategy strategy) {
  VLOGF_ENTER();
  if (!enabled_) {
    LOGF(WARNING) << "Trying to transform a request when ZSL is disabled";
    return false;
  }

  // Select the best buffer.
  ZslBuffer* selected_buffer = SelectZslBuffer(strategy);
  if (!selected_buffer) {
    LOGF(WARNING) << "Unable to find a suitable ZSL buffer. Request will not "
                     "be transformed.";
    return false;
  }

  LOGF(INFO) << "Transforming request into ZSL reprocessing request";
  base::AutoLock ring_buffer_lock(ring_buffer_lock_);
  request->input_buffer = &selected_buffer->buffer;
  request->input_buffer->stream = bi_stream_.get();
  request->input_buffer->acquire_fence = -1;
  request->input_buffer->release_fence = -1;

  // The result metadata for the RAW buffers come from the preview frames. We
  // need to add JPEG orientation back so that the resulting JPEG is of the
  // correct orientation.
  if (selected_buffer->metadata.update(ANDROID_JPEG_ORIENTATION,
                                       &jpeg_orientation, 1) != 0) {
    LOGF(ERROR) << "Failed to update JPEG_ORIENTATION";
  }
  // Note that camera device adapter would take ownership of this pointer.
  *settings = selected_buffer->metadata.release();
  return true;
}

void ZslHelper::ProcessZslCaptureResult(
    const camera3_capture_result_t* result,
    const camera3_stream_buffer_t** attached_output,
    const camera3_stream_buffer_t** transformed_input) {
  VLOGF_ENTER();
  for (size_t i = 0; i < result->num_output_buffers; ++i) {
    if (IsAttachedZslBuffer(&result->output_buffers[i])) {
      *attached_output = &result->output_buffers[i];
      break;
    }
  }
  if (result->input_buffer && IsTransformedZslBuffer(result->input_buffer)) {
    *transformed_input = result->input_buffer;
    ReleaseStreamBuffer(*result->input_buffer);
  }
  if (IsAttachedZslFrame(result->frame_number)) {
    base::AutoLock ring_buffer_lock(ring_buffer_lock_);
    auto it = buffer_index_map_.find(result->frame_number);
    if (it != buffer_index_map_.end()) {
      ZslBuffer* buffer = MutableReadBufferByBufferIndex(it->second);

      for (size_t i = 0; i < result->num_output_buffers; ++i) {
        if (result->output_buffers[i].stream == bi_stream_.get()) {
          WaitAttachedFrame(result->frame_number,
                            result->output_buffers[i].release_fence);
          break;
        }
      }

      if (result->partial_result != 0) {  // Result has metadata
        // Merge the result metadata.
        if (buffer) {
          buffer->metadata.append(result->result);
          if (result->partial_result == partial_result_count_) {
            buffer->metadata_ready = true;
          }
        }
      }
    }
  }
}

void ZslHelper::WaitAttachedFrame(uint32_t frame_number, int release_fence) {
  fence_sync_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&ZslHelper::WaitAttachedFrameOnFenceSyncThread,
                 base::Unretained(this), frame_number, release_fence));
}

void ZslHelper::WaitAttachedFrameOnFenceSyncThread(uint32_t frame_number,
                                                   int release_fence) {
  if (release_fence != -1 &&
      sync_wait(release_fence, ZslHelper::kZslSyncWaitTimeoutMs)) {
    LOGF(WARNING) << "Failed to wait for release fence on attached ZSL buffer";
  } else {
    base::AutoLock ring_buffer_lock(ring_buffer_lock_);
    auto it = buffer_index_map_.find(frame_number);
    if (it != buffer_index_map_.end()) {
      ZslBuffer* buffer = MutableReadBufferByBufferIndex(it->second);
      if (buffer) {
        buffer->buffer_ready = true;
      }
    }
    return;
  }
  fence_sync_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&ZslHelper::WaitAttachedFrameOnFenceSyncThread,
                 base::Unretained(this), frame_number, release_fence));
}

void ZslHelper::ReleaseStreamBuffer(camera3_stream_buffer_t buffer) {
  fence_sync_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&ZslHelper::ReleaseStreamBufferOnFenceSyncThread,
                            base::Unretained(this), base::Passed(&buffer)));
}

void ZslHelper::ReleaseStreamBufferOnFenceSyncThread(
    camera3_stream_buffer_t buffer) {
  if (buffer.release_fence != -1 &&
      sync_wait(buffer.release_fence, ZslHelper::kZslSyncWaitTimeoutMs)) {
    LOGF(WARNING) << "Failed to wait for release fence on ZSL input buffer";
  } else {
    if (!zsl_buffer_manager_.ReleaseBuffer(*buffer.buffer)) {
      LOGF(ERROR) << "Failed to release this stream buffer";
    }
    // The above error should only happen when the mapping in buffer manager
    // becomes invalid somwhow. It's not recoverable, so we don't retry here.
    return;
  }
  fence_sync_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&ZslHelper::ReleaseStreamBufferOnFenceSyncThread,
                            base::Unretained(this), base::Passed(&buffer)));
}

bool ZslHelper::IsCapabilitySupported(const camera_metadata_t* static_info,
                                      uint8_t capability) {
  camera_metadata_ro_entry_t entry;
  if (find_camera_metadata_ro_entry(
          static_info, ANDROID_REQUEST_AVAILABLE_CAPABILITIES, &entry) == 0) {
    return std::find(entry.data.u8, entry.data.u8 + entry.count, capability) !=
           entry.data.u8 + entry.count;
  }
  return false;
}

bool ZslHelper::SelectZslStreamSize(const camera_metadata_t* static_info,
                                    uint32_t* bi_width,
                                    uint32_t* bi_height) {
  VLOGF_ENTER();
  camera_metadata_ro_entry entry;
  if (find_camera_metadata_ro_entry(
          static_info, ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
          &entry) != 0) {
    LOGF(ERROR) << "Failed to find stream configurations map";
    return false;
  }
  *bi_width = 0;
  *bi_height = 0;

  VLOGF(1) << "Iterating stream configuration map for ZSL streams";
  for (size_t i = 0; i < entry.count; i += 4) {
    const int32_t& format = entry.data.i32[i + STREAM_CONFIG_FORMAT_INDEX];
    if (format != kZslPixelFormat)
      continue;
    const int32_t& width = entry.data.i32[i + STREAM_CONFIG_WIDTH_INDEX];
    const int32_t& height = entry.data.i32[i + STREAM_CONFIG_HEIGHT_INDEX];
    const int32_t& direction =
        entry.data.i32[i + STREAM_CONFIG_DIRECTION_INDEX];
    VLOGF(1) << "format = " << format << ", "
             << "width = " << width << ", "
             << "height = " << height << ", "
             << "direction = " << direction;
    if (direction == ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS_INPUT) {
      if (width * height > (*bi_width) * (*bi_height)) {
        *bi_width = width;
        *bi_height = height;
      }
    }
  }
  return *bi_width > 0 && *bi_height > 0;
}

ZslBuffer* ZslHelper::SelectZslBuffer(SelectionStrategy strategy) {
  // Select the best ZSL buffer based on time and statistics.
  auto GetTimestamp = [](const android::CameraMetadata& android_metadata) {
    camera_metadata_ro_entry_t entry;
    if (android_metadata.exists(ANDROID_SENSOR_TIMESTAMP)) {
      entry = android_metadata.find(ANDROID_SENSOR_TIMESTAMP);
      return entry.data.i64[0];
    }
    LOGF(ERROR) << "Cannot find sensor timestamp in ZSL buffer";
    return static_cast<int64_t>(-1);
  };
  base::AutoLock ring_buffer_lock(ring_buffer_lock_);
  if (strategy == LAST_SUBMITTED) {
    for (int i = kZslBufferSize - 1; i >= 0; --i) {
      if (!ring_buffer_.IsFilledIndex(i)) {
        continue;
      }
      ZslBuffer* buffer = ring_buffer_.MutableReadBuffer(i);
      if (buffer->metadata_ready && buffer->buffer_ready && !buffer->selected) {
        buffer->selected = true;
        return buffer;
      }
    }
    LOGF(WARNING) << "Failed to find a unselected submitted ZSL buffer";
    return nullptr;
  }

  // For CLOSEST or CLOSEST_3A strategies.
  int64_t cur_timestamp = GetCurrentTimestamp();
  LOGF(INFO) << "Current timestamp = " << cur_timestamp;
  ZslBuffer* selected_buffer = nullptr;
  int64_t min_diff = kZslLookbackNs;
  int64_t ideal_timestamp = cur_timestamp - kZslLookbackNs;
  for (int i = kZslBufferSize - 1; i >= 0; --i) {
    if (!ring_buffer_.IsFilledIndex(i)) {
      continue;
    }
    ZslBuffer* buffer = ring_buffer_.MutableReadBuffer(i);
    if (!buffer->metadata_ready || !buffer->buffer_ready || buffer->selected) {
      continue;
    }
    int64_t timestamp = GetTimestamp(buffer->metadata);
    bool satisfy_3a = strategy == CLOSEST || (strategy == CLOSEST_3A &&
                                              Is3AConverged(buffer->metadata));
    int64_t diff = timestamp - ideal_timestamp;
    VLOGF(1) << "Candidate timestamp = " << timestamp
             << " (Satisfy 3A = " << satisfy_3a << ", "
             << "Difference from desired timestamp = " << diff << ")";
    if (diff > kZslLookbackLengthNs) {
      continue;
    } else if (diff < 0) {
      // We don't select buffers that are older than what is displayed.
      break;
    }
    if (satisfy_3a) {
      if (diff < min_diff) {
        min_diff = diff;
        selected_buffer = buffer;
      } else {
        // Not possible to find a better buffer
        break;
      }
    }
  }
  if (selected_buffer == nullptr) {
    LOGF(WARNING)
        << "Failed to a find suitable ZSL buffer with the given strategy";
    return nullptr;
  }
  LOGF(INFO) << "Timestamp of the selected buffer = "
             << GetTimestamp(selected_buffer->metadata);
  selected_buffer->selected = true;
  return selected_buffer;
}

int64_t ZslHelper::GetCurrentTimestamp() {
  struct timespec t = {};
  clock_gettime(
      timestamp_source_ == ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_UNKNOWN
          ? CLOCK_MONOTONIC
          : CLOCK_BOOTTIME /* ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE_REALTIME */,
      &t);
  return static_cast<int64_t>(t.tv_sec) * 1000000000LL + t.tv_nsec;
}

bool ZslHelper::Is3AConverged(const android::CameraMetadata& android_metadata) {
  auto GetState = [&](size_t tag) {
    camera_metadata_ro_entry_t entry;
    if (android_metadata.exists(tag)) {
      entry = android_metadata.find(tag);
      return entry.data.u8[0];
    }
    LOGF(ERROR) << "Cannot find the metadata for "
                << get_camera_metadata_tag_name(tag);
    return static_cast<uint8_t>(0);
  };
  uint8_t ae_mode = GetState(ANDROID_CONTROL_AE_MODE);
  uint8_t ae_state = GetState(ANDROID_CONTROL_AE_STATE);
  bool ae_converged = [&]() {
    if (ae_mode != ANDROID_CONTROL_AE_MODE_OFF) {
      if (ae_state != ANDROID_CONTROL_AE_STATE_CONVERGED &&
          ae_state != ANDROID_CONTROL_AE_STATE_FLASH_REQUIRED &&
          ae_state != ANDROID_CONTROL_AE_STATE_LOCKED) {
        return false;
      }
    }
    return true;
  }();
  if (!ae_converged) {
    return false;
  }
  uint8_t af_mode = GetState(ANDROID_CONTROL_AF_MODE);
  uint8_t af_state = GetState(ANDROID_CONTROL_AF_STATE);
  bool af_converged = [&]() {
    if (af_mode != ANDROID_CONTROL_AF_MODE_OFF) {
      if (af_state != ANDROID_CONTROL_AF_STATE_PASSIVE_FOCUSED &&
          af_state != ANDROID_CONTROL_AF_STATE_FOCUSED_LOCKED) {
        return false;
      }
    }
    return true;
  }();
  if (!af_converged) {
    return false;
  }
  uint8_t awb_mode = GetState(ANDROID_CONTROL_AWB_MODE);
  uint8_t awb_state = GetState(ANDROID_CONTROL_AWB_STATE);
  bool awb_converged = [&]() {
    if (awb_mode != ANDROID_CONTROL_AWB_MODE_OFF) {
      if (awb_state != ANDROID_CONTROL_AWB_STATE_CONVERGED &&
          awb_state != ANDROID_CONTROL_AWB_STATE_LOCKED) {
        return false;
      }
    }
    return true;
  }();
  // We won't reach here if neither AE nor AF is converged.
  return awb_converged;
}

ZslBuffer* ZslHelper::MutableReadBufferByBufferIndex(size_t buffer_index) {
  size_t current_index = ring_buffer_.CurrentIndex();
  DCHECK(current_index > buffer_index);
  // The DCHECK fails if the caller is attempting to update a buffer that's no
  // longer in the ring buffer. This means a capture result is returned after
  // |kZslBufferSize| frames, which shouldn't happen and likely indicates
  // something is wrong.
  DCHECK(current_index - buffer_index <= ZslHelper::kZslBufferSize);
  if (current_index - buffer_index <= ZslHelper::kZslBufferSize) {
    size_t rel_index =
        ZslHelper::kZslBufferSize - (current_index - buffer_index);
    return ring_buffer_.MutableReadBuffer(rel_index);
  }
  return nullptr;
}

}  // namespace cros
