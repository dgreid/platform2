// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CAMERA_CAMERA3_TEST_CAMERA3_MODULE_CONNECTOR_H_
#define CAMERA_CAMERA3_TEST_CAMERA3_MODULE_CONNECTOR_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/logging.h>
#include <cros-camera/camera_thread.h>
#include <hardware/camera3.h>

#include "common/utils/cros_camera_mojo_utils.h"
#include "common/vendor_tag_manager.h"
#include "mojo/camera_common.mojom.h"
#include "mojo/core/embedder/scoped_ipc_support.h"
#include "mojo/cros_camera_service.mojom.h"

namespace camera3_test {

// Forward declaration
class DeviceConnector;

struct VendorTagInfo {
  std::string section_name;
  std::string tag_name;
  int type;
};

class ModuleConnector {
 public:
  virtual ~ModuleConnector() = default;

  // Get number of cameras; a negative error code is returned if failed.
  virtual int GetNumberOfCameras() = 0;

  // Get camera information.
  virtual int GetCameraInfo(int cam_id, camera_info* info) = 0;

  // Open camera device
  virtual std::unique_ptr<DeviceConnector> OpenDevice(int cam_id) = 0;

  // Get vendor tag by the tag name; False is returned if not found.
  virtual bool GetVendorTagByName(const std::string name, uint32_t* tag) = 0;
};

class HalModuleConnector final : public ModuleConnector {
 public:
  HalModuleConnector(camera_module_t* cam_module,
                     cros::CameraThread* hal_thread);

  // ModuleConnector implementations.
  int GetNumberOfCameras() override;
  int GetCameraInfo(int cam_id, camera_info* info) override;
  std::unique_ptr<DeviceConnector> OpenDevice(int cam_id) override;
  bool GetVendorTagByName(const std::string name, uint32_t* tag) override;

 private:
  void GetVendorTagsOnHalThread();

  void GetNumberOfCamerasOnHalThread(int* result);
  void GetCameraInfoOnHalThread(int cam_id, camera_info* info, int* result);
  void OpenDeviceOnHalThread(int cam_id,
                             std::unique_ptr<DeviceConnector>* dev_connector);

  const camera_module_t* cam_module_;

  // This thread is needed because of the Chrome OS camera HAL adapter
  // assumption that all the camera_module functions should be called on the
  // same Chromium thread. It is expected to start this thread before gtest
  // initialization in main() because test case instantiation needs it running
  // to get the camera ID list.
  cros::CameraThread* hal_thread_;

  // Map of vendor tag information with tag value as the key
  std::map<uint32_t, VendorTagInfo> vendor_tag_map_;

  HalModuleConnector(const HalModuleConnector&) = delete;
  HalModuleConnector& operator=(const HalModuleConnector&) = delete;
};

// Forward declaration
class CameraHalClient;

class ClientModuleConnector final : public ModuleConnector {
 public:
  explicit ClientModuleConnector(CameraHalClient* cam_client);

  // ModuleConnector implementations.
  int GetNumberOfCameras() override;
  int GetCameraInfo(int cam_id, camera_info* info) override;
  std::unique_ptr<DeviceConnector> OpenDevice(int cam_id) override;
  bool GetVendorTagByName(const std::string name, uint32_t* tag) override;

 private:
  CameraHalClient* cam_client_;

  ClientModuleConnector(const ClientModuleConnector&) = delete;
  ClientModuleConnector& operator=(const ClientModuleConnector&) = delete;
};

class CameraHalClient final : public cros::mojom::CameraHalClient,
                              public cros::mojom::CameraModuleCallbacks {
 public:
  static CameraHalClient* GetInstance();

  CameraHalClient();

  // Establish the IPC connection to the camera service.
  int Start(camera_module_callbacks_t* callbacks);

  // Get number of cameras.
  int GetNumberOfCameras();

  // Get camera information.
  int GetCameraInfo(int cam_id, camera_info* info);

  // Open camera device
  void OpenDevice(int cam_id, cros::mojom::Camera3DeviceOpsRequest dev_ops_req);

  // Get vendor tag by the tag name; False is returned if not found.
  bool GetVendorTagByName(const std::string name, uint32_t* tag);

 private:
  // Establishes a connection to dispatcher and registers to CameraHalDispatcher
  // to acquire camera HAL handle.
  void ConnectToDispatcher(base::Callback<void(int)> callback);

  // Implementation of cros::mojom::CameraHalClient.
  void SetUpChannel(cros::mojom::CameraModulePtr camera_module) override;

  // Callback for SetCallbacks Mojo IPC function.
  void OnSetCallbacks(int32_t result);

  void OnGotVendorTagOps();
  void OnGotAllTags(const std::vector<uint32_t>& tag_array);
  void OnGotSectionName(uint32_t tag, const base::Optional<std::string>& name);
  void OnGotTagName(uint32_t tag, const base::Optional<std::string>& name);
  void OnGotTagType(uint32_t tag, int32_t type);

  void GetNumberOfCamerasOnIpcThread(base::Callback<void(int32_t)> cb);
  void GetCameraInfoOnIpcThread(int cam_id,
                                camera_info* info,
                                base::Callback<void(int32_t)> cb);
  void OnGotCameraInfo(int cam_id,
                       camera_info* info,
                       base::Callback<void(int32_t)> cb,
                       int32_t result,
                       cros::mojom::CameraInfoPtr camera_info);

  void OnDeviceOpsRequestReceived(
      cros::mojom::Camera3DeviceOpsRequest dev_ops_req);
  void OpenDeviceOnIpcThread(int cam_id,
                             cros::mojom::Camera3DeviceOpsRequest dev_ops_req,
                             base::Callback<void(int32_t)> cb);
  void CameraDeviceStatusChange(
      int32_t camera_id, cros::mojom::CameraDeviceStatus new_status) override;
  void TorchModeStatusChange(int32_t camera_id,
                             cros::mojom::TorchModeStatus new_status) override;
  void onIpcConnectionLost();

  base::Thread ipc_thread_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  mojo::Binding<cros::mojom::CameraHalClient> camera_hal_client_;
  mojo::Binding<cros::mojom::CameraModuleCallbacks> mojo_module_callbacks_;
  camera_module_callbacks_t* camera_module_callbacks_;
  cros::mojom::CameraHalDispatcherPtr dispatcher_;

  // Signifies when IPC is connected and vendor tags acquired.
  base::WaitableEvent ipc_initialized_;

  std::atomic<size_t> vendor_tag_count_;

  // Map of vendor tag information with tag value as the key.
  std::map<uint32_t, VendorTagInfo> vendor_tag_map_;

  // The vendor tag manager.
  cros::VendorTagManager vendor_tag_manager_;

  // Map of static characteristics with camera id as the key.
  std::map<int, cros::internal::ScopedCameraMetadata>
      static_characteristics_map_;

  // Map of conflicting devices with camera id as the key.
  std::map<int, std::vector<std::vector<char>>> conflicting_devices_char_map_;
  std::map<int, std::vector<char*>> conflicting_devices_map_;

  cros::mojom::VendorTagOpsPtr vendor_tag_ops_;
  cros::mojom::CameraModulePtr camera_module_;

  CameraHalClient(const CameraHalClient&) = delete;
  CameraHalClient& operator=(const CameraHalClient&) = delete;
};

}  // namespace camera3_test

#endif  // CAMERA_CAMERA3_TEST_CAMERA3_MODULE_CONNECTOR_H_
