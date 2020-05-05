// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media_perception/output_manager.h"

#include <functional>

#include "media_perception/frame_perception.pb.h"
#include "media_perception/hotword_detection.pb.h"
#include "media_perception/occupancy_trigger.pb.h"
#include "media_perception/presence_perception.pb.h"
#include "media_perception/proto_mojom_conversion.h"
#include "media_perception/serialized_proto.h"

namespace mri {

namespace {

// To avoid passing a lambda as a base::Closure.
void OnConnectionClosedOrError(const std::string& interface_type) {
  LOG(INFO) << "Got closed connection: " << interface_type;
}

}  // namespace

OutputManager::OutputManager(
    const std::string& configuration_name,
    std::shared_ptr<Rtanalytics> rtanalytics,
    const PerceptionInterfaces& interfaces,
    chromeos::media_perception::mojom::PerceptionInterfacesPtr*
    interfaces_ptr) {
  for (const PerceptionInterface& interface : interfaces.interface()) {
    // Frame perception interface setup.
    if (interface.interface_type() ==
        PerceptionInterfaceType::INTERFACE_FRAME_PERCEPTION) {
      (*interfaces_ptr)->frame_perception_handler_request =
          mojo::MakeRequest(&frame_perception_handler_ptr_);
      frame_perception_handler_ptr_.set_connection_error_handler(
          base::Bind(&OnConnectionClosedOrError, "INTERFACE_FRAME_PERCEPTION"));

      // Frame perception outputs setup.
      for (const PipelineOutput& output : interface.output()) {
        if (output.output_type() ==
            PipelineOutputType::OUTPUT_FRAME_PERCEPTION) {
          SerializedSuccessStatus serialized_status =
              rtanalytics->SetPipelineOutputHandler(
              configuration_name, output.stream_name(),
              std::bind(&OutputManager::HandleFramePerception,
                        this, std::placeholders::_1));
          SuccessStatus status = Serialized<SuccessStatus>(
              serialized_status).Deserialize();
          if (!status.success()) {
            LOG(ERROR) << "Failed to set output handler for "
                       << configuration_name << " with output "
                       << output.stream_name();
          }
        }
      }
      continue;
    }

    // Hotword detection interface setup.
    if (interface.interface_type() ==
        PerceptionInterfaceType::INTERFACE_HOTWORD_DETECTION) {
      (*interfaces_ptr)->hotword_detection_handler_request =
          mojo::MakeRequest(&hotword_detection_handler_ptr_);
      hotword_detection_handler_ptr_.set_connection_error_handler(
          base::Bind(&OnConnectionClosedOrError,
                     "INTERFACE_HOTWORD_DETECTION"));

      // Hotword detection outputs setup.
      for (const PipelineOutput& output : interface.output()) {
        if (output.output_type() ==
            PipelineOutputType::OUTPUT_HOTWORD_DETECTION) {
          SerializedSuccessStatus serialized_status =
              rtanalytics->SetPipelineOutputHandler(
                  configuration_name, output.stream_name(),
                  std::bind(&OutputManager::HandleHotwordDetection,
                            this, std::placeholders::_1));
          SuccessStatus status = Serialized<SuccessStatus>(
              serialized_status).Deserialize();
          if (!status.success()) {
            LOG(ERROR) << "Failed to set output handler for "
                       << configuration_name << " with output "
                       << output.stream_name();
          }
        }
      }
      continue;
    }

    // Presence perception interface setup.
    if (interface.interface_type() ==
        PerceptionInterfaceType::INTERFACE_PRESENCE_PERCEPTION) {
      (*interfaces_ptr)->presence_perception_handler_request =
          mojo::MakeRequest(&presence_perception_handler_ptr_);
      presence_perception_handler_ptr_.set_connection_error_handler(
          base::Bind(&OnConnectionClosedOrError,
          "INTERFACE_PRESENCE_PERCEPTION"));

      // Presence perception outputs setup.
      for (const PipelineOutput& output : interface.output()) {
        if (output.output_type() ==
            PipelineOutputType::OUTPUT_PRESENCE_PERCEPTION) {
          SerializedSuccessStatus serialized_status =
              rtanalytics->SetPipelineOutputHandler(
                  configuration_name, output.stream_name(),
                  std::bind(&OutputManager::HandlePresencePerception,
                            this, std::placeholders::_1));
          SuccessStatus status = Serialized<SuccessStatus>(
              serialized_status).Deserialize();
          if (!status.success()) {
            LOG(ERROR) << "Failed to set output handler for "
                       << configuration_name << " with output "
                       << output.stream_name();
          }
        }
      }
      continue;
    }

    // Occupancy trigger interface setup.
    if (interface.interface_type() ==
        PerceptionInterfaceType::INTERFACE_OCCUPANCY_TRIGGER) {
      (*interfaces_ptr)->occupancy_trigger_handler_request =
          mojo::MakeRequest(&occupancy_trigger_handler_ptr_);
      occupancy_trigger_handler_ptr_.set_connection_error_handler(
          base::Bind(&OnConnectionClosedOrError,
          "INTERFACE_OCCUPANCY_TRIGGER"));

      // Occpancy trigger outputs setup.
      for (const PipelineOutput& output : interface.output()) {
        if (output.output_type() ==
            PipelineOutputType::OUTPUT_OCCUPANCY_TRIGGER) {
          SerializedSuccessStatus serialized_status =
              rtanalytics->SetPipelineOutputHandler(
                  configuration_name, output.stream_name(),
                  std::bind(&OutputManager::HandleOccupancyTrigger,
                            this, std::placeholders::_1));
          SuccessStatus status = Serialized<SuccessStatus>(
              serialized_status).Deserialize();
          if (!status.success()) {
            LOG(ERROR) << "Failed to set output handler for "
                       << configuration_name << " with output "
                       << output.stream_name();
          }
        }
      }
      continue;
    }

    // Appearances interface setup.
    if (interface.interface_type() ==
        PerceptionInterfaceType::INTERFACE_APPEARANCES) {
      (*interfaces_ptr)->appearances_handler_request =
          mojo::MakeRequest(&appearances_handler_ptr_);
      appearances_handler_ptr_.set_connection_error_handler(
          base::Bind(&OnConnectionClosedOrError,
          "APPEARANCES"));

      // Appearances outputs setup.
      for (const PipelineOutput& output : interface.output()) {
        if (output.output_type() ==
            PipelineOutputType::OUTPUT_APPEARANCES) {
          SerializedSuccessStatus serialized_status =
              rtanalytics->SetPipelineOutputHandler(
                  configuration_name, output.stream_name(),
                  std::bind(&OutputManager::HandleAppearances,
                            this, std::placeholders::_1));
          SuccessStatus status = Serialized<SuccessStatus>(
              serialized_status).Deserialize();
          if (!status.success()) {
            LOG(ERROR) << "Failed to set output handler for "
                       << configuration_name << " with output "
                       << output.stream_name();
          }
        }
      }
      continue;
    }

    // One touch Autozoom interface setup.
    if (interface.interface_type() ==
        PerceptionInterfaceType::INTERFACE_ONE_TOUCH_AUTOZOOM) {
      (*interfaces_ptr)->one_touch_autozoom_handler_request =
          mojo::MakeRequest(&one_touch_autozoom_handler_ptr_);
      one_touch_autozoom_handler_ptr_.set_connection_error_handler(
          base::Bind(&OnConnectionClosedOrError,
          "ONE_TOUCH_AUTOZOOM"));

      // One touch Autozoom outputs setup.
      for (const PipelineOutput& output : interface.output()) {
        if (output.output_type() ==
            PipelineOutputType::OUTPUT_SMART_FRAMING) {
          SerializedSuccessStatus serialized_status =
              rtanalytics->SetPipelineOutputHandler(
                  configuration_name, output.stream_name(),
                  std::bind(&OutputManager::HandleSmartFraming,
                            this, std::placeholders::_1));
          SuccessStatus status = Serialized<SuccessStatus>(
              serialized_status).Deserialize();
          if (!status.success()) {
            LOG(ERROR) << "Failed to set output handler for "
                       << configuration_name << " with output "
                       << output.stream_name();
          }
        }
      }
      continue;
    }

    // Software Autozoom interface setup.
    if (interface.interface_type() ==
        PerceptionInterfaceType::INTERFACE_SOFTWARE_AUTOZOOM) {
      (*interfaces_ptr)->software_autozoom_handler_request =
          mojo::MakeRequest(&software_autozoom_handler_ptr_);
      software_autozoom_handler_ptr_.set_connection_error_handler(
          base::Bind(&OnConnectionClosedOrError,
          "SOFTWARE_AUTOZOOM"));

      // Software Autozoom outputs setup.
      for (const PipelineOutput& output : interface.output()) {
        if (output.output_type() ==
            PipelineOutputType::OUTPUT_SMART_FRAMING) {
          SerializedSuccessStatus serialized_status =
              rtanalytics->SetPipelineOutputHandler(
                  configuration_name, output.stream_name(),
                  std::bind(&OutputManager::HandleSmartFraming,
                            this, std::placeholders::_1));
          SuccessStatus status = Serialized<SuccessStatus>(
              serialized_status).Deserialize();
          if (!status.success()) {
            LOG(ERROR) << "Failed to set output handler for "
                       << configuration_name << " with output "
                       << output.stream_name();
          }
        }
      }
      continue;
    }

  }
}

void OutputManager::HandleFramePerception(const std::vector<uint8_t>& bytes) {
  if (!frame_perception_handler_ptr_.is_bound()) {
    LOG(WARNING) << "Got frame perception output but handler ptr is not bound.";
    return;
  }

  if (frame_perception_handler_ptr_.get() == nullptr) {
    LOG(ERROR) << "Handler ptr is null.";
    return;
  }

  FramePerception frame_perception =
      Serialized<FramePerception>(bytes).Deserialize();
  frame_perception_handler_ptr_->OnFramePerception(
      chromeos::media_perception::mojom::ToMojom(frame_perception));
}

void OutputManager::HandleHotwordDetection(const std::vector<uint8_t>& bytes) {
  if (!hotword_detection_handler_ptr_.is_bound()) {
    LOG(WARNING)
        << "Got hotword detection output but handler ptr is not bound.";
    return;
  }

  if (hotword_detection_handler_ptr_.get() == nullptr) {
    LOG(ERROR) << "Handler ptr is null.";
    return;
  }

  HotwordDetection hotword_detection =
      Serialized<HotwordDetection>(bytes).Deserialize();
  hotword_detection_handler_ptr_->OnHotwordDetection(
      chromeos::media_perception::mojom::ToMojom(hotword_detection));
}

void OutputManager::HandlePresencePerception(
    const std::vector<uint8_t>& bytes) {
  if (!presence_perception_handler_ptr_.is_bound()) {
    LOG(WARNING)
        << "Got presence perception output but handler ptr is not bound.";
    return;
  }

  if (presence_perception_handler_ptr_.get() == nullptr) {
    LOG(ERROR) << "Handler ptr is null.";
    return;
  }

  PresencePerception presence_perception =
      Serialized<PresencePerception>(bytes).Deserialize();
  presence_perception_handler_ptr_->OnPresencePerception(
      chromeos::media_perception::mojom::ToMojom(presence_perception));
}

void OutputManager::HandleOccupancyTrigger(
    const std::vector<uint8_t>& bytes) {
  if (!occupancy_trigger_handler_ptr_.is_bound()) {
    LOG(WARNING)
        << "Got occupancy trigger output but handler ptr is not bound.";
    return;
  }

  if (occupancy_trigger_handler_ptr_.get() == nullptr) {
    LOG(ERROR) << "Handler ptr is null.";
    return;
  }

  OccupancyTrigger occupancy_trigger =
      Serialized<OccupancyTrigger>(bytes).Deserialize();
  occupancy_trigger_handler_ptr_->OnOccupancyTrigger(
      chromeos::media_perception::mojom::ToMojom(occupancy_trigger));
}

void OutputManager::HandleAppearances(
    const std::vector<uint8_t>& bytes) {
  if (!appearances_handler_ptr_.is_bound()) {
    LOG(WARNING)
        << "Got appearances but handler ptr is not bound.";
    return;
  }

  if (appearances_handler_ptr_.get() == nullptr) {
    LOG(ERROR) << "Handler ptr is null.";
    return;
  }

  appearances_handler_ptr_->OnAppearances(bytes);
}

void OutputManager::HandleSmartFraming(
    const std::vector<uint8_t>& bytes) {
  if (one_touch_autozoom_handler_ptr_.is_bound() &&
      one_touch_autozoom_handler_ptr_.get() != nullptr) {
   one_touch_autozoom_handler_ptr_->OnSmartFraming(bytes);
  } else if (software_autozoom_handler_ptr_.is_bound() &&
      software_autozoom_handler_ptr_.get() != nullptr) {
   software_autozoom_handler_ptr_->OnSmartFraming(bytes);
  } else {
    LOG(WARNING)
        << "Got smart framing but handler ptr is not bound.";
  }
}

}  // namespace mri
