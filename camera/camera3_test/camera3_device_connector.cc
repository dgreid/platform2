// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera3_test/camera3_device_connector.h"

#include <linux/videodev2.h>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/stl_util.h>
#include <base/posix/eintr_wrapper.h>
#include <drm_fourcc.h>
#include <mojo/public/cpp/system/platform_handle.h>
#include <system/camera_metadata_hidden.h>

#include "camera3_test/camera3_module_connector.h"
#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/constants.h"
#include "cros-camera/ipc_util.h"

namespace camera3_test {

HalDeviceConnector::HalDeviceConnector(int cam_id, camera3_device_t* cam_device)
    : cam_device_(cam_device),
      dev_thread_("Camera3TestHalDeviceConnectorThread") {
  DETACH_FROM_THREAD(thread_checker_);
}

HalDeviceConnector::~HalDeviceConnector() {
  int result = -EIO;
  dev_thread_.PostTaskSync(FROM_HERE,
                           base::Bind(&HalDeviceConnector::CloseOnThread,
                                      base::Unretained(this), &result));
  dev_thread_.Stop();
}

void HalDeviceConnector::CloseOnThread(int* result) {
  if (cam_device_) {
    *result = cam_device_->common.close(&cam_device_->common);
  } else {
    *result = 0;
  }
}

int HalDeviceConnector::Initialize(const camera3_callback_ops_t* callback_ops) {
  if (!dev_thread_.Start()) {
    return -EINVAL;
  }
  int result = -EIO;
  dev_thread_.PostTaskSync(
      FROM_HERE, base::Bind(&HalDeviceConnector::InitializeOnThread,
                            base::Unretained(this), callback_ops, &result));
  return result;
}

void HalDeviceConnector::InitializeOnThread(
    const camera3_callback_ops_t* callback_ops, int* result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!cam_device_) {
    *result = -ENODEV;
    return;
  }
  *result = cam_device_->ops->initialize(cam_device_, callback_ops);
  if (*result != 0) {
    cam_device_->common.close(&cam_device_->common);
    cam_device_ = nullptr;
  }
}

int HalDeviceConnector::ConfigureStreams(
    camera3_stream_configuration_t* stream_list) {
  VLOGF_ENTER();
  int32_t result = -EIO;
  dev_thread_.PostTaskSync(
      FROM_HERE, base::Bind(&HalDeviceConnector::ConfigureStreamsOnThread,
                            base::Unretained(this), stream_list, &result));
  return result;
}

void HalDeviceConnector::ConfigureStreamsOnThread(
    camera3_stream_configuration_t* stream_list, int* result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (!cam_device_) {
    *result = -ENODEV;
    return;
  }
  *result = cam_device_->ops->configure_streams(cam_device_, stream_list);
}

const camera_metadata_t* HalDeviceConnector::ConstructDefaultRequestSettings(
    int type) {
  VLOGF_ENTER();
  const camera_metadata_t* metadata = nullptr;
  dev_thread_.PostTaskSync(
      FROM_HERE,
      base::Bind(&HalDeviceConnector::ConstructDefaultRequestSettingsOnThread,
                 base::Unretained(this), type, &metadata));
  return metadata;
}

void HalDeviceConnector::ConstructDefaultRequestSettingsOnThread(
    int type, const camera_metadata_t** result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  if (cam_device_) {
    *result =
        cam_device_->ops->construct_default_request_settings(cam_device_, type);
  }
}

int HalDeviceConnector::ProcessCaptureRequest(
    camera3_capture_request_t* capture_request) {
  VLOGF_ENTER();
  int32_t result = -EIO;
  dev_thread_.PostTaskSync(
      FROM_HERE, base::Bind(&HalDeviceConnector::ProcessCaptureRequestOnThread,
                            base::Unretained(this), capture_request, &result));
  return result;
}

void HalDeviceConnector::ProcessCaptureRequestOnThread(
    camera3_capture_request_t* request, int* result) {
  DCHECK_CALLED_ON_VALID_THREAD(thread_checker_);
  VLOGF_ENTER();
  if (!cam_device_) {
    *result = -ENODEV;
    return;
  }
  *result = cam_device_->ops->process_capture_request(cam_device_, request);
}

int HalDeviceConnector::Flush() {
  VLOGF_ENTER();
  if (!cam_device_) {
    return -ENODEV;
  }
  return cam_device_->ops->flush(cam_device_);
}

ClientDeviceConnector::ClientDeviceConnector()
    : mojo_callback_ops_(this),
      user_callback_ops_(nullptr),
      dev_thread_("Camera3TestClientDeviceConnectorThread") {
  dev_thread_.Start();
}

ClientDeviceConnector::~ClientDeviceConnector() {
  auto future = cros::Future<int32_t>::Create(nullptr);
  dev_thread_.PostTaskAsync(
      FROM_HERE,
      base::Bind(&ClientDeviceConnector::CloseOnThread, base::Unretained(this),
                 cros::GetFutureCallback(future)));
  if (!future->Wait() || future->Get() != 0) {
    ADD_FAILURE() << "Camera device close failed";
  }
  dev_thread_.Stop();
}

cros::mojom::Camera3DeviceOpsRequest
ClientDeviceConnector::GetDeviceOpsRequest() {
  cros::mojom::Camera3DeviceOpsRequest dev_ops_req;
  dev_thread_.PostTaskSync(
      FROM_HERE,
      base::Bind(&ClientDeviceConnector::MakeDeviceOpsRequestOnThread,
                 base::Unretained(this), &dev_ops_req));
  return dev_ops_req;
}

void ClientDeviceConnector::MakeDeviceOpsRequestOnThread(
    cros::mojom::Camera3DeviceOpsRequest* dev_ops_req) {
  dev_ops_.reset();
  *dev_ops_req = mojo::MakeRequest(&dev_ops_);
}

void ClientDeviceConnector::CloseOnThread(
    base::OnceCallback<void(int32_t)> cb) {
  dev_ops_->Close(base::BindOnce(&ClientDeviceConnector::OnClosedOnThread,
                                 base::Unretained(this), std::move(cb)));
}

void ClientDeviceConnector::OnClosedOnThread(
    base::OnceCallback<void(int32_t)> cb, int32_t result) {
  dev_ops_.reset();
  mojo_callback_ops_.Close();
  std::move(cb).Run(result);
}

int ClientDeviceConnector::Initialize(
    const camera3_callback_ops_t* callback_ops) {
  if (!callback_ops) {
    return -EINVAL;
  }
  auto future = cros::Future<int32_t>::Create(nullptr);
  dev_thread_.PostTaskAsync(
      FROM_HERE, base::Bind(&ClientDeviceConnector::InitializeOnThread,
                            base::Unretained(this), callback_ops,
                            cros::GetFutureCallback(future)));
  if (!future->Wait()) {
    LOGF(ERROR) << "Failed to initialize client camera device";
    return -EIO;
  }
  user_callback_ops_ = callback_ops;
  return future->Get();
}

void ClientDeviceConnector::InitializeOnThread(
    const camera3_callback_ops_t* callback_ops,
    base::OnceCallback<void(int32_t)> cb) {
  VLOGF_ENTER();
  cros::mojom::Camera3CallbackOpsPtr callback_ops_ptr;
  cros::mojom::Camera3CallbackOpsRequest callback_ops_request =
      mojo::MakeRequest(&callback_ops_ptr);
  mojo_callback_ops_.Bind(std::move(callback_ops_request));
  dev_ops_->Initialize(std::move(callback_ops_ptr), std::move(cb));
}

int ClientDeviceConnector::ConfigureStreams(
    camera3_stream_configuration_t* stream_list) {
  if (!stream_list) {
    return -EINVAL;
  }
  auto future = cros::Future<int32_t>::Create(nullptr);
  dev_thread_.PostTaskAsync(
      FROM_HERE, base::Bind(&ClientDeviceConnector::ConfigureStreamsOnThread,
                            base::Unretained(this), stream_list,
                            cros::GetFutureCallback(future)));
  if (!future->Wait()) {
    return -ENODEV;
  }
  return future->Get();
}

void ClientDeviceConnector::ConfigureStreamsOnThread(
    camera3_stream_configuration_t* stream_list,
    base::OnceCallback<void(int32_t)> cb) {
  camera3_streams_.clear();
  for (uint32_t i = 0; i < stream_list->num_streams; i++) {
    camera3_streams_.insert(stream_list->streams[i]);
  }
  cros::mojom::Camera3StreamConfigurationPtr stream_config =
      cros::mojom::Camera3StreamConfiguration::New();
  stream_config->operation_mode =
      static_cast<cros::mojom::Camera3StreamConfigurationMode>(
          stream_list->operation_mode);
  for (const auto& s : camera3_streams_) {
    cros::mojom::Camera3StreamPtr stream = cros::mojom::Camera3Stream::New();
    stream->id = reinterpret_cast<uint64_t>(s);
    stream->stream_type =
        static_cast<cros::mojom::Camera3StreamType>(s->stream_type);
    stream->width = s->width;
    stream->height = s->height;
    stream->format = static_cast<cros::mojom::HalPixelFormat>(s->format);
    stream->usage = s->usage;
    stream->max_buffers = s->max_buffers;
    stream->data_space = static_cast<uint32_t>(s->data_space);
    stream->rotation =
        static_cast<cros::mojom::Camera3StreamRotation>(s->rotation);
    cros::mojom::CropRotateScaleInfoPtr info =
        cros::mojom::CropRotateScaleInfo::New();
    info->crop_rotate_scale_degrees =
        static_cast<cros::mojom::Camera3StreamRotation>(
            s->crop_rotate_scale_degrees);
    stream->crop_rotate_scale_info = std::move(info);
    stream_config->streams.push_back(std::move(stream));
  }
  dev_ops_->ConfigureStreams(
      std::move(stream_config),
      base::BindOnce(&ClientDeviceConnector::OnConfiguredStreams,
                     base::Unretained(this), std::move(cb)));
}

void ClientDeviceConnector::OnConfiguredStreams(
    base::OnceCallback<void(int32_t)> cb,
    int32_t result,
    cros::mojom::Camera3StreamConfigurationPtr updated_config) {
  VLOGF_ENTER();
  if (result == 0) {
    for (const auto& s : updated_config->streams) {
      camera3_stream_t* ptr = reinterpret_cast<camera3_stream_t*>(s->id);
      ASSERT_NE(camera3_streams_.find(ptr), camera3_streams_.end());
      ptr->usage = s->usage;
      ptr->max_buffers = s->max_buffers;
    }
  }
  std::move(cb).Run(result);
}

const camera_metadata_t* ClientDeviceConnector::ConstructDefaultRequestSettings(
    int type) {
  VLOGF_ENTER();
  auto future = cros::Future<const camera_metadata_t*>::Create(nullptr);
  dev_thread_.PostTaskAsync(
      FROM_HERE,
      base::Bind(
          &ClientDeviceConnector::ConstructDefaultRequestSettingsOnThread,
          base::Unretained(this), type, cros::GetFutureCallback(future)));
  if (!future->Wait()) {
    return nullptr;
  }
  return future->Get();
}

void ClientDeviceConnector::ConstructDefaultRequestSettingsOnThread(
    int type, base::OnceCallback<void(const camera_metadata_t*)> cb) {
  if (base::Contains(default_req_settings_map_, type)) {
    std::move(cb).Run(default_req_settings_map_.at(type).get());
    return;
  }
  dev_ops_->ConstructDefaultRequestSettings(
      static_cast<cros::mojom::Camera3RequestTemplate>(type),
      base::BindOnce(
          &ClientDeviceConnector::OnConstructedDefaultRequestSettings,
          base::Unretained(this), type, std::move(cb)));
}

void ClientDeviceConnector::OnConstructedDefaultRequestSettings(
    int type,
    base::OnceCallback<void(const camera_metadata_t*)> cb,
    cros::mojom::CameraMetadataPtr settings) {
  VLOGF_ENTER();
  if (!base::Contains(default_req_settings_map_, type)) {
    default_req_settings_map_[type] =
        cros::internal::DeserializeCameraMetadata(settings);
  }
  std::move(cb).Run(default_req_settings_map_.at(type).get());
}

int ClientDeviceConnector::ProcessCaptureRequest(
    camera3_capture_request_t* capture_request) {
  VLOGF_ENTER();
  if (!capture_request) {
    return -EINVAL;
  }
  auto future = cros::Future<int32_t>::Create(nullptr);
  dev_thread_.PostTaskAsync(
      FROM_HERE,
      base::Bind(&ClientDeviceConnector::ProcessCaptureRequestOnThread,
                 base::Unretained(this), capture_request,
                 cros::GetFutureCallback(future)));
  if (!future->Wait()) {
    return -EIO;
  }
  return future->Get();
}

void ClientDeviceConnector::ProcessCaptureRequestOnThread(
    camera3_capture_request_t* capture_request,
    base::OnceCallback<void(int32_t)> cb) {
  cros::mojom::Camera3CaptureRequestPtr request =
      cros::mojom::Camera3CaptureRequest::New();
  request->frame_number = capture_request->frame_number;
  request->settings =
      cros::internal::SerializeCameraMetadata(capture_request->settings);
  if (capture_request->input_buffer) {
    cros::mojom::Camera3StreamBufferPtr input_buffer =
        PrepareStreamBufferPtr(capture_request->input_buffer);
    ASSERT_FALSE(input_buffer.is_null());
    request->input_buffer = std::move(input_buffer);
  }
  for (uint32_t i = 0; i < capture_request->num_output_buffers; i++) {
    cros::mojom::Camera3StreamBufferPtr output_buffer =
        PrepareStreamBufferPtr(capture_request->output_buffers + i);
    ASSERT_FALSE(output_buffer.is_null());
    request->output_buffers.push_back(std::move(output_buffer));
  }
  dev_ops_->ProcessCaptureRequest(std::move(request), std::move(cb));
}

cros::mojom::Camera3StreamBufferPtr
ClientDeviceConnector::PrepareStreamBufferPtr(
    const camera3_stream_buffer_t* buffer) {
  VLOGF_ENTER();
  uint32_t drm_format = 0;
  switch (cros::CameraBufferManager::GetV4L2PixelFormat(*buffer->buffer)) {
    case V4L2_PIX_FMT_JPEG:
      drm_format = DRM_FORMAT_R8;
      break;
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV12M:
      drm_format = DRM_FORMAT_NV12;
      break;
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV21M:
      drm_format = DRM_FORMAT_NV21;
      break;
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YUV420M:
      drm_format = DRM_FORMAT_YUV420;
      break;
    case V4L2_PIX_FMT_YVU420:
    case V4L2_PIX_FMT_YVU420M:
      drm_format = DRM_FORMAT_YVU420;
      break;
    default:
      ADD_FAILURE() << "Unsupported V4L2 format: 0x" << std::hex
                    << cros::CameraBufferManager::GetV4L2PixelFormat(
                           *buffer->buffer);
      return nullptr;
  }
  uint32_t num_planes =
      cros::CameraBufferManager::GetNumPlanes(*buffer->buffer);
  std::vector<uint32_t> strides;
  std::vector<uint32_t> offsets;
  for (uint32_t i = 0; i < num_planes; i++) {
    strides.push_back(
        cros::CameraBufferManager::GetPlaneStride(*buffer->buffer, i));
    offsets.push_back(
        cros::CameraBufferManager::GetPlaneOffset(*buffer->buffer, i));
  }
  buffer_handle_t* native_handle = buffer->buffer;
  std::vector<mojo::ScopedHandle> fds;
  for (uint32_t i = 0; i < num_planes; i++) {
    int dup_fd = HANDLE_EINTR(dup((*native_handle)->data[i]));
    fds.emplace_back(mojo::WrapPlatformFile(dup_fd));
  }
  uint64_t buffer_id = reinterpret_cast<uint64_t>(native_handle);
  base::AutoLock bufferLock(buffer_handle_map_lock_);
  buffer_handle_map_[buffer_id] = native_handle;
  cros::mojom::Camera3StreamBufferPtr buffer_ptr =
      cros::mojom::Camera3StreamBuffer::New();
  buffer_ptr->stream_id = reinterpret_cast<uint64_t>(buffer->stream);
  buffer_ptr->buffer_id = buffer_id;
  buffer_ptr->status =
      static_cast<cros::mojom::Camera3BufferStatus>(buffer->status);
  cros::mojom::CameraBufferHandlePtr handle_ptr =
      cros::mojom::CameraBufferHandle::New();
  handle_ptr->buffer_id = buffer_id;
  handle_ptr->fds = std::move(fds);
  handle_ptr->drm_format = drm_format;
  handle_ptr->hal_pixel_format =
      static_cast<cros::mojom::HalPixelFormat>(buffer->stream->format);
  handle_ptr->width = cros::CameraBufferManager::GetWidth(*buffer->buffer);
  handle_ptr->height = cros::CameraBufferManager::GetHeight(*buffer->buffer);
  handle_ptr->strides = std::move(strides);
  handle_ptr->offsets = std::move(offsets);
  buffer_ptr->buffer_handle = std::move(handle_ptr);
  return buffer_ptr;
}

int ClientDeviceConnector::Flush() {
  VLOGF_ENTER();
  auto future = cros::Future<int32_t>::Create(nullptr);
  dev_ops_->Flush(cros::GetFutureCallback(future));
  if (!future->Wait()) {
    return -ENODEV;
  }
  return future->Get();
}

void ClientDeviceConnector::Notify(cros::mojom::Camera3NotifyMsgPtr message) {
  VLOGF_ENTER();
  camera3_notify_msg_t notify_msg;
  notify_msg.type = static_cast<int32_t>(message->type);
  if (message->type == cros::mojom::Camera3MsgType::CAMERA3_MSG_ERROR) {
    cros::mojom::Camera3ErrorMsgPtr& error = message->message->get_error();
    notify_msg.message.error.frame_number = error->frame_number;
    if (!error->error_stream_id) {
      notify_msg.message.error.error_stream = nullptr;
    } else {
      notify_msg.message.error.error_stream =
          reinterpret_cast<camera3_stream_t*>(error->error_stream_id);
    }
    notify_msg.message.error.error_code = static_cast<int>(error->error_code);
  } else {  // message->type ==
            // cros::mojom::Camera3MsgType::CAMERA3_MSG_SHUTTER)
    cros::mojom::Camera3ShutterMsgPtr& shutter =
        message->message->get_shutter();
    notify_msg.message.shutter.frame_number = shutter->frame_number;
    notify_msg.message.shutter.timestamp = shutter->timestamp;
  }
  user_callback_ops_->notify(user_callback_ops_, &notify_msg);
}

void ClientDeviceConnector::ProcessCaptureResult(
    cros::mojom::Camera3CaptureResultPtr result) {
  VLOGF_ENTER();
  camera3_capture_result_t capture_result;

  capture_result.frame_number = result->frame_number;
  capture_result.partial_result = result->partial_result;

  cros::internal::ScopedCameraMetadata metadata;
  if (!result->result->entries.has_value()) {
    capture_result.result = nullptr;
  } else {
    metadata = cros::internal::DeserializeCameraMetadata(result->result);
    capture_result.result = metadata.get();
  }

  size_t num_output_buffers = result->output_buffers.has_value()
                                  ? result->output_buffers.value().size()
                                  : 0u;
  capture_result.num_output_buffers = num_output_buffers;

  std::vector<camera3_stream_buffer_t> output_buffers(num_output_buffers);
  if (!result->output_buffers.has_value()) {
    capture_result.output_buffers = nullptr;
  } else {
    for (size_t i = 0; i < num_output_buffers; i++) {
      cros::mojom::Camera3StreamBufferPtr& buffer_ptr =
          result->output_buffers.value()[i];
      camera3_stream_buffer_t* buffer = &output_buffers.at(i);
      int ret = DecodeStreamBufferPtr(buffer_ptr, buffer);
      if (ret) {
        LOGF(ERROR) << "Failed to decode output stream buffer";
        return;
      }
      if (camera3_streams_.count(buffer->stream) == 0) {
        LOGF(ERROR) << "Invalid stream";
        return;
      }
      base::AutoLock bufferLock(buffer_handle_map_lock_);
      buffer_handle_map_.erase(buffer_ptr->buffer_id);
    }
    capture_result.output_buffers =
        const_cast<const camera3_stream_buffer_t*>(output_buffers.data());
  }

  camera3_stream_buffer_t input_buffer;
  if (result->input_buffer.is_null()) {
    capture_result.input_buffer = nullptr;
  } else {
    int ret = DecodeStreamBufferPtr(result->input_buffer, &input_buffer);
    if (ret) {
      LOGF(ERROR) << "Failed to decode input stream buffer";
      return;
    }
    if (camera3_streams_.count(input_buffer.stream) == 0) {
      LOGF(ERROR) << "Invalid stream";
      return;
    }
    base::AutoLock bufferLock(buffer_handle_map_lock_);
    buffer_handle_map_.erase(result->input_buffer->buffer_id);
    capture_result.input_buffer = &input_buffer;
  }

  user_callback_ops_->process_capture_result(user_callback_ops_,
                                             &capture_result);
}

int ClientDeviceConnector::DecodeStreamBufferPtr(
    const cros::mojom::Camera3StreamBufferPtr& buffer_ptr,
    camera3_stream_buffer_t* buffer) {
  base::AutoLock bufferLock(buffer_handle_map_lock_);
  buffer->stream = reinterpret_cast<camera3_stream_t*>(buffer_ptr->stream_id);
  uint64_t buffer_id = buffer_ptr->buffer_id;
  if (buffer_handle_map_.count(buffer_id) == 0) {
    return -EINVAL;
  }
  buffer->buffer = buffer_handle_map_[buffer_id];
  buffer->status = static_cast<int>(buffer_ptr->status);
  if (buffer_ptr->acquire_fence.is_valid()) {
    buffer->acquire_fence =
        mojo::UnwrapPlatformHandle(std::move(buffer_ptr->acquire_fence))
            .ReleaseFD();
  } else {
    buffer->acquire_fence = -1;
  }

  if (buffer_ptr->release_fence.is_valid()) {
    buffer->acquire_fence =
        mojo::UnwrapPlatformHandle(std::move(buffer_ptr->release_fence))
            .ReleaseFD();
  } else {
    buffer->release_fence = -1;
  }
  return 0;
}

}  // namespace camera3_test
