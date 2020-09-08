// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_PERCEPTION_MEDIA_PERCEPTION_IMPL_H_
#define MEDIA_PERCEPTION_MEDIA_PERCEPTION_IMPL_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <mojo/public/cpp/bindings/binding.h>

#include "media_perception/chrome_audio_service_client.h"
#include "media_perception/output_manager.h"
#include "media_perception/rtanalytics.h"
#include "media_perception/video_capture_service_client.h"
#include "mojom/media_perception.mojom.h"

namespace mri {

class MediaPerceptionImpl
    : public chromeos::media_perception::mojom::MediaPerception {
 public:
  MediaPerceptionImpl(
      chromeos::media_perception::mojom::MediaPerceptionRequest request,
      std::shared_ptr<VideoCaptureServiceClient> vidcap_client,
      std::shared_ptr<ChromeAudioServiceClient> cras_client,
      std::shared_ptr<Rtanalytics> rtanalytics);

  // chromeos::media_perception::mojom::MediaPerception:
  void SetupConfiguration(const std::string& configuration_name,
                          SetupConfigurationCallback callback) override;
  void SetTemplateArguments(
      const std::string& configuration_name,
      const std::vector<uint8_t>& serialized_arguments_proto,
      SetTemplateArgumentsCallback callback) override;
  void GetVideoDevices(GetVideoDevicesCallback callback) override;
  void GetAudioDevices(GetAudioDevicesCallback callback) override;
  void GetTemplateDevices(const std::string& configuration_name,
                          GetTemplateDevicesCallback callback) override;
  void SetVideoDeviceForTemplateName(
      const std::string& configuration_name,
      const std::string& template_name,
      chromeos::media_perception::mojom::VideoDevicePtr device,
      SetVideoDeviceForTemplateNameCallback callback) override;
  void SetAudioDeviceForTemplateName(
      const std::string& configuration_name,
      const std::string& template_name,
      chromeos::media_perception::mojom::AudioDevicePtr device,
      SetAudioDeviceForTemplateNameCallback callback) override;
  void SetVirtualVideoDeviceForTemplateName(
      const std::string& configuration_name,
      const std::string& template_name,
      chromeos::media_perception::mojom::VirtualVideoDevicePtr device,
      SetVirtualVideoDeviceForTemplateNameCallback callback) override;
  void GetPipelineState(const std::string& configuration_name,
                        GetPipelineStateCallback callback) override;
  void SetPipelineState(
      const std::string& configuration_name,
      chromeos::media_perception::mojom::PipelineStatePtr desired_state,
      SetPipelineStateCallback callback) override;
  void GetGlobalPipelineState(GetGlobalPipelineStateCallback callback) override;

  void set_connection_error_handler(base::Closure connection_error_handler);

 private:
  mojo::Binding<chromeos::media_perception::mojom::MediaPerception> binding_;

  std::map<std::string /* configuration_name */, std::unique_ptr<OutputManager>>
      configuration_name_to_output_manager_map_;

  std::shared_ptr<VideoCaptureServiceClient> vidcap_client_;

  std::shared_ptr<ChromeAudioServiceClient> cras_client_;

  std::shared_ptr<Rtanalytics> rtanalytics_;

  DISALLOW_COPY_AND_ASSIGN(MediaPerceptionImpl);
};

}  // namespace mri

#endif  // MEDIA_PERCEPTION_MEDIA_PERCEPTION_IMPL_H_
