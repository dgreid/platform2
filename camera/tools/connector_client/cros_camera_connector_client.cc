/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <linux/videodev2.h>
#include <sysexits.h>

#include <cstring>
#include <string>

#include <base/bind.h>
#include <base/files/file.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>

#include "cros-camera/common.h"
#include "tools/connector_client/cros_camera_connector_client.h"

namespace cros {

int OnGotCameraInfo(void* context,
                    const cros_cam_info_t* info,
                    unsigned is_removed) {
  // TODO(b/151047930): Support hot-plugging when external camera is supported.
  CHECK_EQ(is_removed, 0u);

  auto GetDrmFormatName = [](uint32_t fourcc) {
    std::string result = "0000";
    for (size_t i = 0; i < 4; ++i, fourcc >>= 8) {
      const char c = static_cast<char>(fourcc & 0xFF);
      if (c <= 0x1f || c >= 0x7f) {
        return base::StringPrintf("0x%x", fourcc);
      }
      result[i] = c;
    }
    return result;
  };

  LOGF(INFO) << "Gotten camera info of " << info->id
             << " (name = " << info->name
             << ", format_count = " << info->format_count << ")";
  for (unsigned i = 0; i < info->format_count; ++i) {
    LOGF(INFO) << "format = " << GetDrmFormatName(info->format_info[i].fourcc)
               << ", width = " << info->format_info[i].width
               << ", height = " << info->format_info[i].height
               << ", fps = " << info->format_info[i].fps;
  }

  auto* client = reinterpret_cast<CrosCameraConnectorClient*>(context);
  client->SetCamInfo(info);
  return 0;
}

int OnCaptureResultAvailable(void* context,
                             const cros_cam_capture_result_t* result) {
  static uint32_t frame_count = 0;

  CHECK_EQ(result->status, 0);
  const cros_cam_frame_t* frame = result->frame;
  CHECK_NE(frame, nullptr);
  LOGF(INFO) << "Frame Available";

  auto* client = reinterpret_cast<CrosCameraConnectorClient*>(context);
  client->ProcessFrame(frame);

  frame_count++;
  if (frame_count == 10) {
    frame_count = 0;
    LOGF(INFO) << "Restarting capture";
    client->RestartCapture();
  }
  return 0;
}

CrosCameraConnectorClient::CrosCameraConnectorClient()
    : capture_thread_("CamConnClient"), num_restarts_(0) {}

int CrosCameraConnectorClient::OnInit() {
  int res = brillo::Daemon::OnInit();
  if (res != EX_OK) {
    return res;
  }

  if (!capture_thread_.Start()) {
    LOGF(FATAL) << "Failed to start capture thread";
  }

  res = cros_cam_init();
  if (res != 0) {
    return EX_UNAVAILABLE;
  }

  res = cros_cam_get_cam_info(&OnGotCameraInfo, this);
  if (res != 0) {
    return EX_UNAVAILABLE;
  }

  CHECK(!camera_device_list_.empty());
  request_device_iter_ = camera_device_list_.begin();
  request_format_iter_ = format_info_map_[*request_device_iter_].begin();
  while (request_device_iter_ != camera_device_list_.end() &&
         request_format_iter_ ==
             format_info_map_[*request_device_iter_].end()) {
    request_device_iter_++;
    request_format_iter_ = format_info_map_[*request_device_iter_].begin();
  }
  CHECK(request_device_iter_ != camera_device_list_.end());

  capture_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CrosCameraConnectorClient::StartCaptureOnThread,
                     base::Unretained(this)));

  return EX_OK;
}

void CrosCameraConnectorClient::OnShutdown(int* exit_code) {
  capture_thread_.Stop();
  cros_cam_exit();
}

void CrosCameraConnectorClient::SetCamInfo(const cros_cam_info_t* info) {
  camera_device_list_.push_back(info->id);
  auto& format_info = format_info_map_[info->id];
  format_info = {info->format_info, info->format_info + info->format_count};
}

void CrosCameraConnectorClient::RestartCapture() {
  capture_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CrosCameraConnectorClient::RestartCaptureOnThread,
                     base::Unretained(this)));
}

void CrosCameraConnectorClient::ProcessFrame(const cros_cam_frame_t* frame) {
  static const char kJpegFilePattern[] = "/tmp/connector_#.jpg";
  static const char kNv12FilePattern[] = "/tmp/connector_#.yuv";
  static int frame_iter = 0;

  if (frame->format.fourcc == V4L2_PIX_FMT_MJPEG) {
    std::string output_path(kJpegFilePattern);
    base::ReplaceSubstringsAfterOffset(&output_path, /*start_offset=*/0, "#",
                                       base::StringPrintf("%06u", frame_iter));
    base::File file(base::FilePath(output_path),
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    file.WriteAtCurrentPos(reinterpret_cast<char*>(frame->planes[0].data),
                           frame->planes[0].size);
    LOGF(INFO) << "Saved JPEG: " << output_path
               << "  (size = " << frame->planes[0].size << ")";
  } else if (frame->format.fourcc == V4L2_PIX_FMT_NV12) {
    std::string output_path(kNv12FilePattern);
    base::ReplaceSubstringsAfterOffset(&output_path, /*start_offset=*/0, "#",
                                       base::StringPrintf("%06u", frame_iter));
    base::File file(base::FilePath(output_path),
                    base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
    file.WriteAtCurrentPos(
        reinterpret_cast<const char*>(frame->planes[0].data),
        request_format_iter_->height * frame->planes[0].stride);
    file.WriteAtCurrentPos(
        reinterpret_cast<const char*>(frame->planes[1].data),
        (request_format_iter_->height + 1) / 2 * frame->planes[1].stride);
    LOGF(INFO) << "Saved YUV (NV12): " << output_path;
  }

  frame_iter++;
}

void CrosCameraConnectorClient::StartCaptureOnThread() {
  CHECK(capture_thread_.task_runner()->BelongsToCurrentThread());

  LOGF(INFO) << "Startin capture: device = " << (*request_device_iter_)
             << ", fourcc = " << request_format_iter_->fourcc
             << ", width = " << request_format_iter_->width
             << ", height = " << request_format_iter_->height
             << ", fps = " << request_format_iter_->fps;

  const cros_cam_capture_request_t request = {
      .id = *request_device_iter_,
      .format = &(*request_format_iter_),
  };
  cros_cam_start_capture(&request, &OnCaptureResultAvailable, this);
}

void CrosCameraConnectorClient::StopCaptureOnThread() {
  CHECK(capture_thread_.task_runner()->BelongsToCurrentThread());

  cros_cam_stop_capture(*request_device_iter_);
}

void CrosCameraConnectorClient::RestartCaptureOnThread() {
  CHECK(capture_thread_.task_runner()->BelongsToCurrentThread());
  ++num_restarts_;
  LOGF(INFO) << "Restarting capture #" << num_restarts_;
  StopCaptureOnThread();
  // TODO(b/151047930): Test the start/stop capture sequence with gtest.
  request_format_iter_++;
  if (request_format_iter_ == format_info_map_[*request_device_iter_].end()) {
    request_device_iter_++;
    if (request_device_iter_ == camera_device_list_.end()) {
      LOGF(INFO) << "Finished all captures";
      Quit();
      return;
    }
    request_format_iter_ = format_info_map_[*request_device_iter_].begin();
  }
  StartCaptureOnThread();
}

}  // namespace cros

int main() {
  cros::CrosCameraConnectorClient connector_client;
  return connector_client.Run();
}
