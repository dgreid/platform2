/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_ADAPTER_CAMERA_HAL_ADAPTER_H_
#define CAMERA_HAL_ADAPTER_CAMERA_HAL_ADAPTER_H_

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <hardware/camera3.h>

#include <base/single_thread_task_runner.h>
#include <base/threading/thread.h>
#include <base/timer/elapsed_timer.h>

#include "common/vendor_tag_manager.h"
#include "cros-camera/camera_metrics.h"
#include "cros-camera/future.h"
#include "hal_adapter/reprocess_effect/reprocess_effect_manager.h"
#include "mojo/camera3.mojom.h"
#include "mojo/camera_common.mojom.h"

namespace cros {

class CameraDeviceAdapter;

class CameraModuleDelegate;

class CameraModuleCallbacksDelegate;

class VendorTagOpsDelegate;

class CameraHalAdapter;

struct CameraModuleCallbacksAux : camera_module_callbacks_t {
  int module_id;
  CameraHalAdapter* adapter;
};

class CameraHalAdapter {
 public:
  explicit CameraHalAdapter(std::vector<camera_module_t*> camera_modules);

  virtual ~CameraHalAdapter();

  // Starts the camera HAL adapter.  This method must be called before calling
  // any other methods.
  bool Start();

  // Creates the CameraModule Mojo connection from |camera_module_request|.
  void OpenCameraHal(mojom::CameraModuleRequest camera_module_request);

  // Callback interface for CameraModuleDelegate.
  // These methods are callbacks for |module_delegate_| and are executed on
  // the mojo IPC handler thread in |module_delegate_|.
  virtual int32_t OpenDevice(int32_t camera_id,
                             mojom::Camera3DeviceOpsRequest device_ops_request);

  virtual int32_t GetNumberOfCameras();

  virtual int32_t GetCameraInfo(int32_t camera_id,
                                mojom::CameraInfoPtr* camera_info);

  int32_t SetCallbacks(mojom::CameraModuleCallbacksPtr callbacks);

  virtual int32_t SetTorchMode(int32_t camera_id, bool enabled);

  int32_t Init();

  void GetVendorTagOps(mojom::VendorTagOpsRequest vendor_tag_ops_request);

  // A callback for the camera devices opened in OpenDevice().  Used to run
  // CloseDevice() on the same thread that OpenDevice() runs on.
  void CloseDeviceCallback(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      int32_t camera_id);

 protected:
  // Convert the unified external |camera_id| into the corresponding camera
  // module and its internal id. Returns (nullptr, 0) if not found.
  std::pair<camera_module_t*, int> GetInternalModuleAndId(int camera_id);

  // Initialize all underlying camera HALs on |camera_module_thread_| and
  // build the mapping table for camera id.
  virtual void StartOnThread(base::Callback<void(bool)> callback);

  virtual void NotifyCameraDeviceStatusChange(
      CameraModuleCallbacksDelegate* delegate,
      int camera_id,
      camera_device_status_t status);

  virtual void NotifyTorchModeStatusChange(
      CameraModuleCallbacksDelegate* delegate,
      int camera_id,
      torch_mode_status_t status);

 private:
  // The static methods implement camera_module_callbacks_t, which will delegate
  // to the corresponding instance methods.
  static void camera_device_status_change(
      const camera_module_callbacks_t* callbacks,
      int camera_id,
      int new_status);

  static void torch_mode_status_change(
      const camera_module_callbacks_t* callbacks,
      const char* camera_id,
      int new_status);

  void CameraDeviceStatusChange(const CameraModuleCallbacksAux* callbacks,
                                int camera_id,
                                camera_device_status_t new_status);

  void TorchModeStatusChange(const CameraModuleCallbacksAux* callbacks,
                             int camera_id,
                             torch_mode_status_t new_status);

  // Send the latest status to the newly connected client.
  void SendLatestStatus(int callbacks_id);

  // Convert the |module_id| and its corresponding internal |camera_id| into the
  // unified external camera id. Returns -1 if not found.
  int GetExternalId(int module_id, int camera_id);

  // Clean up the camera device specified by |camera_id| in |device_adapters_|.
  void CloseDevice(int32_t camera_id);

  void ResetModuleDelegateOnThread(uint32_t module_id);
  void ResetCallbacksDelegateOnThread(uint32_t callbacks_id);
  void ResetVendorTagOpsDelegateOnThread(uint32_t vendor_tag_ops_id);

  // The handles to the camera HALs dlopen()/dlsym()'d on process start.
  std::vector<camera_module_t*> camera_modules_;

  // The thread that all camera module functions operate on.
  base::Thread camera_module_thread_;

  // The thread that all the Mojo communication of camera module callbacks
  // operate on.
  base::Thread camera_module_callbacks_thread_;

  // The number of built-in cameras.
  int num_builtin_cameras_;

  // The next id for newly plugged external camera, which is starting from
  // |num_builtin_cameras_|.
  int next_external_camera_id_;

  // The mapping tables of internal/external |camera_id|.
  // (external camera id) <=> (module id, internal camera id)
  std::map<int, std::pair<int, int>> camera_id_map_;
  std::vector<std::map<int, int>> camera_id_inverse_map_;

  // We need to keep the status for each camera to send up-to-date information
  // for newly connected client so everyone is in sync.
  // (external camera id) <=> (latest status)
  std::map<int, camera_device_status_t> device_status_map_;
  std::map<int, camera_device_status_t> default_device_status_map_;
  std::map<int, torch_mode_status_t> torch_mode_status_map_;
  std::map<int, torch_mode_status_t> default_torch_mode_status_map_;

  // The callback structs with auxiliary metadata for converting |camera_id|
  // per camera module.
  std::vector<std::unique_ptr<CameraModuleCallbacksAux>> callbacks_auxs_;

  // The delegates that handle the CameraModule mojo IPC.  The key of the map is
  // got from |module_id_|.
  std::map<uint32_t, std::unique_ptr<CameraModuleDelegate>> module_delegates_;

  // The delegates that handle the VendorTagOps mojo IPC. The key of the map is
  // got from |vendor_tag_ops_id_|.
  std::map<uint32_t, std::unique_ptr<VendorTagOpsDelegate>>
      vendor_tag_ops_delegates_;

  // The delegate that handles the CameraModuleCallbacks mojo IPC.  The key of
  // the map is got from |callbacks_id_|.
  std::map<uint32_t, std::unique_ptr<CameraModuleCallbacksDelegate>>
      callbacks_delegates_;

  // Protects |module_delegates_|.
  base::Lock module_delegates_lock_;
  // Protects |callbacks_delegates_|.
  base::Lock callbacks_delegates_lock_;

  // Strictly increasing integers used as the key for new CameraModuleDelegate,
  // CameraModuleCallbacksDelegate and VendorTagOpsDelegate instances in
  // |module_delegates_|, |callback_delegates_| and |vendor_tag_ops_delegates_|.
  uint32_t module_id_;
  uint32_t callbacks_id_;
  uint32_t vendor_tag_ops_id_;

  // The handles to the opened camera devices.  |device_adapters_| is accessed
  // only in OpenDevice(), CloseDevice() and CameraDeviceStatusChange().  In
  // order to do lock-free access to |device_adapters_|, we run all of them on
  // the same thread (i.e. the mojo IPC handler thread in |module_delegate_|).
  std::map<int32_t, std::unique_ptr<CameraDeviceAdapter>> device_adapters_;

  // The vendor tag manager.
  VendorTagManager vendor_tag_manager_;

  // The reprocess effect manager.
  ReprocessEffectManager reprocess_effect_manager_;

  // The map of session start time.
  std::map<int, base::ElapsedTimer> session_timer_map_;

  // Metrics for camera service.
  std::unique_ptr<CameraMetrics> camera_metrics_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(CameraHalAdapter);
};

}  // namespace cros

#endif  // CAMERA_HAL_ADAPTER_CAMERA_HAL_ADAPTER_H_
