/*
 * Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_ZSL_HELPER_H_
#define CAMERA_HAL_ADAPTER_ZSL_HELPER_H_

#include <map>
#include <memory>
#include <queue>
#include <utility>
#include <vector>

#include <hardware/camera3.h>
#include <time.h>

#include <base/containers/ring_buffer.h>
#include <base/synchronization/lock.h>
#include <camera/camera_metadata.h>
#include <system/camera_metadata.h>
#include "common/utils/common_types.h"

#include "cros-camera/camera_buffer_manager.h"
#include "hal_adapter/frame_number_mapper.h"

namespace cros {

const int GRALLOC_USAGE_STILL_CAPTURE = GRALLOC_USAGE_PRIVATE_1;
const int GRALLOC_USAGE_ZSL_ENABLED = GRALLOC_USAGE_PRIVATE_2;

struct ZslBuffer {
 public:
  ZslBuffer();
  explicit ZslBuffer(uint32_t frame_number,
                     const camera3_stream_buffer_t& buffer);

  // The frame number associated with this buffer.
  uint32_t frame_number;

  // Metadata of this buffer.
  android::CameraMetadata metadata;

  // The underlying stream buffer for this buffer.
  camera3_stream_buffer_t buffer;

  // Whether all metadata have been returned.
  bool metadata_ready;

  // Whether the buffer has been returned.
  bool buffer_ready;

  // Whether buffer is selected for reprocessing. selected is false by default,
  // and true when the buffer is selected. All buffers that are not selected
  // are freed when popped out.
  bool selected;
};

class ZslBufferManager {
 public:
  ZslBufferManager();
  ~ZslBufferManager();

  // Initializes a ZSL buffer manager with a pool size of |pool_size| and
  // output stream set to |output_stream|.
  bool Initialize(size_t pool_size, camera3_stream_t* output_stream);

  // Gets a buffer from the buffer pool.
  buffer_handle_t* GetBuffer();

  // Releases a buffer to the buffer pool.
  bool ReleaseBuffer(buffer_handle_t buffer_to_release);

 private:
  // Whether manager is initialized. True if all buffers in buffer pool have
  // been successfully allocated.
  bool initialized_;

  // The buffer manager that allocates and frees the buffer handles.
  CameraBufferManager* buffer_manager_;

  // The buffer pool that stores all the buffers previously allocated. The size
  // of the vector is constant to ensure all buffer_handle_t* stay constant.
  std::vector<buffer_handle_t> buffer_pool_;

  // Stores all the free buffers available for use.
  std::queue<buffer_handle_t*> free_buffers_;

  // A mapping from buffer_handle_t to the buffer_handle_t* pointing to the
  // corresponding entry in |buffer_pool_|.
  std::map<buffer_handle_t, buffer_handle_t*> buffer_to_buffer_pointer_map_;

  // The lock that protects all buffer pool structures (|buffer_pool_|,
  // |free_buffers_|, |buffer_to_buffer_pointer_map_|.
  base::Lock buffer_pool_lock_;

  // The ZSL output stream.
  camera3_stream_t* output_stream_;
};

class ZslHelper {
 public:
  // TODO(lnishan): Replace the ring buffer with a variable-length data
  // structure and adjust the buffer size by the lookback time.
  static const size_t kZslBufferSize = 20;
  static const int kZslSyncWaitTimeoutMs = 3;
  static const int kZslPixelFormat = HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED;
  static const uint8_t kZslCapability =
      ANDROID_REQUEST_AVAILABLE_CAPABILITIES_PRIVATE_REPROCESSING;
  // |kZslLoobackNs| accounts for the display latency (i.e., the time it takes
  // for a buffer to be drawn on screen since its start of exposure).
  // TODO(lnishan): Make this a chromeos-config entry and read the
  // configuration during runtime.
  static const int64_t kZslLookbackNs = 420'000'000;        // 420ms
  static const int64_t kZslLookbackLengthNs = 150'000'000;  // 150ms
  enum {
    STREAM_CONFIG_FORMAT_INDEX,
    STREAM_CONFIG_WIDTH_INDEX,
    STREAM_CONFIG_HEIGHT_INDEX,
    STREAM_CONFIG_DIRECTION_INDEX
  };
  enum SelectionStrategy { LAST_SUBMITTED, CLOSEST, CLOSEST_3A };

  // Initialize static metadata and ZSL ring buffer.
  explicit ZslHelper(const camera_metadata_t* static_info,
                     FrameNumberMapper* mapper);

  ~ZslHelper();

  // Whether ZSL is enabled for the device adapter that owns this helper.
  bool IsZslEnabled();

  // Enable or disable ZSL.
  void SetZslEnabled(bool enabled);

  // Whether we can enable ZSL with the list of streams being configured.
  bool CanEnableZsl(const internal::ScopedStreams& streams);

  // Attaches the ZSL bidirectional stream to the stream configuration.
  void AttachZslStream(camera3_stream_configuration_t* stream_list,
                       std::vector<camera3_stream_t*>* streams);

  // Processes a capture request by either attaching a RAW output buffer (for
  // queueing the ZSL ring buffer) or transforming the request by adding a RAW
  // input stream.
  void ProcessZslCaptureRequest(
      uint32_t framework_frame_number,
      camera3_capture_request_t* request,
      std::vector<camera3_stream_buffer_t>* output_buffers,
      internal::ScopedCameraMetadata* settings,
      camera3_capture_request_t* still_request,
      std::vector<camera3_stream_buffer_t>* still_output_buffers,
      SelectionStrategy strategy = CLOSEST_3A);

  // Merges ZSL metadata and mark buffer as ready to be submitted.
  void ProcessZslCaptureResult(
      const camera3_capture_result_t* result,
      const camera3_stream_buffer_t** attached_output,
      const camera3_stream_buffer_t** transformed_input);

 private:
  // Whether ZSL is enabled for this capture request.
  // Note that this function deletes the ANDROID_CONTROL_ENABLE_ZSL if
  // delete_entry is true.
  bool IsZslRequested(camera_metadata_t* settings);

  // Whether frame_number belongs to an attached ZSL request.
  bool IsAttachedZslFrame(uint32_t frame_number);

  // Whether this buffer belongs to an attached ZSL request.
  bool IsAttachedZslBuffer(const camera3_stream_buffer_t* buffer);

  // Whether this buffer belongs to a transformed ZSL request.
  bool IsTransformedZslBuffer(const camera3_stream_buffer_t* buffer);

  // Attaches ZSL output buffer into the request.
  void AttachRequest(camera3_capture_request_t* request,
                     std::vector<camera3_stream_buffer_t>* output_buffers);

  // Transforms a simple capture request into a reprocessing request.
  bool TransformRequest(camera3_capture_request_t* request,
                        camera_metadata_t** settings,
                        int32_t jpeg_orientation,
                        SelectionStrategy strategy = CLOSEST_3A);

  // Wait for the release fence on an attached ZSL output buffer. This function
  // is called after the attached buffer for |frame_number| is returned. After
  // |release_fence| is signalled, we'll mark the corresponding ZSL buffer as
  // ready.
  void WaitAttachedFrame(uint32_t frame_number, int release_fence);
  void WaitAttachedFrameOnFenceSyncThread(uint32_t frame_number,
                                          int release_fence);

  // Releases this stream buffer and the buffer handle underneath.
  void ReleaseStreamBuffer(camera3_stream_buffer_t buffer);
  void ReleaseStreamBufferOnFenceSyncThread(camera3_stream_buffer_t buffer);

  // Whether capability is supported.
  bool IsCapabilitySupported(const camera_metadata_t* static_info,
                             uint8_t capability);

  // Determines the size of the RAW stream for private reprocessing.
  bool SelectZslStreamSize(const camera_metadata_t* static_info,
                           uint32_t* bi_width,
                           uint32_t* bi_height);

  // Selects the best ZSL buffer for reprocessing from the ZSL ring buffer.
  ZslBuffer* SelectZslBuffer(SelectionStrategy strategy);

  // Gets the current timestamp with the source from |timestamp_source_|.
  int64_t GetCurrentTimestamp();

  // Whether this buffer is 3A-converged (AE, AF, AWB).
  bool Is3AConverged(const android::CameraMetadata& android_metadata);

  // Reads ZSL buffer by the specified buffer index.
  ZslBuffer* MutableReadBufferByBufferIndex(size_t buffer_index);

  bool initialized_;

  // Whether ZSL mechanism is enabled.
  bool enabled_;
  // Lock to protect |enabled_|.
  base::Lock enabled_lock_;

  // The actual ZSL stream.
  std::unique_ptr<camera3_stream_t> bi_stream_;

  // Manages the buffer used for ZSL, essentially a buffer pool.
  ZslBufferManager zsl_buffer_manager_;

  // ZSL ring buffer stores the buffer handles, their status (e.g., processed,
  // chosen) and their corresponding metadata.
  base::RingBuffer<ZslBuffer, ZslHelper::kZslBufferSize> ring_buffer_;
  // Lock to protect |ring_buffer_|.
  base::Lock ring_buffer_lock_;

  // A mapping from frame number to (ZSL) ring buffer index.
  std::map<uint32_t, size_t> buffer_index_map_;

  // A thread that asynchornously waits for release fences and releases buffers
  // to ZSL Buffer Manager.
  base::Thread fence_sync_thread_;

  // ANDROID_REQUEST_PARTIAL_RESULT_COUNT from static metadata.
  int32_t partial_result_count_;

  // ANDROID_REQUEST_MAX_NUM_INPUT_STREAMS from static metadata.
  int32_t max_num_input_streams_;

  // ANDROID_SENSOR_INFO_TIMESTAMP_SOURCE from static metadata.
  camera_metadata_enum_android_sensor_info_timestamp_source_t timestamp_source_;

  // Utility for mapping framework and HAL frame numbers.
  FrameNumberMapper* frame_number_mapper_;
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_ZSL_HELPER_H_
