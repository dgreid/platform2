/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/frame_number_mapper.h"

#include <algorithm>
#include <utility>

#include "cros-camera/common.h"

namespace cros {

FrameNumberMapper::FrameNumberMapper() : next_frame_number_(0) {}

uint32_t FrameNumberMapper::GetHalFrameNumber(uint32_t framework_frame_number) {
  base::AutoLock l(frame_number_lock_);
  uint32_t hal_frame_number =
      std::max(framework_frame_number, next_frame_number_);
  frame_number_map_[hal_frame_number] = framework_frame_number;

  next_frame_number_ = hal_frame_number + 1;
  return hal_frame_number;
}

uint32_t FrameNumberMapper::GetFrameworkFrameNumber(uint32_t hal_frame_number) {
  base::AutoLock l(frame_number_lock_);
  auto it = frame_number_map_.find(hal_frame_number);
  return it != frame_number_map_.end() ? it->second : 0;
}

bool FrameNumberMapper::IsAddedFrame(uint32_t hal_frame_number) {
  base::AutoLock l(added_frame_numbers_lock_);
  return added_frame_numbers_.find(hal_frame_number) !=
         added_frame_numbers_.end();
}

void FrameNumberMapper::RegisterCaptureRequest(
    const camera3_capture_request_t* request,
    bool is_request_split,
    bool is_request_added) {
  {
    base::AutoLock l(pending_result_status_lock_);
    ResultStatus status{
        .has_pending_input_buffer = request->input_buffer != nullptr,
        .num_pending_output_buffers = request->num_output_buffers,
        .has_pending_result = true};
    pending_result_status_[request->frame_number] = std::move(status);
  }
  if (is_request_split) {
    base::AutoLock l(request_streams_map_lock_);
    auto& streams = request_streams_map_[request->frame_number];
    streams.resize(request->num_output_buffers);
    for (size_t i = 0; i < request->num_output_buffers; ++i) {
      streams[i] = request->output_buffers[i].stream;
    }
  }
  if (is_request_added) {
    base::AutoLock l(added_frame_numbers_lock_);
    added_frame_numbers_.insert(request->frame_number);
  }
}

void FrameNumberMapper::RegisterCaptureResult(
    const camera3_capture_result_t* result, int32_t partial_result_count) {
  base::AutoLock l(pending_result_status_lock_);
  auto it = pending_result_status_.find(result->frame_number);
  if (it == pending_result_status_.end()) {
    LOGF(ERROR) << "This result wasn't registered in frame number mapper";
    return;
  }

  // Update the status of this result.
  ResultStatus& status = it->second;
  if (result->partial_result == partial_result_count) {
    status.has_pending_result = false;
  }
  status.num_pending_output_buffers -= result->num_output_buffers;
  if (result->input_buffer) {
    status.has_pending_input_buffer = false;
  }

  // See if we can remove the frame number mapping of this frame.
  if (!status.has_pending_result && status.num_pending_output_buffers == 0 &&
      !status.has_pending_input_buffer) {
    FinishHalFrameNumber(result->frame_number);
    pending_result_status_.erase(it);
  }
}

void FrameNumberMapper::PreprocessNotifyMsg(
    const camera3_notify_msg_t* msg,
    std::vector<camera3_notify_msg_t>* msgs,
    camera3_stream_t* zsl_stream) {
  if (msg->type == CAMERA3_MSG_SHUTTER) {
    const camera3_shutter_msg_t& shutter = msg->message.shutter;
    if (!IsAddedFrame(shutter.frame_number)) {
      msgs->push_back(*msg);
      msgs->back().message.shutter.frame_number =
          GetFrameworkFrameNumber(shutter.frame_number);
    }
  } else if (msg->type == CAMERA3_MSG_ERROR) {
    const camera3_error_msg_t& error = msg->message.error;
    auto MarkResultReady = [&]() {
      base::AutoLock l(pending_result_status_lock_);
      auto it = pending_result_status_.find(error.frame_number);
      if (it != pending_result_status_.end()) {
        auto& status = it->second;
        status.has_pending_result = false;
        if (!status.has_pending_result &&
            status.num_pending_output_buffers == 0 &&
            !status.has_pending_input_buffer) {
          FinishHalFrameNumber(error.frame_number);
        }
      }
    };
    uint32_t framework_frame_number =
        error.frame_number == 0 ? 0
                                : GetFrameworkFrameNumber(error.frame_number);
    if (!IsRequestSplit(error.frame_number)) {
      if (error.error_code == CAMERA3_MSG_ERROR_REQUEST ||
          error.error_code == CAMERA3_MSG_ERROR_RESULT) {
        MarkResultReady();
      }
      if (error.error_code == CAMERA3_MSG_ERROR_BUFFER &&
          error.error_stream == zsl_stream) {
        LOGF(ERROR) << "HAL fails to fill in ZSL output buffer!";
      } else {
        msgs->push_back(*msg);
        msgs->back().message.error.frame_number = framework_frame_number;
      }
      return;
    }
    auto ConvertToMsgErrorBuffer = [&](camera3_stream_t* stream) {
      if (stream != zsl_stream) {
        camera3_notify_msg_t msg{
            .type = CAMERA3_MSG_ERROR,
            .message.error = {.frame_number = framework_frame_number,
                              .error_stream = stream,
                              .error_code = CAMERA3_MSG_ERROR_BUFFER}};
        msgs->push_back(std::move(msg));
      }
    };
    auto ConvertToMsgErrorResult = [&]() {
      MarkResultReady();
      camera3_notify_msg_t msg{
          .type = CAMERA3_MSG_ERROR,
          .message.error = {.frame_number = framework_frame_number,
                            .error_stream = nullptr,
                            .error_code = CAMERA3_MSG_ERROR_RESULT}};
      msgs->push_back(std::move(msg));
    };
    switch (error.error_code) {
      case CAMERA3_MSG_ERROR_DEVICE:
        DCHECK_EQ(msg->message.error.frame_number, 0);
        msgs->push_back(*msg);
        break;
      case CAMERA3_MSG_ERROR_REQUEST: {
        base::AutoLock l(request_streams_map_lock_);
        const std::vector<camera3_stream_t*>& streams =
            request_streams_map_[error.frame_number];
        for (camera3_stream_t* stream : streams) {
          ConvertToMsgErrorBuffer(stream);
        }
        if (!IsAddedFrame(error.frame_number)) {
          ConvertToMsgErrorResult();
        }
      } break;
      case CAMERA3_MSG_ERROR_RESULT:
        if (!IsAddedFrame(error.frame_number)) {
          ConvertToMsgErrorResult();
        }
        break;
      case CAMERA3_MSG_ERROR_BUFFER:
        ConvertToMsgErrorBuffer(error.error_stream);
        break;
    }
  }
}

void FrameNumberMapper::FinishHalFrameNumber(uint32_t hal_frame_number) {
  {
    base::AutoLock frame_number_lock(frame_number_lock_);
    base::AutoLock added_frame_numbers_lock(added_frame_numbers_lock_);
    frame_number_map_.erase(hal_frame_number);
    added_frame_numbers_.erase(hal_frame_number);
  }
  {
    base::AutoLock request_streams_map_lock(request_streams_map_lock_);
    request_streams_map_.erase(hal_frame_number);
  }
}

bool FrameNumberMapper::IsRequestSplit(uint32_t hal_frame_number) {
  base::AutoLock l(request_streams_map_lock_);
  return request_streams_map_.find(hal_frame_number) !=
         request_streams_map_.end();
}

}  // namespace cros
