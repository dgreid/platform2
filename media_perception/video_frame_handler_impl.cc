// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media_perception/video_frame_handler_impl.h"

#include <utility>

#include "base/logging.h"
#include "mojom/scoped_access_permission.mojom.h"
#include "mojom/video_capture_types.mojom.h"

namespace mri {

bool VideoFrameHandlerImpl::HasValidCaptureFormat() {
  return capture_format_.width_in_pixels() > 0 &&
         capture_format_.height_in_pixels() > 0;
}

void VideoFrameHandlerImpl::SetCaptureFormat(const VideoStreamParams& params) {
  capture_format_ = params;
}

bool VideoFrameHandlerImpl::CaptureFormatsMatch(
    const VideoStreamParams& params) {
  return capture_format_.width_in_pixels() == params.width_in_pixels() &&
         capture_format_.height_in_pixels() == params.height_in_pixels() &&
         capture_format_.frame_rate_in_frames_per_second() ==
             params.frame_rate_in_frames_per_second();
}

VideoStreamParams VideoFrameHandlerImpl::GetCaptureFormat() {
  return capture_format_;
}

int VideoFrameHandlerImpl::GetFrameHandlerCount() {
  return frame_handler_map_.size();
}

int VideoFrameHandlerImpl::AddFrameHandler(
    VideoCaptureServiceClient::FrameHandler frame_handler) {
  frame_handler_id_counter_++;
  frame_handler_map_.insert(
      std::make_pair(frame_handler_id_counter_, std::move(frame_handler)));
  return frame_handler_id_counter_;
}

bool VideoFrameHandlerImpl::RemoveFrameHandler(int frame_handler_id) {
  std::map<int, VideoCaptureServiceClient::FrameHandler>::iterator it =
      frame_handler_map_.find(frame_handler_id);
  if (it == frame_handler_map_.end()) {
    return false;
  }
  frame_handler_map_.erase(frame_handler_id);
  return true;
}

video_capture::mojom::VideoFrameHandlerPtr
VideoFrameHandlerImpl::CreateInterfacePtr() {
  video_capture::mojom::VideoFrameHandlerPtr server_ptr;
  binding_.Bind(mojo::MakeRequest(&server_ptr));
  return server_ptr;
}

void VideoFrameHandlerImpl::OnNewBuffer(
    int32_t buffer_id, media::mojom::VideoBufferHandlePtr buffer_handle) {
  LOG(INFO) << "On new buffer";
  CHECK(buffer_handle->is_shared_memory_via_raw_file_descriptor());
  std::unique_ptr<SharedMemoryProvider> shared_memory_provider =
      SharedMemoryProvider::CreateFromRawFileDescriptor(
          true /*read_only*/,
          std::move(buffer_handle->get_shared_memory_via_raw_file_descriptor()
                        ->file_descriptor_handle),
          buffer_handle->get_shared_memory_via_raw_file_descriptor()
              ->shared_memory_size_in_bytes);
  if (!shared_memory_provider) {
    LOG(ERROR) << "SharedMemoryProvider is nullptr.";
    return;
  }
  incoming_buffer_id_to_buffer_map_.insert(
      std::make_pair(buffer_id, std::move(shared_memory_provider)));
}

void VideoFrameHandlerImpl::OnFrameReadyInBuffer(
    int32_t buffer_id,
    int32_t frame_feedback_id,
    video_capture::mojom::ScopedAccessPermissionPtr permission,
    media::mojom::VideoFrameInfoPtr frame_info) {
  SharedMemoryProvider* incoming_buffer =
      incoming_buffer_id_to_buffer_map_.at(buffer_id).get();
  // Loop through all the registered frame handlers and push a frame out.
  std::map<int, VideoCaptureServiceClient::FrameHandler>::iterator it;
  for (it = frame_handler_map_.begin(); it != frame_handler_map_.end(); it++) {
    it->second(
        frame_info->timestamp->microseconds,
        static_cast<const uint8_t*>(
            incoming_buffer->GetSharedMemoryForInProcessAccess()->memory()),
        incoming_buffer->GetMemorySizeInBytes(),
        capture_format_.width_in_pixels(), capture_format_.height_in_pixels());
  }
}

void VideoFrameHandlerImpl::OnFrameDropped(
    ::media::mojom::VideoCaptureFrameDropReason reason) {
  LOG(WARNING) << "Got call to OnFrameDropped: " << reason;
}

void VideoFrameHandlerImpl::OnBufferRetired(int32_t buffer_id) {
  incoming_buffer_id_to_buffer_map_.erase(buffer_id);
}

// The following methods are not needed to be implementated, as far as we know
// now.
void VideoFrameHandlerImpl::OnError(::media::mojom::VideoCaptureError error) {
  LOG(ERROR) << "Got call to OnError: " << error;
}

void VideoFrameHandlerImpl::OnLog(const std::string& message) {
  LOG(INFO) << "Got call to OnLog: " << message;
}

void VideoFrameHandlerImpl::OnStarted() {
  LOG(INFO) << "Got call to OnStarted";
}

void VideoFrameHandlerImpl::OnStartedUsingGpuDecode() {
  LOG(INFO) << "Got call on OnStartedUsingGpuDecode";
}

void VideoFrameHandlerImpl::OnStopped() {
  LOG(INFO) << "Got call to OnStopped";
}

}  // namespace mri
