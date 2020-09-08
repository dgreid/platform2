// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_PERCEPTION_FAKE_RTANALYTICS_H_
#define MEDIA_PERCEPTION_FAKE_RTANALYTICS_H_

#include "media_perception/rtanalytics.h"

#include <string>
#include <vector>

#include "base/macros.h"

namespace mri {

class FakeRtanalytics : public Rtanalytics {
 public:
  FakeRtanalytics() = default;

  void SetSerializedDeviceTemplates(
      std::vector<SerializedDeviceTemplate> serialized_device_templates);
  std::string GetMostRecentOutputStreamName() const {
    return most_recent_output_stream_name_;
  }

  // Rtanalytics:
  SerializedPerceptionInterfaces SetupConfiguration(
      const std::string& configuration_name,
      SerializedSuccessStatus* success_status) override;
  SerializedSuccessStatus SetTemplateArguments(
      const std::string& configuration_name,
      const SerializedTemplateArguments& serialized_arguments) override;
  std::vector<SerializedDeviceTemplate> GetTemplateDevices(
      const std::string& configuration_name) const override;
  SerializedSuccessStatus SetVideoDeviceForTemplateName(
      const std::string& configuration_name,
      const std::string& template_name,
      const SerializedVideoDevice& video_device) override;
  SerializedSuccessStatus SetAudioDeviceForTemplateName(
      const std::string& configuration_name,
      const std::string& template_name,
      const SerializedAudioDevice& audio_device) override;
  SerializedSuccessStatus SetVirtualVideoDeviceForTemplateName(
      const std::string& configuration_name,
      const std::string& template_name,
      const SerializedVirtualVideoDevice& virtual_device) override;
  SerializedPipelineState GetPipelineState(
      const std::string& configuration_name) const override;
  SerializedPipelineState SetPipelineState(
      const std::string& configuration_name,
      const SerializedPipelineState& desired_state) override;
  SerializedSuccessStatus SetPipelineOutputHandler(
      const std::string& configuration_name,
      const std::string& output_stream,
      PipelineOutputHandler output_handler) override;
  SerializedGlobalPipelineState GetGlobalPipelineState() const override;

 private:
  // A list of device templates to be returned by GetTemplateDevices.
  std::vector<SerializedDeviceTemplate> serialized_device_templates_;

  std::string most_recent_output_stream_name_;

  DISALLOW_COPY_AND_ASSIGN(FakeRtanalytics);
};

}  // namespace mri

#endif  // MEDIA_PERCEPTION_FAKE_RTANALYTICS_H_
