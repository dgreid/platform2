// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media_perception/producer_impl.h"

#include <stdlib.h>
#include <utility>

#include "base/logging.h"
#include "mojom/constants.mojom.h"
#include "mojom/video_capture_types.mojom.h"

namespace mri {

video_capture::mojom::ProducerPtr ProducerImpl::CreateInterfacePtr() {
  video_capture::mojom::ProducerPtr server_ptr;
  binding_.Bind(mojo::MakeRequest(&server_ptr));
  return server_ptr;
}

void ProducerImpl::RegisterVirtualDevice(
    video_capture::mojom::VideoSourceProviderPtr* provider,
    media::mojom::VideoCaptureDeviceInfoPtr info) {
  (*provider)->AddSharedMemoryVirtualDevice(
      std::move(info), CreateInterfacePtr(), true,
      mojo::MakeRequest(&virtual_device_));
}

void ProducerImpl::OnNewBuffer(int32_t buffer_id,
                               media::mojom::VideoBufferHandlePtr buffer_handle,
                               OnNewBufferCallback callback) {
  CHECK(buffer_handle->is_shared_memory_via_raw_file_descriptor());
  std::unique_ptr<SharedMemoryProvider> shared_memory_provider =
      SharedMemoryProvider::CreateFromRawFileDescriptor(
          false /*read_only*/,
          std::move(buffer_handle->get_shared_memory_via_raw_file_descriptor()
                        ->file_descriptor_handle),
          buffer_handle->get_shared_memory_via_raw_file_descriptor()
              ->shared_memory_size_in_bytes);
  if (!shared_memory_provider) {
    LOG(ERROR) << "SharedMemoryProvider is nullptr.";
    return;
  }
  outgoing_buffer_id_to_buffer_map_.insert(
      std::make_pair(buffer_id, std::move(shared_memory_provider)));
  std::move(callback).Run();
}

void ProducerImpl::OnBufferRetired(int32_t buffer_id) {
  outgoing_buffer_id_to_buffer_map_.erase(buffer_id);
}

void ProducerImpl::PushNextFrame(
    std::shared_ptr<ProducerImpl> producer_impl,
    base::TimeDelta timestamp,
    std::unique_ptr<const uint8_t[]> data,
    int data_size,
    media::mojom::VideoCapturePixelFormat pixel_format,
    int width,
    int height) {
  gfx::mojom::SizePtr size = gfx::mojom::Size::New();
  size->width = width;
  size->height = height;
  virtual_device_->RequestFrameBuffer(
      std::move(size), pixel_format, nullptr,
      base::Bind(&ProducerImpl::OnFrameBufferReceived, base::Unretained(this),
                 producer_impl, timestamp, base::Passed(&data), data_size,
                 pixel_format, width, height));
}

void ProducerImpl::OnFrameBufferReceived(
    std::shared_ptr<ProducerImpl> producer_impl,
    base::TimeDelta timestamp,
    std::unique_ptr<const uint8_t[]> data,
    int data_size,
    media::mojom::VideoCapturePixelFormat pixel_format,
    int width,
    int height,
    int32_t buffer_id) {
  if (buffer_id == video_capture::mojom::kInvalidBufferId) {
    LOG(ERROR) << "Got invalid buffer id.";
    return;
  }

  media::mojom::VideoFrameInfoPtr info = media::mojom::VideoFrameInfo::New();
  info->timestamp = mojo_base::mojom::TimeDelta::New();
  info->timestamp->microseconds = timestamp.InMicroseconds();
  info->pixel_format = pixel_format;
  gfx::mojom::SizePtr size = gfx::mojom::Size::New();
  size->width = width;
  size->height = height;
  info->coded_size = std::move(size);
  gfx::mojom::RectPtr rect = gfx::mojom::Rect::New();
  rect->width = width;
  rect->height = height;
  info->visible_rect = std::move(rect);
  info->metadata = mojo_base::mojom::DictionaryValue::New();

  SharedMemoryProvider* outgoing_buffer =
      outgoing_buffer_id_to_buffer_map_.at(buffer_id).get();

  auto memory = static_cast<uint8_t*>(
      outgoing_buffer->GetSharedMemoryForInProcessAccess()->memory());
  memcpy(memory, data.get(), outgoing_buffer->GetMemorySizeInBytes());
  virtual_device_->OnFrameReadyInBuffer(buffer_id, std::move(info));
}

}  // namespace mri
