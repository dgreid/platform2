/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_H_
#define CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_H_

#include <list>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <vector>

#include <base/bind.h>
#include <base/synchronization/lock.h>
#include <base/threading/thread.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "common/libcamera_connector/camera_client_ops.h"
#include "common/libcamera_connector/types.h"
#include "cros-camera/camera_service_connector.h"
#include "mojo/camera_common.mojom.h"
#include "mojo/cros_camera_service.mojom.h"

namespace cros {

// CameraClient encapsulates the primary functionalities of a camera client. It
// fetches and manages the static information of the cameras connected to the
// device. It also handles the synchronization around the starting and stopping
// of a capture session.
//
// Expected usage of this class:
//   1. During initialization, the user of this class is expected to call Init()
//      with a callback to receive mojom::CameraHalClientPtr and register it
//      with CameraHalDispatcher. Init() is expected to be called exactly once.
//   2. CameraHalDispatcher should call SetUpChannel() to provide
//      mojom::CameraModulePtr and CameraClient would query and store the static
//      metadata of cameras.
//   3. The user can register a callback with SetCameraInfoCallback() to receive
//      updates about cameras, including the addition or removal of a camera
//      and/or its static metadata.
//   4. Capture can be started or stopped with StartCapture() and StopCapture().
//      These calls can be called unlimited number of times before Exit() is
//      called. Note that StartCapture() should not be called consecutively.
//      There can only one active capture session any any given time.
//   5. During shutdown, Exit() is called and CameraClient would make sure the
//      cameras are closed before the return of the call.
class CameraClient final : public mojom::CameraHalClient {
 public:
  using RegisterClientCallback =
      base::OnceCallback<void(mojom::CameraHalClientPtr)>;

  CameraClient();

  // Starts the thread and initializes the HAL client.
  void Init(RegisterClientCallback register_client_callback,
            IntOnceCallback init_callback);

  // Disconnects the client from camera HAL dispatcher.
  int Exit();

  // Sets the callback for camera info changes and fires |callback| with the
  // info of the cameras currently present.
  int SetCameraInfoCallback(cros_cam_get_cam_info_cb_t callback, void* context);

  // Starts capturing with the given parameters. Blocks until the device is
  // opened.
  // We recommend that all calls to StartCapture and StopCapture be
  // sequenced, but in case they're not, the calls will be fulfilled in a FIFO
  // order.
  int StartCapture(const cros_cam_capture_request_t* request,
                   cros_cam_capture_cb_t callback,
                   void* context);

  // Stops capturing immediately. Blocks until the camera device is closed.
  // We recommend that all calls to StartCapture and StopCapture be
  // sequenced, but in case they're not, the calls will be fulfilled in a FIFO
  // order.
  int StopCapture(int id);

  // Implementation of cros::mojom::CameraHalClient. Called by camera HAL
  // dispatcher to provide the camera module interface.
  void SetUpChannel(mojom::CameraModulePtr camera_module) override;

 private:
  struct CameraInfo {
    int facing;
    std::string name;
    std::vector<cros_cam_format_info_t> format_info;
    int32_t jpeg_max_size;
  };

  // SessionState indicates the state of a session:
  //   - |kIdle|: Session is idle.
  //     - Transitions to |kStarting| when we're starting a session.
  //   - |kStarting|: Session is starting.
  //     - Transitions to |kCapturing| when the device is opened.
  //     - Transitions to |kIdle| when an error is encountered while
  //       opening the device.
  //   - |kCapturing|: Session is started and actively calling capture
  //     callback to return capture result.
  //     - Transitions to |kStopping| when we're stopping a session.
  //   - |kStopping|: Session is stopping. Session is stopping and disallows any
  //     further capture results.
  //     - Transitions to |kIdle| after the device is closed, even when an error
  //       is encountered.
  enum class SessionState { kIdle, kStarting, kCapturing, kStopping };

  struct SessionInfo {
    int32_t camera_id;
    cros_cam_format_info_t format;
    cros_cam_capture_cb_t capture_callback;
    void* context;
  };

  // SessionRequestType indicates the type of a SessionRequest:
  //   - |kStart|: Starts a capture session.
  //   - |kStop|: Stops a capture session but allows additional capture results
  //     to be sent before the device is closed.
  enum class SessionRequestType { kStart, kStop };

  // SessionRequest specifies a request that starts or stops a capture session.
  struct SessionRequest {
    SessionRequestType type;
    SessionInfo info;
    // |result_callback| is associated with the future held by the caller who
    // initiated the session. The caller would wait on the future, and
    // |result_callback| is called when there is an error while processing the
    // request. If no error is encountered, |result_callback| moved to
    // |SessionContext| to be called later.
    IntOnceCallback result_callback;
  };

  // |SessionContext| stores all the contextual information of a session.
  struct SessionContext {
    SessionState state;
    SessionInfo info;
    // |result_callback| is moved from the SessionRequest that initiated the
    // context. |result_callback| is called when we finish starting or stopping
    // the session.
    IntOnceCallback result_callback;
    CameraClientOps client_ops;
  };

  // Registers the client at camera HAL dispatcher.
  void RegisterClient(RegisterClientCallback register_client_callback);

  // Closes the message pipe associated with this client.
  void CloseOnThread();

  void GetNumberOfCameras();

  void OnGotNumberOfCameras(int32_t num_builtin_cameras);

  void GetCameraInfo(int32_t camera_id);

  void OnGotCameraInfo(int32_t result, mojom::CameraInfoPtr info);

  void SendCameraInfo();

  void SendCameraInfoInternal(const cros_cam_info_t& cam_info, int is_removed);

  // PushSessionRequest is called by anyone initiating a SessionRequest from
  // a different thread.
  void PushSessionRequest(SessionRequest request);

  // Pushes |request| into |pending_session_requests_| and calls
  // TryProcessSessionRequests().
  void PushSessionRequestOnThread(SessionRequest request);

  // Tries to process session requests in |pending_session_requests_| in a FIFO
  // order. Note that we may process multiple session requests at once due to
  // same-state transitions. (e.g., consecutive stops).
  void TryProcessSessionRequests();

  // Tries to process the specified session request, |request|. Returns 0 if
  // it's processed.
  int ProcessSessionRequest(SessionRequest* request);

  // Clear out |pending_session_requests_|. Can be called to launch an immediate
  // SessionRequest.
  // TODO(lnishan): Take camera id as input for multi-device streaming.
  void FlushInflightSessionRequests(int error);

  int StartCaptureOnThread(SessionRequest* request);

  void OnOpenedDevice(int32_t result);

  int StopCaptureOnThread(SessionRequest* request);

  void OnClosedDevice(int32_t result);

  bool IsDeviceActive(int device);

  void SendCaptureResult(const cros_cam_capture_result_t& result);

  void OnStoppedCaptureFromCallback(int result);

  base::Thread ipc_thread_;
  mojom::CameraModulePtr camera_module_;
  mojo::Binding<mojom::CameraHalClient> camera_hal_client_;

  IntOnceCallback init_callback_;

  cros_cam_get_cam_info_cb_t cam_info_callback_;
  void* cam_info_context_;
  std::list<int32_t> camera_id_list_;
  std::list<int32_t>::iterator camera_id_iter_;
  std::map<int32_t, CameraInfo> camera_info_map_;
  // Lock that protects |cam_info_callback_|, |cam_info_context_|,
  // |camera_id_list_| and |camera_info_map_|.
  base::Lock camera_info_lock_;

  // TODO(lnishan): Support multi-device streaming by using a mapping from
  // camera ID to SessionContext.
  SessionContext context_;
  std::queue<SessionRequest> pending_session_requests_;
};

}  // namespace cros

#endif  // CAMERA_COMMON_LIBCAMERA_CONNECTOR_CAMERA_CLIENT_H_
