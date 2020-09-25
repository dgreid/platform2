/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/camera_device_adapter.h"

#include <unistd.h>

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/logging.h>
#include <base/timer/elapsed_timer.h>
#include <drm_fourcc.h>
#include <libyuv.h>
#include <mojo/public/cpp/system/platform_handle.h>
#include <sync/sync.h>
#include <system/camera_metadata.h>

#include "common/camera_buffer_handle.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"
#include "cros-camera/ipc_util.h"
#include "hal_adapter/camera3_callback_ops_delegate.h"
#include "hal_adapter/camera3_device_ops_delegate.h"

namespace cros {

constexpr base::TimeDelta kMonitorTimeDelta = base::TimeDelta::FromSeconds(2);

Camera3CaptureRequest::Camera3CaptureRequest(
    const camera3_capture_request_t& req)
    : settings_(android::CameraMetadata(clone_camera_metadata(req.settings))),
      input_buffer_(*req.input_buffer),
      output_stream_buffers_(req.output_buffers,
                             req.output_buffers + req.num_output_buffers) {
  Camera3CaptureRequest::frame_number = req.frame_number;
  Camera3CaptureRequest::settings = settings_.getAndLock();
  Camera3CaptureRequest::input_buffer = &input_buffer_;
  Camera3CaptureRequest::num_output_buffers = req.num_output_buffers;
  Camera3CaptureRequest::output_buffers = output_stream_buffers_.data();
}

CameraDeviceAdapter::CameraDeviceAdapter(camera3_device_t* camera_device,
                                         const camera_metadata_t* static_info,
                                         base::Callback<void()> close_callback)
    : camera_device_ops_thread_("CameraDeviceOpsThread"),
      camera_callback_ops_thread_("CameraCallbackOpsThread"),
      fence_sync_thread_("FenceSyncThread"),
      reprocess_effect_thread_("ReprocessEffectThread"),
      notify_error_thread_("NotifyErrorThread"),
      monitor_thread_("CameraMonitor"),
      close_callback_(close_callback),
      device_closed_(false),
      camera_device_(camera_device),
      static_info_(static_info),
      zsl_helper_(static_info, &frame_number_mapper_),
      camera_metrics_(CameraMetrics::New()) {
  VLOGF_ENTER() << ":" << camera_device_;
  camera3_callback_ops_t::process_capture_result = ProcessCaptureResult;
  camera3_callback_ops_t::notify = Notify;
}

CameraDeviceAdapter::~CameraDeviceAdapter() {
  VLOGF_ENTER() << ":" << camera_device_;
  camera_device_ops_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraDeviceAdapter::ResetDeviceOpsDelegateOnThread,
                 base::Unretained(this)));
  camera_callback_ops_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraDeviceAdapter::ResetCallbackOpsDelegateOnThread,
                 base::Unretained(this)));
  camera_device_ops_thread_.Stop();
  camera_callback_ops_thread_.Stop();
}

bool CameraDeviceAdapter::Start(
    HasReprocessEffectVendorTagCallback
        has_reprocess_effect_vendor_tag_callback,
    ReprocessEffectCallback reprocess_effect_callback) {
  if (!camera_device_ops_thread_.Start()) {
    LOGF(ERROR) << "Failed to start CameraDeviceOpsThread";
    return false;
  }
  if (!camera_callback_ops_thread_.Start()) {
    LOGF(ERROR) << "Failed to start CameraCallbackOpsThread";
    return false;
  }
  device_ops_delegate_.reset(new Camera3DeviceOpsDelegate(
      this, camera_device_ops_thread_.task_runner()));
  partial_result_count_ = [&]() {
    camera_metadata_ro_entry entry;
    if (find_camera_metadata_ro_entry(
            static_info_, ANDROID_REQUEST_PARTIAL_RESULT_COUNT, &entry) != 0) {
      return 1;
    }
    return entry.data.i32[0];
  }();
  camera_metadata_inspector_ =
      CameraMetadataInspector::Create(partial_result_count_);
  has_reprocess_effect_vendor_tag_callback_ =
      std::move(has_reprocess_effect_vendor_tag_callback);
  reprocess_effect_callback_ = std::move(reprocess_effect_callback);
  return true;
}

void CameraDeviceAdapter::Bind(
    mojom::Camera3DeviceOpsRequest device_ops_request) {
  device_ops_delegate_->Bind(
      device_ops_request.PassMessagePipe(),
      // Close the device when the Mojo channel breaks.
      base::Bind(base::IgnoreResult(&CameraDeviceAdapter::Close),
                 base::Unretained(this)));
}

int32_t CameraDeviceAdapter::Initialize(
    mojom::Camera3CallbackOpsPtr callback_ops) {
  VLOGF_ENTER();
  if (!fence_sync_thread_.Start()) {
    LOGF(ERROR) << "Fence sync thread failed to start";
    return -ENODEV;
  }
  if (!reprocess_effect_thread_.Start()) {
    LOGF(ERROR) << "Reprocessing effect thread failed to start";
    return -ENODEV;
  }
  if (!notify_error_thread_.Start()) {
    LOGF(ERROR) << "Notify error thread failed to start";
    return -ENODEV;
  }
  if (!monitor_thread_.Start()) {
    LOGF(ERROR) << "Monitor thread failed to start";
    return -ENODEV;
  }
  base::AutoLock l(callback_ops_delegate_lock_);
  // Unlike the camera module, only one peer is allowed to access a camera
  // device at any time.
  DCHECK(!callback_ops_delegate_);
  callback_ops_delegate_.reset(new Camera3CallbackOpsDelegate(
      camera_callback_ops_thread_.task_runner()));
  callback_ops_delegate_->Bind(
      callback_ops.PassInterface(),
      base::Bind(&CameraDeviceAdapter::ResetCallbackOpsDelegateOnThread,
                 base::Unretained(this)));
  capture_request_monitor_.SetTaskRunner(monitor_thread_.task_runner());
  capture_result_monitor_.SetTaskRunner(monitor_thread_.task_runner());
  return camera_device_->ops->initialize(camera_device_, this);
}

int32_t CameraDeviceAdapter::ConfigureStreams(
    mojom::Camera3StreamConfigurationPtr config,
    mojom::Camera3StreamConfigurationPtr* updated_config) {
  VLOGF_ENTER();

  base::ElapsedTimer timer;

  base::AutoLock l(streams_lock_);

  // Free previous allocated buffers before new allocation.
  FreeAllocatedStreamBuffers();

  internal::ScopedStreams new_streams;
  for (const auto& s : config->streams) {
    LOGF(INFO) << "id = " << s->id << ", type = " << s->stream_type
               << ", size = " << s->width << "x" << s->height
               << ", format = " << s->format;
    uint64_t id = s->id;
    std::unique_ptr<camera3_stream_t>& stream = new_streams[id];
    stream.reset(new camera3_stream_t);
    memset(stream.get(), 0, sizeof(*stream.get()));
    stream->stream_type = static_cast<camera3_stream_type_t>(s->stream_type);
    stream->width = s->width;
    stream->height = s->height;
    stream->format = static_cast<int32_t>(s->format);
    stream->usage = s->usage;
    stream->max_buffers = s->max_buffers;
    stream->data_space = static_cast<android_dataspace_t>(s->data_space);
    stream->rotation = static_cast<camera3_stream_rotation_t>(s->rotation);
    stream->crop_rotate_scale_degrees = 0;
    if (s->crop_rotate_scale_info) {
      stream->crop_rotate_scale_degrees =
          static_cast<camera3_stream_rotation_t>(
              s->crop_rotate_scale_info->crop_rotate_scale_degrees);
    }

    // Currently we are not interest in the resolution of input stream and
    // bidirectional stream.
    if (stream->stream_type == CAMERA3_STREAM_OUTPUT) {
      camera_metrics_->SendConfigureStreamResolution(
          stream->width, stream->height, stream->format);
    }
  }
  streams_.swap(new_streams);
  zsl_helper_.SetZslEnabled(zsl_helper_.CanEnableZsl(streams_));

  camera3_stream_configuration_t stream_list;
  stream_list.num_streams = config->streams.size();
  std::vector<camera3_stream_t*> streams(stream_list.num_streams);
  stream_list.streams = streams.data();
  stream_list.operation_mode =
      static_cast<camera3_stream_configuration_mode_t>(config->operation_mode);
  size_t i = 0;
  for (auto it = streams_.begin(); it != streams_.end(); it++) {
    stream_list.streams[i++] = it->second.get();
  }
  if (zsl_helper_.IsZslEnabled()) {
    zsl_helper_.AttachZslStream(&stream_list, &streams);
    zsl_stream_ = streams.back();
  }

  int32_t result =
      camera_device_->ops->configure_streams(camera_device_, &stream_list);
  if (!result) {
    *updated_config = mojom::Camera3StreamConfiguration::New();
    (*updated_config)->operation_mode = config->operation_mode;
    for (const auto& s : streams_) {
      mojom::Camera3StreamPtr ptr = mojom::Camera3Stream::New();
      ptr->id = s.first;
      ptr->format = static_cast<mojom::HalPixelFormat>(s.second->format);
      ptr->width = s.second->width;
      ptr->height = s.second->height;
      ptr->stream_type =
          static_cast<mojom::Camera3StreamType>(s.second->stream_type);
      ptr->data_space = s.second->data_space;
      // HAL should only change usage and max_buffers.
      ptr->usage = s.second->usage;
      ptr->max_buffers = s.second->max_buffers;
      ptr->crop_rotate_scale_info = mojom::CropRotateScaleInfo::New(
          static_cast<mojom::Camera3StreamRotation>(
              s.second->crop_rotate_scale_degrees));
      (*updated_config)->streams.push_back(std::move(ptr));
    }
  }

  camera_metrics_->SendConfigureStreamsLatency(timer.Elapsed());

  return result;
}

mojom::CameraMetadataPtr CameraDeviceAdapter::ConstructDefaultRequestSettings(
    mojom::Camera3RequestTemplate type) {
  VLOGF_ENTER();
  const camera_metadata_t* metadata =
      camera_device_->ops->construct_default_request_settings(
          camera_device_, static_cast<int32_t>(type));
  return internal::SerializeCameraMetadata(metadata);
}

int32_t CameraDeviceAdapter::ProcessCaptureRequest(
    mojom::Camera3CaptureRequestPtr request) {
  VLOGF_ENTER();

  // Complete the pending reprocess request first if exists. We need to
  // prioritize reprocess requests because CCA can be waiting for the
  // reprocessed picture before unblocking UI.
  {
    base::AutoLock lock(process_reprocess_request_callback_lock_);
    if (!process_reprocess_request_callback_.is_null())
      std::move(process_reprocess_request_callback_).Run();
  }
  if (!request) {
    return 0;
  }

  camera3_capture_request_t req;
  req.frame_number =
      frame_number_mapper_.GetHalFrameNumber(request->frame_number);

  internal::ScopedCameraMetadata settings =
      internal::DeserializeCameraMetadata(request->settings);

  if (capture_request_monitor_.IsRunning()) {
    capture_request_monitor_.Reset();
  } else {
    capture_request_monitor_.Start(
        FROM_HERE, kMonitorTimeDelta,
        base::BindOnce(&CameraDeviceAdapter::MonitorTimeout,
                       base::Unretained(this), "CaptureRequest"));
    LOG(INFO) << "Start monitor timer for capture request, frame number:"
              << req.frame_number;
  }

  // Deserialize input buffer.
  buffer_handle_t input_buffer_handle;
  camera3_stream_buffer_t input_buffer;
  if (!request->input_buffer.is_null()) {
    base::AutoLock streams_lock(streams_lock_);
    base::AutoLock buffer_handles_lock(buffer_handles_lock_);
    if (request->input_buffer->buffer_handle) {
      if (RegisterBufferLocked(
              std::move(request->input_buffer->buffer_handle))) {
        LOGF(ERROR) << "Failed to register input buffer";
        return -EINVAL;
      }
    }
    input_buffer.buffer =
        const_cast<const native_handle_t**>(&input_buffer_handle);
    internal::DeserializeStreamBuffer(request->input_buffer, streams_,
                                      buffer_handles_, &input_buffer);
    req.input_buffer = &input_buffer;
  } else {
    req.input_buffer = nullptr;
  }

  // Deserialize output buffers.
  size_t num_output_buffers = request->output_buffers.size();
  DCHECK_GT(num_output_buffers, 0);

  std::vector<camera3_stream_buffer_t> output_buffers(num_output_buffers);
  std::vector<camera3_stream_buffer_t> still_output_buffers;
  camera3_capture_request_t still_req{.num_output_buffers = 0};
  {
    base::AutoLock streams_lock(streams_lock_);
    base::AutoLock buffer_handles_lock(buffer_handles_lock_);
    for (size_t i = 0; i < num_output_buffers; ++i) {
      mojom::Camera3StreamBufferPtr& out_buf_ptr = request->output_buffers[i];
      if (out_buf_ptr->buffer_handle) {
        if (RegisterBufferLocked(std::move(out_buf_ptr->buffer_handle))) {
          LOGF(ERROR) << "Failed to register output buffer";
          return -EINVAL;
        }
      }
      internal::DeserializeStreamBuffer(out_buf_ptr, streams_, buffer_handles_,
                                        &output_buffers.at(i));
    }
    if (zsl_helper_.IsZslEnabled()) {
      zsl_helper_.ProcessZslCaptureRequest(
          request->frame_number, &req, &output_buffers, &settings, &still_req,
          &still_output_buffers, ZslHelper::SelectionStrategy::CLOSEST_3A);
    }
    req.num_output_buffers = output_buffers.size();
    req.output_buffers =
        const_cast<const camera3_stream_buffer_t*>(output_buffers.data());
  }

  req.settings = settings.get();

  if (camera_metadata_inspector_) {
    camera_metadata_inspector_->InspectRequest(&req);
  }

  // Apply reprocessing effects
  if (req.input_buffer &&
      has_reprocess_effect_vendor_tag_callback_.Run(*req.settings)) {
    VLOGF(1) << "Applying reprocessing effects on input buffer";
    // Run reprocessing effect asynchronously so that it does not block other
    // requests.  It introduces a risk that buffers of the same stream may be
    // returned out of order.  Since CTS would not go this way and GCA would
    // not mix reprocessing effect captures with normal ones, it should be
    // fine.
    auto req_ptr = std::make_unique<Camera3CaptureRequest>(req);
    reprocess_effect_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(
            &CameraDeviceAdapter::ReprocessEffectsOnReprocessEffectThread,
            base::Unretained(this), base::Passed(&req_ptr)));
    return 0;
  }

  int ret_split;
  bool is_request_split = still_req.num_output_buffers > 0;
  if (is_request_split) {
    // Swap frame numbers to send the split still capture request first.
    // When merging the capture results, the shutter message and result metadata
    // of the second preview capture request will be trimmed, which is safe
    // because we don't reprocess preview frames.
    std::swap(req.frame_number, still_req.frame_number);
    frame_number_mapper_.RegisterCaptureRequest(&still_req, is_request_split,
                                                /*is_request_added=*/false);
    ret_split = camera_device_->ops->process_capture_request(camera_device_,
                                                             &still_req);
    free_camera_metadata(const_cast<camera_metadata_t*>(still_req.settings));
    if (ret_split != 0) {
      LOGF(ERROR) << "Failed to process split still capture request";
      return ret_split;
    }
  }

  frame_number_mapper_.RegisterCaptureRequest(
      &req, is_request_split, /*is_request_added=*/is_request_split);
  int ret = camera_device_->ops->process_capture_request(camera_device_, &req);

  if (is_request_split) {
    // No need to process -ENODEV here since it's not recoverable.
    if (ret == -EINVAL) {
      NotifyAddedFrameError(std::move(still_req),
                            std::move(still_output_buffers));
    }
    // Any errors occurred in the split request are handled separately.
    return 0;
  }
  return ret;
}

void CameraDeviceAdapter::Dump(mojo::ScopedHandle fd) {
  VLOGF_ENTER();
  base::ScopedFD dump_fd(mojo::UnwrapPlatformHandle(std::move(fd)).TakeFD());
  camera_device_->ops->dump(camera_device_, dump_fd.get());
}

int32_t CameraDeviceAdapter::Flush() {
  VLOGF_ENTER();
  return camera_device_->ops->flush(camera_device_);
}

static bool IsMatchingFormat(mojom::HalPixelFormat hal_pixel_format,
                             uint32_t drm_format) {
  switch (hal_pixel_format) {
    case mojom::HalPixelFormat::HAL_PIXEL_FORMAT_RGBA_8888:
      return drm_format == DRM_FORMAT_ABGR8888;
    case mojom::HalPixelFormat::HAL_PIXEL_FORMAT_RGBX_8888:
      return drm_format == DRM_FORMAT_XBGR8888;
    case mojom::HalPixelFormat::HAL_PIXEL_FORMAT_BGRA_8888:
      return drm_format == DRM_FORMAT_ARGB8888;
    case mojom::HalPixelFormat::HAL_PIXEL_FORMAT_YCrCb_420_SP:
      return drm_format == DRM_FORMAT_NV21;
    case mojom::HalPixelFormat::HAL_PIXEL_FORMAT_YCbCr_422_I:
      return drm_format == DRM_FORMAT_YUYV;
    case mojom::HalPixelFormat::HAL_PIXEL_FORMAT_BLOB:
      return drm_format == DRM_FORMAT_R8;
    case mojom::HalPixelFormat::HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
      // We can't really check implementation defined formats.
      return true;
    case mojom::HalPixelFormat::HAL_PIXEL_FORMAT_YCbCr_420_888:
      return (drm_format == DRM_FORMAT_YUV420 ||
              drm_format == DRM_FORMAT_YVU420 ||
              drm_format == DRM_FORMAT_NV21 || drm_format == DRM_FORMAT_NV12);
    case mojom::HalPixelFormat::HAL_PIXEL_FORMAT_YV12:
      return drm_format == DRM_FORMAT_YVU420;
  }
  return false;
}

int32_t CameraDeviceAdapter::RegisterBuffer(
    uint64_t buffer_id,
    mojom::Camera3DeviceOps::BufferType type,
    std::vector<mojo::ScopedHandle> fds,
    uint32_t drm_format,
    mojom::HalPixelFormat hal_pixel_format,
    uint32_t width,
    uint32_t height,
    const std::vector<uint32_t>& strides,
    const std::vector<uint32_t>& offsets) {
  base::AutoLock l(buffer_handles_lock_);
  return CameraDeviceAdapter::RegisterBufferLocked(
      buffer_id, type, std::move(fds), drm_format, hal_pixel_format, width,
      height, strides, offsets);
}

int32_t CameraDeviceAdapter::Close() {
  // Close the device.
  VLOGF_ENTER();
  if (device_closed_) {
    return 0;
  }
  reprocess_effect_thread_.Stop();
  notify_error_thread_.Stop();
  int32_t ret = camera_device_->common.close(&camera_device_->common);
  device_closed_ = true;
  DCHECK_EQ(ret, 0);
  fence_sync_thread_.Stop();
  capture_request_monitor_.Stop();
  capture_result_monitor_.Stop();
  monitor_thread_.Stop();

  FreeAllocatedStreamBuffers();

  close_callback_.Run();
  return ret;
}

int32_t CameraDeviceAdapter::ConfigureStreamsAndGetAllocatedBuffers(
    mojom::Camera3StreamConfigurationPtr config,
    mojom::Camera3StreamConfigurationPtr* updated_config,
    AllocatedBuffers* allocated_buffers) {
  VLOGF_ENTER();
  int32_t result = ConfigureStreams(std::move(config), updated_config);

  // Early return if configure streams failed.
  if (result) {
    return result;
  }

  bool is_success =
      AllocateBuffersForStreams((*updated_config)->streams, allocated_buffers);

  if (!is_success) {
    FreeAllocatedStreamBuffers();
  }

  return result;
}

// static
void CameraDeviceAdapter::ProcessCaptureResult(
    const camera3_callback_ops_t* ops, const camera3_capture_result_t* result) {
  VLOGF_ENTER();
  CameraDeviceAdapter* self = const_cast<CameraDeviceAdapter*>(
      static_cast<const CameraDeviceAdapter*>(ops));
  if (self->capture_result_monitor_.IsRunning()) {
    self->capture_result_monitor_.Reset();
  } else {
    self->capture_result_monitor_.Start(
        FROM_HERE, kMonitorTimeDelta,
        base::BindOnce(&CameraDeviceAdapter::MonitorTimeout,
                       base::Unretained(self), "CaptureResult"));
    LOG(INFO) << "Start monitor timer for capture result frame number:"
              << result->frame_number;
  }

  camera3_capture_result_t res = *result;
  camera3_stream_buffer_t in_buf = {};
  mojom::Camera3CaptureResultPtr result_ptr;
  {
    base::AutoLock reprocess_handles_lock(self->reprocess_handles_lock_);
    if (result->input_buffer && !self->reprocess_handles_.empty() &&
        *result->input_buffer->buffer ==
            *self->reprocess_handles_.front().GetHandle()) {
      in_buf = *result->input_buffer;
      // Restore original input buffer
      base::AutoLock buffer_handles_lock(self->buffer_handles_lock_);
      in_buf.buffer =
          &self->buffer_handles_.at(self->input_buffer_handle_ids_.front())
               ->self;
      self->reprocess_handles_.pop_front();
      self->input_buffer_handle_ids_.pop_front();
      res.input_buffer = &in_buf;
    }
  }
  {
    base::AutoLock reprocess_result_metadata_lock(
        self->reprocess_result_metadata_lock_);
    auto it = self->reprocess_result_metadata_.find(res.frame_number);
    if (it != self->reprocess_result_metadata_.end() && !it->second.isEmpty() &&
        res.result != nullptr) {
      it->second.append(res.result);
      res.result = it->second.getAndLock();
    }
    result_ptr = self->PrepareCaptureResult(&res);
    if (res.result != nullptr) {
      self->reprocess_result_metadata_.erase(res.frame_number);
    }
  }

  if (!result_ptr->result->entries.has_value() &&
      !result_ptr->output_buffers.has_value() &&
      result_ptr->input_buffer.is_null()) {
    // Android camera framework doesn't accept empty capture results. Since ZSL
    // would remove the input buffer, output buffers and metadata it added, it's
    // possible that we end up with an empty capture result.
    return;
  }

  base::AutoLock l(self->callback_ops_delegate_lock_);
  if (self->callback_ops_delegate_) {
    if (self->camera_metadata_inspector_) {
      self->camera_metadata_inspector_->InspectResult(result);
    }
    self->callback_ops_delegate_->ProcessCaptureResult(std::move(result_ptr));
  }
}

// static
void CameraDeviceAdapter::Notify(const camera3_callback_ops_t* ops,
                                 const camera3_notify_msg_t* msg) {
  VLOGF_ENTER();
  CameraDeviceAdapter* self = const_cast<CameraDeviceAdapter*>(
      static_cast<const CameraDeviceAdapter*>(ops));
  std::vector<camera3_notify_msg_t> msgs;
  self->PreprocessNotifyMsg(msg, &msgs);
  if (msgs.size() == 0) {
    LOGF(INFO) << "Trimmed a Notify() call";
    return;
  }
  for (auto& msg : msgs) {
    mojom::Camera3NotifyMsgPtr msg_ptr = self->PrepareNotifyMsg(&msg);
    base::AutoLock l(self->callback_ops_delegate_lock_);
    if (msg.type == CAMERA3_MSG_ERROR) {
      self->camera_metrics_->SendError(msg.message.error.error_code);
    }
    if (self->callback_ops_delegate_) {
      self->callback_ops_delegate_->Notify(std::move(msg_ptr));
    }
    if (msg.type == CAMERA3_MSG_ERROR &&
        msg.message.error.error_code == CAMERA3_MSG_ERROR_DEVICE) {
      LOGF(ERROR) << "Fatal device error; aborting the camera service";
      _exit(EIO);
    }
  }
}

bool CameraDeviceAdapter::AllocateBuffersForStreams(
    const std::vector<mojom::Camera3StreamPtr>& streams,
    AllocatedBuffers* allocated_buffers) {
  AllocatedBuffers tmp_allocated_buffers;
  auto* camera_buffer_manager = CameraBufferManager::GetInstance();
  for (const auto& stream : streams) {
    std::vector<mojom::Camera3StreamBufferPtr> new_buffers;
    uint32_t stream_format = static_cast<uint32_t>(stream->format);
    uint64_t stream_id = stream->id;
    DCHECK(stream_format == HAL_PIXEL_FORMAT_BLOB ||
           stream_format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED ||
           stream_format == HAL_PIXEL_FORMAT_YCbCr_420_888);
    uint32_t buffer_width;
    uint32_t buffer_height;
    int status;
    if (stream_format == HAL_PIXEL_FORMAT_BLOB) {
      camera_metadata_ro_entry entry;
      status = find_camera_metadata_ro_entry(static_info_,
                                             ANDROID_JPEG_MAX_SIZE, &entry);
      if (status) {
        LOG(ERROR) << "No Jpeg max size information in metadata.";
        return false;
      }
      buffer_width = entry.data.i32[0];
      buffer_height = 1;
    } else {
      buffer_width = stream->width;
      buffer_height = stream->height;
    }
    for (size_t i = 0; i < stream->max_buffers; i++) {
      mojom::Camera3StreamBufferPtr new_buffer =
          mojom::Camera3StreamBuffer::New();
      new_buffer->stream_id = stream_id;

      mojom::CameraBufferHandlePtr mojo_buffer_handle =
          mojom::CameraBufferHandle::New();

      buffer_handle_t buffer_handle;
      uint32_t buffer_stride;
      status = camera_buffer_manager->Allocate(
          buffer_width, buffer_height, stream_format, stream->usage,
          BufferType::GRALLOC, &buffer_handle, &buffer_stride);
      if (status) {
        LOGF(ERROR) << "Failed to allocate buffer.";
        return false;
      }

      mojo_buffer_handle->width = buffer_width;
      mojo_buffer_handle->height = buffer_height;
      mojo_buffer_handle->drm_format =
          camera_buffer_manager->ResolveDrmFormat(stream_format, stream->usage);
      CHECK_NE(mojo_buffer_handle->drm_format, 0);

      auto num_planes = CameraBufferManager::GetNumPlanes(buffer_handle);
      mojo_buffer_handle->sizes = std::vector<uint32_t>();
      for (size_t plane = 0; plane < num_planes; plane++) {
        mojo_buffer_handle->fds.push_back(
            mojo::WrapPlatformFile(buffer_handle->data[plane]));
        mojo_buffer_handle->strides.push_back(
            CameraBufferManager::GetPlaneStride(buffer_handle, plane));
        mojo_buffer_handle->offsets.push_back(
            CameraBufferManager::GetPlaneOffset(buffer_handle, plane));
        mojo_buffer_handle->sizes->push_back(
            CameraBufferManager::GetPlaneSize(buffer_handle, plane));
      }

      auto* camera_buffer_handle =
          camera_buffer_handle_t::FromBufferHandle(buffer_handle);
      uint64_t buffer_id = camera_buffer_handle->buffer_id;
      mojo_buffer_handle->buffer_id = buffer_id;
      mojo_buffer_handle->hal_pixel_format = stream->format;

      new_buffer->buffer_id = buffer_id;
      new_buffer->buffer_handle = std::move(mojo_buffer_handle);
      new_buffers.push_back(std::move(new_buffer));

      allocated_stream_buffers_[buffer_id] = std::move(buffer_handle);
    }
    tmp_allocated_buffers.insert(
        std::pair<uint64_t, std::vector<mojom::Camera3StreamBufferPtr>>(
            stream_id, std::move(new_buffers)));
  }
  *allocated_buffers = std::move(tmp_allocated_buffers);
  return true;
}

void CameraDeviceAdapter::FreeAllocatedStreamBuffers() {
  auto camera_buffer_manager = CameraBufferManager::GetInstance();
  if (allocated_stream_buffers_.empty()) {
    return;
  }

  for (auto it : allocated_stream_buffers_) {
    camera_buffer_manager->Free(it.second);
  }
  allocated_stream_buffers_.clear();
}

int32_t CameraDeviceAdapter::RegisterBufferLocked(
    uint64_t buffer_id,
    mojom::Camera3DeviceOps::BufferType type,
    std::vector<mojo::ScopedHandle> fds,
    uint32_t drm_format,
    mojom::HalPixelFormat hal_pixel_format,
    uint32_t width,
    uint32_t height,
    const std::vector<uint32_t>& strides,
    const std::vector<uint32_t>& offsets) {
  if (!IsMatchingFormat(hal_pixel_format, drm_format)) {
    LOG(ERROR) << "HAL pixel format " << hal_pixel_format
               << " does not match DRM format " << FormatToString(drm_format);
    return -EINVAL;
  }
  size_t num_planes = fds.size();

  std::unique_ptr<camera_buffer_handle_t> buffer_handle =
      std::make_unique<camera_buffer_handle_t>();
  buffer_handle->base.version = sizeof(buffer_handle->base);
  buffer_handle->base.numFds = kCameraBufferHandleNumFds;
  buffer_handle->base.numInts = kCameraBufferHandleNumInts;

  buffer_handle->magic = kCameraBufferMagic;
  buffer_handle->buffer_id = buffer_id;
  buffer_handle->type = static_cast<int32_t>(type);
  buffer_handle->drm_format = drm_format;
  buffer_handle->hal_pixel_format = static_cast<uint32_t>(hal_pixel_format);
  buffer_handle->width = width;
  buffer_handle->height = height;
  for (size_t i = 0; i < num_planes; ++i) {
    buffer_handle->fds[i] =
        mojo::UnwrapPlatformHandle(std::move(fds[i])).ReleaseFD();
    buffer_handle->strides[i] = strides[i];
    buffer_handle->offsets[i] = offsets[i];
  }
  buffer_handles_[buffer_id] = std::move(buffer_handle);

  VLOGF(1) << std::hex << "Buffer 0x" << buffer_id << " registered: "
           << "format: " << FormatToString(drm_format)
           << " dimension: " << std::dec << width << "x" << height
           << " num_planes: " << num_planes;
  return 0;
}

int32_t CameraDeviceAdapter::RegisterBufferLocked(
    mojom::CameraBufferHandlePtr buffer) {
  return RegisterBufferLocked(
      buffer->buffer_id, mojom::Camera3DeviceOps::BufferType::GRALLOC,
      std::move(buffer->fds), buffer->drm_format, buffer->hal_pixel_format,
      buffer->width, buffer->height, buffer->strides, buffer->offsets);
}

mojom::Camera3CaptureResultPtr CameraDeviceAdapter::PrepareCaptureResult(
    const camera3_capture_result_t* result) {
  mojom::Camera3CaptureResultPtr r = mojom::Camera3CaptureResult::New();

  r->frame_number =
      frame_number_mapper_.GetFrameworkFrameNumber(result->frame_number);

  frame_number_mapper_.RegisterCaptureResult(result, partial_result_count_);

  const camera3_stream_buffer_t* attached_output = nullptr;
  const camera3_stream_buffer_t* transformed_input = nullptr;
  if (zsl_helper_.IsZslEnabled()) {
    base::AutoLock streams_lock(streams_lock_);
    base::AutoLock buffer_handles_lock(buffer_handles_lock_);
    zsl_helper_.ProcessZslCaptureResult(result, &attached_output,
                                        &transformed_input);
  }

  if (frame_number_mapper_.IsAddedFrame(result->frame_number)) {
    // We use the metadata from the sister frame.
    LOGF(INFO) << "Trimming metadata for " << result->frame_number
               << " (framework frame number = " << r->frame_number << ")";
    r->result = cros::mojom::CameraMetadata::New();
    r->partial_result = 0;
  } else {
    r->result = internal::SerializeCameraMetadata(result->result);
    r->partial_result = result->partial_result;
  }

  // Serialize output buffers.  This may be none as num_output_buffers may be 0.
  if (result->output_buffers) {
    base::AutoLock streams_lock(streams_lock_);
    base::AutoLock buffer_handles_lock(buffer_handles_lock_);
    std::vector<mojom::Camera3StreamBufferPtr> output_buffers;
    for (size_t i = 0; i < result->num_output_buffers; i++) {
      if (result->output_buffers + i == attached_output) {
        continue;
      }
      mojom::Camera3StreamBufferPtr out_buf = internal::SerializeStreamBuffer(
          result->output_buffers + i, streams_, buffer_handles_);
      if (out_buf.is_null()) {
        LOGF(ERROR) << "Failed to serialize output stream buffer";
        // TODO(jcliang): Handle error?
      }
      buffer_handles_[out_buf->buffer_id]->state = kReturned;
      RemoveBufferLocked(*(result->output_buffers + i));
      output_buffers.push_back(std::move(out_buf));
    }
    if (output_buffers.size() > 0) {
      r->output_buffers = std::move(output_buffers);
    }
  }

  // Serialize input buffer.
  if (result->input_buffer && result->input_buffer != transformed_input) {
    base::AutoLock streams_lock(streams_lock_);
    base::AutoLock buffer_handles_lock(buffer_handles_lock_);
    mojom::Camera3StreamBufferPtr input_buffer =
        internal::SerializeStreamBuffer(result->input_buffer, streams_,
                                        buffer_handles_);
    if (input_buffer.is_null()) {
      LOGF(ERROR) << "Failed to serialize input stream buffer";
    }
    buffer_handles_[input_buffer->buffer_id]->state = kReturned;
    RemoveBufferLocked(*result->input_buffer);
    r->input_buffer = std::move(input_buffer);
  }

  return r;
}

void CameraDeviceAdapter::PreprocessNotifyMsg(
    const camera3_notify_msg_t* msg, std::vector<camera3_notify_msg_t>* msgs) {
  frame_number_mapper_.PreprocessNotifyMsg(msg, msgs, zsl_stream_);
}

mojom::Camera3NotifyMsgPtr CameraDeviceAdapter::PrepareNotifyMsg(
    const camera3_notify_msg_t* msg) {
  // Fill in the data from msg...
  mojom::Camera3NotifyMsgPtr m = mojom::Camera3NotifyMsg::New();
  m->type = static_cast<mojom::Camera3MsgType>(msg->type);
  m->message = mojom::Camera3NotifyMsgMessage::New();

  if (msg->type == CAMERA3_MSG_ERROR) {
    mojom::Camera3ErrorMsgPtr error = mojom::Camera3ErrorMsg::New();
    error->frame_number = msg->message.error.frame_number;
    uint64_t stream_id = 0;
    {
      base::AutoLock l(streams_lock_);
      for (const auto& s : streams_) {
        if (s.second.get() == msg->message.error.error_stream) {
          stream_id = s.first;
          break;
        }
      }
    }
    error->error_stream_id = stream_id;
    error->error_code =
        static_cast<mojom::Camera3ErrorMsgCode>(msg->message.error.error_code);
    m->message->set_error(std::move(error));
  } else if (msg->type == CAMERA3_MSG_SHUTTER) {
    mojom::Camera3ShutterMsgPtr shutter = mojom::Camera3ShutterMsg::New();
    shutter->frame_number = msg->message.shutter.frame_number;
    shutter->timestamp = msg->message.shutter.timestamp;
    m->message->set_shutter(std::move(shutter));
  } else {
    LOGF(ERROR) << "Invalid notify message type: " << msg->type;
  }

  return m;
}

void CameraDeviceAdapter::RemoveBufferLocked(
    const camera3_stream_buffer_t& buffer) {
  buffer_handles_lock_.AssertAcquired();
  int release_fence = buffer.release_fence;
  base::ScopedFD scoped_release_fence;
  if (release_fence != -1) {
    release_fence = dup(release_fence);
    if (release_fence == -1) {
      PLOGF(ERROR) << "Failed to dup release_fence";
      return;
    }
    scoped_release_fence.reset(release_fence);
  }

  // Remove the allocated camera buffer handle from |buffer_handles_| and
  // pass it to RemoveBufferOnFenceSyncThread. The buffer handle will be
  // freed after the release fence is signalled.
  const camera_buffer_handle_t* handle =
      camera_buffer_handle_t::FromBufferHandle(*(buffer.buffer));
  if (!handle) {
    return;
  }
  // Remove the buffer handle from |buffer_handles_| now to avoid a race
  // condition where the process_capture_request sends down an existing buffer
  // handle which hasn't been removed in RemoveBufferHandleOnFenceSyncThread.
  uint64_t buffer_id = handle->buffer_id;
  if (buffer_handles_[buffer_id]->state == kRegistered) {
    // Framework registered a new buffer with the same |buffer_id| before we
    // remove the old buffer handle from |buffer_handles_|.
    return;
  }
  std::unique_ptr<camera_buffer_handle_t> buffer_handle;
  buffer_handles_[buffer_id].swap(buffer_handle);
  buffer_handles_.erase(buffer_id);

  fence_sync_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraDeviceAdapter::RemoveBufferOnFenceSyncThread,
                 base::Unretained(this), base::Passed(&scoped_release_fence),
                 base::Passed(&buffer_handle)));
}

void CameraDeviceAdapter::RemoveBufferOnFenceSyncThread(
    base::ScopedFD release_fence,
    std::unique_ptr<camera_buffer_handle_t> buffer) {
  // In theory the release fence should be signaled by HAL as soon as possible,
  // and we could just set a large value for the timeout.  The timeout here is
  // set to 3 ms to allow testing multiple fences in round-robin if there are
  // multiple active buffers.
  const int kSyncWaitTimeoutMs = 3;
  const camera_buffer_handle_t* handle = buffer.get();
  DCHECK(handle);

  if (!release_fence.is_valid() ||
      !sync_wait(release_fence.get(), kSyncWaitTimeoutMs)) {
    VLOGF(1) << "Buffer 0x" << std::hex << handle->buffer_id << " removed";
  } else {
    // sync_wait() timeout. Reschedule and try to remove the buffer again.
    VLOGF(2) << "Release fence sync_wait() timeout on buffer 0x" << std::hex
             << handle->buffer_id;
    fence_sync_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&CameraDeviceAdapter::RemoveBufferOnFenceSyncThread,
                   base::Unretained(this), base::Passed(&release_fence),
                   base::Passed(&buffer)));
  }
}

void CameraDeviceAdapter::ReprocessEffectsOnReprocessEffectThread(
    std::unique_ptr<Camera3CaptureRequest> req) {
  VLOGF_ENTER();
  camera3_stream_t* input_stream = req->input_buffer->stream;
  camera3_stream_t* output_stream = req->output_buffers[0].stream;
  // Here we assume reprocessing effects can provide only one output of the
  // same size and format as that of input. Invoke HAL reprocessing if more
  // outputs, scaling and/or format conversion are required since ISP
  // may provide hardware acceleration for these operations.
  bool need_hal_reprocessing =
      (req->num_output_buffers != 1) ||
      (input_stream->width != req->output_buffers[0].stream->width) ||
      (input_stream->height != req->output_buffers[0].stream->height) ||
      (input_stream->format != req->output_buffers[0].stream->format);

  struct ReprocessContext {
    ReprocessContext(CameraDeviceAdapter* d,
                     const Camera3CaptureRequest* r,
                     bool n)
        : result(0),
          device_adapter(d),
          capture_request(r),
          need_hal_reprocessing(n) {}

    ~ReprocessContext() {
      if (result != 0) {
        camera3_notify_msg_t msg{
            .type = CAMERA3_MSG_ERROR,
            .message.error.frame_number = capture_request->frame_number,
            .message.error.error_code = CAMERA3_MSG_ERROR_REQUEST};
        device_adapter->Notify(device_adapter, &msg);
      }
      if (result != 0 || !need_hal_reprocessing) {
        camera3_capture_result_t capture_result{
            .frame_number = capture_request->frame_number,
            .result = capture_request->settings,
            .num_output_buffers = capture_request->num_output_buffers,
            .output_buffers = capture_request->output_buffers,
            .input_buffer = capture_request->input_buffer};
        device_adapter->ProcessCaptureResult(device_adapter, &capture_result);
      }
    }

    int32_t result;
    CameraDeviceAdapter* device_adapter;
    const Camera3CaptureRequest* capture_request;
    bool need_hal_reprocessing;
  };
  ReprocessContext reprocess_context(this, req.get(), need_hal_reprocessing);
  ScopedYUVBufferHandle scoped_output_handle =
      need_hal_reprocessing ?
                            // Allocate reprocessing buffer
          ScopedYUVBufferHandle::AllocateScopedYUVHandle(
              input_stream->width, input_stream->height,
              GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN)
                            :
                            // Wrap the output buffer
          ScopedYUVBufferHandle::CreateScopedYUVHandle(
              *req->output_buffers[0].buffer, output_stream->width,
              output_stream->height,
              GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN);
  if (!scoped_output_handle) {
    LOGF(ERROR) << "Failed to create scoped output buffer";
    reprocess_context.result = -EINVAL;
    return;
  }
  ScopedYUVBufferHandle scoped_input_handle =
      ScopedYUVBufferHandle::CreateScopedYUVHandle(
          *req->input_buffer->buffer, input_stream->width, input_stream->height,
          GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN);
  if (!scoped_input_handle) {
    LOGF(ERROR) << "Failed to create scoped input buffer";
    reprocess_context.result = -EINVAL;
    return;
  }
  android::CameraMetadata reprocess_result_metadata;
  reprocess_context.result = reprocess_effect_callback_.Run(
      *req->settings, &scoped_input_handle, input_stream->width,
      input_stream->height, &reprocess_result_metadata, &scoped_output_handle);
  if (reprocess_context.result != 0) {
    LOGF(ERROR) << "Failed to apply reprocess effect";
    return;
  }
  if (need_hal_reprocessing) {
    // Replace the input buffer with reprocessing output buffer
    {
      base::AutoLock reprocess_handles_lock(reprocess_handles_lock_);
      reprocess_handles_.push_back(std::move(scoped_output_handle));
      input_buffer_handle_ids_.push_back(
          reinterpret_cast<const camera_buffer_handle_t*>(
              *req->input_buffer->buffer)
              ->buffer_id);
      req->input_buffer->buffer = reprocess_handles_.back().GetHandle();
    }
    {
      base::AutoLock reprocess_result_metadata_lock(
          reprocess_result_metadata_lock_);
      reprocess_result_metadata_.emplace(req->frame_number,
                                         reprocess_result_metadata);
    }
    // Store the HAL reprocessing request and wait for CameraDeviceOpsThread to
    // complete it. Also post a null capture request to guarantee it will be
    // called when there's no existing capture requests.
    auto future = cros::Future<int32_t>::Create(nullptr);
    {
      base::AutoLock lock(process_reprocess_request_callback_lock_);
      DCHECK(process_reprocess_request_callback_.is_null());
      process_reprocess_request_callback_ = base::BindOnce(
          &CameraDeviceAdapter::ProcessReprocessRequestOnDeviceOpsThread,
          base::Unretained(this), base::Passed(&req),
          cros::GetFutureCallback(future));
    }
    camera_device_ops_thread_.task_runner()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](CameraDeviceAdapter* adapter) {
                         // Ignore returned value.
                         adapter->ProcessCaptureRequest(nullptr);
                       },
                       base::Unretained(this)));
    reprocess_context.result = future->Get();
    if (reprocess_context.result != 0) {
      LOGF(ERROR) << "Failed to process capture request after reprocessing";
    }
  }
}

void CameraDeviceAdapter::ProcessReprocessRequestOnDeviceOpsThread(
    std::unique_ptr<Camera3CaptureRequest> req,
    base::Callback<void(int32_t)> callback) {
  VLOGF_ENTER();
  frame_number_mapper_.RegisterCaptureRequest(req.get(),
                                              /*is_request_split=*/false,
                                              /*is_request_added=*/false);
  callback.Run(
      camera_device_->ops->process_capture_request(camera_device_, req.get()));
}

void CameraDeviceAdapter::NotifyAddedFrameError(
    camera3_capture_request_t req,
    std::vector<camera3_stream_buffer_t> output_buffers) {
  notify_error_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraDeviceAdapter::NotifyAddedFrameErrorOnNotifyErrorThread,
                 base::Unretained(this), base::Passed(&req),
                 base::Passed(&output_buffers)));
}

void CameraDeviceAdapter::NotifyAddedFrameErrorOnNotifyErrorThread(
    camera3_capture_request_t req,
    std::vector<camera3_stream_buffer_t> output_buffers) {
  uint32_t framework_frame_number =
      frame_number_mapper_.GetFrameworkFrameNumber(req.frame_number);
  if (callback_ops_delegate_) {
    base::AutoLock l(callback_ops_delegate_lock_);
    for (auto& buffer : output_buffers) {
      camera3_notify_msg_t msg{
          .type = CAMERA3_MSG_ERROR,
          .message.error = {.frame_number = framework_frame_number,
                            .error_stream = buffer.stream,
                            .error_code = CAMERA3_MSG_ERROR_BUFFER}};
      mojom::Camera3NotifyMsgPtr msg_ptr = PrepareNotifyMsg(&msg);
      callback_ops_delegate_->Notify(std::move(msg_ptr));
      // Result is not expected from the added frame so need to send
      // CAMERA3_MSG_ERROR_RESULT here.
    }

    // We still need to return the buffer handles back if we use notify() to
    // return  errors.
    camera3_capture_result_t res{
        .frame_number = framework_frame_number,
        .result = nullptr,
        .num_output_buffers = static_cast<uint32_t>(output_buffers.size()),
        .output_buffers = output_buffers.data(),
        .input_buffer = nullptr,  // Added requests don't have input buffer.
        .partial_result = 0};
    mojom::Camera3CaptureResultPtr result_ptr = PrepareCaptureResult(&res);
    callback_ops_delegate_->ProcessCaptureResult(std::move(result_ptr));
  }
}

void CameraDeviceAdapter::ResetDeviceOpsDelegateOnThread() {
  DCHECK(camera_device_ops_thread_.task_runner()->BelongsToCurrentThread());
  device_ops_delegate_.reset();
}

void CameraDeviceAdapter::ResetCallbackOpsDelegateOnThread() {
  DCHECK(camera_callback_ops_thread_.task_runner()->BelongsToCurrentThread());
  base::AutoLock l(callback_ops_delegate_lock_);
  callback_ops_delegate_.reset();
}

void CameraDeviceAdapter::MonitorTimeout(const std::string& name) {
  DCHECK(monitor_thread_.task_runner()->BelongsToCurrentThread());
  LOGF(WARNING) << kMonitorTimeDelta << " passed without " << name;
}

}  // namespace cros
