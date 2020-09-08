// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_PERCEPTION_VIDEO_FRAME_HANDLER_IMPL_H_
#define MEDIA_PERCEPTION_VIDEO_FRAME_HANDLER_IMPL_H_

#include <base/memory/shared_memory.h>
#include <map>
#include <memory>
#include <mojo/public/cpp/bindings/binding.h>
#include <string>

#include "base/logging.h"
#include "media_perception/device_management.pb.h"
#include "media_perception/shared_memory_provider.h"
#include "media_perception/video_capture_service_client.h"
#include "mojom/device_factory.mojom.h"
#include "mojom/video_frame_handler.mojom.h"

namespace mri {

class VideoFrameHandlerImpl : public video_capture::mojom::VideoFrameHandler {
 public:
  VideoFrameHandlerImpl() : frame_handler_id_counter_(0), binding_(this) {}

  bool HasValidCaptureFormat();

  void SetCaptureFormat(const VideoStreamParams& params);

  VideoStreamParams GetCaptureFormat();

  // Checks if the frame dimensions match the current dimensions.
  bool CaptureFormatsMatch(const VideoStreamParams& params);

  // Creates a local proxy of the VideoFrameHandlerPtr interface.
  video_capture::mojom::VideoFrameHandlerPtr CreateInterfacePtr();

  // Returns the count of active frame handlers on this handler.
  int GetFrameHandlerCount();

  // Add a handler that will be called when new frames come from the associated
  // device. Return value is an id for this frame handler.
  int AddFrameHandler(VideoCaptureServiceClient::FrameHandler frame_handler);

  // Removes a frame handler on this device with this id. Return value indicates
  // if the removal was successful.
  bool RemoveFrameHandler(int frame_handler_id);

  // video_capture::mojom::VideoFrameHandler overrides.
  void OnNewBuffer(int32_t buffer_id,
                   media::mojom::VideoBufferHandlePtr buffer_handle) override;
  void OnFrameReadyInBuffer(
      int32_t buffer_id,
      int32_t frame_feedback_id,
      video_capture::mojom::ScopedAccessPermissionPtr permission,
      media::mojom::VideoFrameInfoPtr frame_info) override;
  void OnFrameDropped(
      ::media::mojom::VideoCaptureFrameDropReason reason) override;
  void OnBufferRetired(int32_t buffer_id) override;
  void OnError(::media::mojom::VideoCaptureError error) override;
  void OnLog(const std::string& message) override;
  void OnStarted() override;
  void OnStartedUsingGpuDecode() override;
  void OnStopped() override;

 private:
  // Incremented to create unique frame handler ids.
  int frame_handler_id_counter_;

  // Frame handler map for forwarding frames to one or more clients.
  std::map<int, VideoCaptureServiceClient::FrameHandler> frame_handler_map_;

  // Binding of the Recevier interface to message pipe.
  mojo::Binding<video_capture::mojom::VideoFrameHandler> binding_;

  // Stores the capture format requested from the open device.
  VideoStreamParams capture_format_;

  std::map<int32_t /*buffer_id*/, std::unique_ptr<SharedMemoryProvider>>
      incoming_buffer_id_to_buffer_map_;
};

}  // namespace mri

#endif  // MEDIA_PERCEPTION_VIDEO_FRAME_HANDLER_IMPL_H_
