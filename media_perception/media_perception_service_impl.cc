// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media_perception/media_perception_service_impl.h"

#include <base/bind.h>
#include <utility>

#include "media_perception/media_perception_controller_impl.h"

namespace mri {

namespace {

void OnConnectionClosedOrError(
    const MediaPerceptionControllerImpl* const controller) {
  LOG(WARNING) << "Got closed connection.";
  delete controller;
}

}  // namespace

MediaPerceptionServiceImpl::MediaPerceptionServiceImpl(
    mojo::ScopedMessagePipeHandle pipe,
    base::Closure connection_error_handler,
    std::shared_ptr<VideoCaptureServiceClient> video_capture_service_client,
    std::shared_ptr<ChromeAudioServiceClient> chrome_audio_service_client,
    std::shared_ptr<Rtanalytics> rtanalytics)
    : binding_(this,
               mojo::InterfaceRequest<
                   chromeos::media_perception::mojom::MediaPerceptionService>(
                   std::move(pipe))),
      video_capture_service_client_(video_capture_service_client),
      chrome_audio_service_client_(chrome_audio_service_client),
      rtanalytics_(rtanalytics) {
  binding_.set_connection_error_handler(std::move(connection_error_handler));
}

void MediaPerceptionServiceImpl::GetController(
    chromeos::media_perception::mojom::MediaPerceptionControllerRequest request,
    chromeos::media_perception::mojom::MediaPerceptionControllerClientPtr
        client) {
  client_ = std::move(client);

  // Use a connection error handler to strongly bind |controller| to |request|.
  MediaPerceptionControllerImpl* const controller =
      new MediaPerceptionControllerImpl(
          std::move(request), video_capture_service_client_,
          chrome_audio_service_client_, rtanalytics_);
  controller->set_connection_error_handler(
      base::Bind(&OnConnectionClosedOrError, base::Unretained(controller)));
}

void MediaPerceptionServiceImpl::ConnectToVideoCaptureService(
    video_capture::mojom::VideoSourceProviderRequest request) {
  if (client_)
    client_->ConnectToVideoCaptureService(std::move(request));
}

}  // namespace mri
