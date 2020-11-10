// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_PERCEPTION_MEDIA_PERCEPTION_SERVICE_IMPL_H_
#define MEDIA_PERCEPTION_MEDIA_PERCEPTION_SERVICE_IMPL_H_

#include <memory>
#include <mojo/public/cpp/bindings/binding.h>

#include "media_perception/chrome_audio_service_client.h"
#include "media_perception/rtanalytics.h"
#include "media_perception/video_capture_service_client.h"
#include "mojom/media_perception_service.mojom.h"

namespace mri {

class MediaPerceptionServiceImpl
    : public chromeos::media_perception::mojom::MediaPerceptionService {
 public:
  // Creates an instance bound to |pipe|. The specified
  // |connection_error_handler| will be invoked if the binding encounters a
  // connection error.
  MediaPerceptionServiceImpl(
      mojo::ScopedMessagePipeHandle pipe,
      base::Closure connection_error_handler,
      std::shared_ptr<VideoCaptureServiceClient> video_capture_service_client,
      std::shared_ptr<ChromeAudioServiceClient> chrome_audio_service_client,
      std::shared_ptr<Rtanalytics> rtanalytics);
  MediaPerceptionServiceImpl(const MediaPerceptionServiceImpl&) = delete;
  MediaPerceptionServiceImpl& operator=(const MediaPerceptionServiceImpl&) =
      delete;

  void ConnectToVideoCaptureService(
      video_capture::mojom::VideoSourceProviderRequest request);

  // chromeos::media_perception::mojom::MediaPerceptionService:
  void GetController(
      chromeos::media_perception::mojom::MediaPerceptionControllerRequest
          request,
      chromeos::media_perception::mojom::MediaPerceptionControllerClientPtr
          client) override;

 private:
  chromeos::media_perception::mojom::MediaPerceptionControllerClientPtr client_;

  mojo::Binding<chromeos::media_perception::mojom::MediaPerceptionService>
      binding_;

  std::shared_ptr<VideoCaptureServiceClient> video_capture_service_client_;
  std::shared_ptr<ChromeAudioServiceClient> chrome_audio_service_client_;

  std::shared_ptr<Rtanalytics> rtanalytics_;
};

}  // namespace mri

#endif  // MEDIA_PERCEPTION_MEDIA_PERCEPTION_SERVICE_IMPL_H_
