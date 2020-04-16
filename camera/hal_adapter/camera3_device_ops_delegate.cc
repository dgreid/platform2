/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/camera3_device_ops_delegate.h"

#include <inttypes.h>
#include <utility>
#include <vector>

#include <base/strings/stringprintf.h>

#include "cros-camera/common.h"
#include "hal_adapter/camera_device_adapter.h"
#include "hal_adapter/camera_trace_event.h"

namespace cros {

Camera3DeviceOpsDelegate::Camera3DeviceOpsDelegate(
    CameraDeviceAdapter* camera_device_adapter,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner)
    : internal::MojoBinding<Camera3DeviceOps>(task_runner),
      camera_device_adapter_(camera_device_adapter) {}

Camera3DeviceOpsDelegate::~Camera3DeviceOpsDelegate() {}

void Camera3DeviceOpsDelegate::Initialize(
    mojom::Camera3CallbackOpsPtr callback_ops, InitializeCallback callback) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();
  std::move(callback).Run(
      camera_device_adapter_->Initialize(std::move(callback_ops)));
}

void Camera3DeviceOpsDelegate::ConfigureStreams(
    mojom::Camera3StreamConfigurationPtr config,
    ConfigureStreamsCallback callback) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  for (const auto& stream : config->streams) {
    TRACE_CAMERA_SCOPED("stream_id", stream->id, "width", stream->width,
                        "height", stream->height, "format", stream->format);
  }
  mojom::Camera3StreamConfigurationPtr updated_config;
  int32_t result = camera_device_adapter_->ConfigureStreams(std::move(config),
                                                            &updated_config);
  std::move(callback).Run(result, std::move(updated_config));
}

void Camera3DeviceOpsDelegate::ConstructDefaultRequestSettings(
    mojom::Camera3RequestTemplate type,
    ConstructDefaultRequestSettingsCallback callback) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();
  std::move(callback).Run(
      camera_device_adapter_->ConstructDefaultRequestSettings(type));
}

void Camera3DeviceOpsDelegate::ProcessCaptureRequest(
    mojom::Camera3CaptureRequestPtr request,
    ProcessCaptureRequestCallback callback) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  for (const auto& output_buffer : request->output_buffers) {
    TRACE_CAMERA_ASYNC_BEGIN(base::StringPrintf("frame capture stream %" PRIu64,
                                                output_buffer->stream_id),
                             request->frame_number, "frame_number",
                             request->frame_number, "stream_id",
                             output_buffer->stream_id, "buffer_id",
                             output_buffer->buffer_id);
  }
  std::move(callback).Run(
      camera_device_adapter_->ProcessCaptureRequest(std::move(request)));
}

void Camera3DeviceOpsDelegate::Dump(mojo::ScopedHandle fd) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();
  camera_device_adapter_->Dump(std::move(fd));
}

void Camera3DeviceOpsDelegate::Flush(FlushCallback callback) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();
  std::move(callback).Run(camera_device_adapter_->Flush());
}

void Camera3DeviceOpsDelegate::RegisterBuffer(
    uint64_t buffer_id,
    mojom::Camera3DeviceOps::BufferType type,
    std::vector<mojo::ScopedHandle> fds,
    uint32_t drm_format,
    mojom::HalPixelFormat hal_pixel_format,
    uint32_t width,
    uint32_t height,
    const std::vector<uint32_t>& strides,
    const std::vector<uint32_t>& offsets,
    RegisterBufferCallback callback) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED("buffer_id", buffer_id);
  std::move(callback).Run(camera_device_adapter_->RegisterBuffer(
      buffer_id, type, std::move(fds), drm_format, hal_pixel_format, width,
      height, strides, offsets));
}

void Camera3DeviceOpsDelegate::Close(CloseCallback callback) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();
  std::move(callback).Run(camera_device_adapter_->Close());
}

void Camera3DeviceOpsDelegate::ConfigureStreamsAndGetAllocatedBuffers(
    mojom::Camera3StreamConfigurationPtr config,
    ConfigureStreamsAndGetAllocatedBuffersCallback callback) {
  VLOGF_ENTER();
  DCHECK(task_runner_->BelongsToCurrentThread());
  for (const auto& stream : config->streams) {
    TRACE_CAMERA_SCOPED("stream_id", stream->id, "width", stream->width,
                        "height", stream->height, "format", stream->format);
  }
  mojom::Camera3StreamConfigurationPtr updated_config;
  CameraDeviceAdapter::AllocatedBuffers allocated_buffers;
  int32_t result =
      camera_device_adapter_->ConfigureStreamsAndGetAllocatedBuffers(
          std::move(config), &updated_config, &allocated_buffers);
  std::move(callback).Run(result, std::move(updated_config),
                          std::move(allocated_buffers));
}

}  // namespace cros
