// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera3_test/camera3_module_connector.h"

#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <base/no_destructor.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <gtest/gtest.h>
#include <system/camera_metadata_hidden.h>

#include "camera3_test/camera3_device_connector.h"
#include "cros-camera/constants.h"
#include "cros-camera/ipc_util.h"

namespace camera3_test {

HalModuleConnector::HalModuleConnector(camera_module_t* cam_module,
                                       cros::CameraThread* hal_thread)
    : cam_module_(cam_module), hal_thread_(hal_thread) {
  hal_thread_->PostTaskSync(
      FROM_HERE, base::Bind(&HalModuleConnector::GetVendorTagsOnHalThread,
                            base::Unretained(this)));
}

void HalModuleConnector::GetVendorTagsOnHalThread() {
  vendor_tag_ops_t ops;
  if (cam_module_->get_vendor_tag_ops != nullptr) {
    cam_module_->get_vendor_tag_ops(&ops);
    int count = ops.get_tag_count(&ops);
    if (count > 0) {
      std::vector<uint32_t> tag_array(count, 0);
      ops.get_all_tags(&ops, tag_array.data());
      for (const auto& tag : tag_array) {
        vendor_tag_map_.emplace(std::make_pair(
            tag, VendorTagInfo{.section_name = ops.get_section_name(&ops, tag),
                               .tag_name = ops.get_tag_name(&ops, tag),
                               .type = ops.get_tag_type(&ops, tag)}));
      }
    }
  }
}

int HalModuleConnector::GetNumberOfCameras() {
  if (!cam_module_) {
    return -ENODEV;
  }
  int result = -EINVAL;
  hal_thread_->PostTaskSync(
      FROM_HERE, base::Bind(&HalModuleConnector::GetNumberOfCamerasOnHalThread,
                            base::Unretained(this), &result));
  return result;
}

void HalModuleConnector::GetNumberOfCamerasOnHalThread(int* result) {
  *result = cam_module_->get_number_of_cameras();
}

std::unique_ptr<DeviceConnector> HalModuleConnector::OpenDevice(int cam_id) {
  if (!cam_module_) {
    return nullptr;
  }
  std::unique_ptr<DeviceConnector> dev_connector;
  hal_thread_->PostTaskSync(
      FROM_HERE, base::Bind(&HalModuleConnector::OpenDeviceOnHalThread,
                            base::Unretained(this), cam_id, &dev_connector));
  return dev_connector;
}

void HalModuleConnector::OpenDeviceOnHalThread(
    int cam_id, std::unique_ptr<DeviceConnector>* dev_connector) {
  hw_device_t* device = nullptr;
  char cam_id_name[3];
  snprintf(cam_id_name, sizeof(cam_id_name), "%d", cam_id);
  if (cam_module_->common.methods->open(&cam_module_->common, cam_id_name,
                                        &device) == 0) {
    *dev_connector = std::make_unique<HalDeviceConnector>(
        cam_id, reinterpret_cast<camera3_device_t*>(device));
  }
}

int HalModuleConnector::GetCameraInfo(int cam_id, camera_info* info) {
  if (!cam_module_) {
    return -ENODEV;
  }
  int result = -ENODEV;
  hal_thread_->PostTaskSync(
      FROM_HERE, base::Bind(&HalModuleConnector::GetCameraInfoOnHalThread,
                            base::Unretained(this), cam_id, info, &result));
  return result;
}

void HalModuleConnector::GetCameraInfoOnHalThread(int cam_id,
                                                  camera_info* info,
                                                  int* result) {
  *result = cam_module_->get_camera_info(cam_id, info);
}

bool HalModuleConnector::GetVendorTagByName(const std::string name,
                                            uint32_t* tag) {
  if (!tag) {
    return false;
  }
  auto it = std::find_if(vendor_tag_map_.begin(), vendor_tag_map_.end(),
                         [&](const std::pair<uint32_t, VendorTagInfo>& v) {
                           return v.second.tag_name == name;
                         });
  if (it != vendor_tag_map_.end()) {
    *tag = it->first;
  }
  return it != vendor_tag_map_.end();
}

ClientModuleConnector::ClientModuleConnector(CameraHalClient* cam_client)
    : cam_client_(cam_client) {}

int ClientModuleConnector::GetNumberOfCameras() {
  if (!cam_client_) {
    return -ENODEV;
  }
  return cam_client_->GetNumberOfCameras();
}

std::unique_ptr<DeviceConnector> ClientModuleConnector::OpenDevice(int cam_id) {
  cros::mojom::Camera3DeviceOpsPtr dev_ops = cam_client_->OpenDevice(cam_id);
  return std::make_unique<ClientDeviceConnector>(dev_ops.PassInterface());
}

int ClientModuleConnector::GetCameraInfo(int cam_id, camera_info* info) {
  if (!cam_client_) {
    return -ENODEV;
  }
  return cam_client_->GetCameraInfo(cam_id, info);
}

bool ClientModuleConnector::GetVendorTagByName(const std::string name,
                                               uint32_t* tag) {
  return cam_client_->GetVendorTagByName(name, tag);
}

// static
CameraHalClient* CameraHalClient::GetInstance() {
  static base::NoDestructor<CameraHalClient> c;
  return c.get();
}

CameraHalClient::CameraHalClient()
    : ipc_thread_("CameraHALClientIPCThread"),
      camera_hal_client_(this),
      mojo_module_callbacks_(this),
      ipc_initialized_(base::WaitableEvent::ResetPolicy::MANUAL,
                       base::WaitableEvent::InitialState::NOT_SIGNALED),
      vendor_tag_count_(0) {}

int CameraHalClient::Start(camera_module_callbacks_t* callbacks) {
  static constexpr ::base::TimeDelta kIpcTimeout =
      ::base::TimeDelta::FromSeconds(3);

  VLOGF_ENTER();
  if (!callbacks) {
    return -EINVAL;
  }
  camera_module_callbacks_ = callbacks;
  mojo::core::Init();
  if (!ipc_thread_.StartWithOptions(
          base::Thread::Options(base::MessagePumpType::IO, 0))) {
    LOGF(ERROR) << "Failed to start thread";
    return -EIO;
  }
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      ipc_thread_.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);

  mojo::ScopedMessagePipeHandle child_pipe;
  base::FilePath socket_path(cros::constants::kCrosCameraSocketPathString);
  if (cros::CreateMojoChannelToParentByUnixDomainSocket(
          socket_path, &child_pipe) != MOJO_RESULT_OK) {
    LOGF(ERROR) << "Failed to create mojo channel";
    return -EIO;
  }

  dispatcher_ = mojo::MakeProxy(
      cros::mojom::CameraHalDispatcherPtrInfo(std::move(child_pipe), 0u),
      ipc_thread_.task_runner());
  if (!dispatcher_.is_bound()) {
    LOGF(ERROR) << "Failed to bind mojo dispatcher";
    return -EIO;
  }

  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraHalClient::RegisterClient, base::Unretained(this)));

  if (!ipc_initialized_.TimedWait(kIpcTimeout)) {
    LOGF(ERROR) << "Failed to set up channel and get vendor tags";
    return -EIO;
  }

  return 0;
}

void CameraHalClient::RegisterClient() {
  VLOGF_ENTER();
  ASSERT_TRUE(ipc_thread_.task_runner()->BelongsToCurrentThread());
  cros::mojom::CameraHalClientPtr client_ptr;
  camera_hal_client_.Bind(mojo::MakeRequest(&client_ptr));
  dispatcher_->RegisterClient(std::move(client_ptr));
}

void CameraHalClient::SetUpChannel(cros::mojom::CameraModulePtr camera_module) {
  VLOGF_ENTER();
  ASSERT_TRUE(ipc_thread_.task_runner()->BelongsToCurrentThread());
  camera_module_ = std::move(camera_module);
  camera_module_.set_connection_error_handler(base::Bind(
      &CameraHalClient::onIpcConnectionLost, base::Unretained(this)));

  cros::mojom::CameraModuleCallbacksPtr camera_module_callbacks_ptr;
  cros::mojom::CameraModuleCallbacksRequest camera_module_callbacks_request =
      mojo::MakeRequest(&camera_module_callbacks_ptr);
  mojo_module_callbacks_.Bind(std::move(camera_module_callbacks_request));
  camera_module_->SetCallbacks(
      std::move(camera_module_callbacks_ptr),
      base::Bind(&CameraHalClient::OnSetCallbacks, base::Unretained(this)));
}

void CameraHalClient::OnSetCallbacks(int32_t result) {
  VLOGF_ENTER();
  ASSERT_TRUE(ipc_thread_.task_runner()->BelongsToCurrentThread());
  if (result != 0) {
    LOGF(ERROR) << "Failed to set callbacks";
    exit(EXIT_FAILURE);
  }

  cros::mojom::VendorTagOpsRequest ops_req =
      mojo::MakeRequest(&vendor_tag_ops_);
  camera_module_->GetVendorTagOps(
      std::move(ops_req),
      base::Bind(&CameraHalClient::OnGotVendorTagOps, base::Unretained(this)));
}

void CameraHalClient::OnGotVendorTagOps() {
  VLOGF_ENTER();
  vendor_tag_ops_->GetAllTags(
      base::Bind(&CameraHalClient::OnGotAllTags, base::Unretained(this)));
}

void CameraHalClient::OnGotAllTags(const std::vector<uint32_t>& tag_array) {
  VLOGF_ENTER();
  if (tag_array.empty()) {
    ipc_initialized_.Signal();
    return;
  }
  vendor_tag_count_ = tag_array.size();
  for (const auto& tag : tag_array) {
    vendor_tag_ops_->GetSectionName(
        tag, base::Bind(&CameraHalClient::OnGotSectionName,
                        base::Unretained(this), tag));
  }
}

void CameraHalClient::OnGotSectionName(
    uint32_t tag, const base::Optional<std::string>& name) {
  VLOGF_ENTER();
  ASSERT_NE(base::nullopt, name);
  vendor_tag_map_[tag].section_name = *name;

  vendor_tag_ops_->GetTagName(tag, base::Bind(&CameraHalClient::OnGotTagName,
                                              base::Unretained(this), tag));
}

void CameraHalClient::OnGotTagName(uint32_t tag,
                                   const base::Optional<std::string>& name) {
  VLOGF_ENTER();
  ASSERT_NE(base::nullopt, name);
  vendor_tag_map_[tag].tag_name = *name;

  vendor_tag_ops_->GetTagType(tag, base::Bind(&CameraHalClient::OnGotTagType,
                                              base::Unretained(this), tag));
}

void CameraHalClient::OnGotTagType(uint32_t tag, int32_t type) {
  VLOGF_ENTER();
  vendor_tag_map_[tag].type = type;

  if ((--vendor_tag_count_) == 0) {
    for (const auto& it : vendor_tag_map_) {
      ASSERT_TRUE(vendor_tag_manager_.Add(it.first, it.second.section_name,
                                          it.second.tag_name, it.second.type));
    }
    vendor_tag_map_.clear();
    if (set_camera_metadata_vendor_ops(&vendor_tag_manager_) != 0) {
      ADD_FAILURE() << "Failed to set vendor ops to camera metadata";
    }

    ipc_initialized_.Signal();
  }
}

int CameraHalClient::GetNumberOfCameras() {
  VLOGF_ENTER();
  auto future = cros::Future<int32_t>::Create(nullptr);
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(&CameraHalClient::GetNumberOfCamerasOnIpcThread,
                     base::Unretained(this), cros::GetFutureCallback(future)));
  if (!future->Wait()) {
    ADD_FAILURE() << "Wait timeout";
    return -ENODEV;
  }
  return future->Get();
}

void CameraHalClient::GetNumberOfCamerasOnIpcThread(
    base::Callback<void(int32_t)> cb) {
  VLOGF_ENTER();
  if (!ipc_initialized_.IsSignaled()) {
    cb.Run(-ENODEV);
    return;
  }
  camera_module_->GetNumberOfCameras(cb);
}

int CameraHalClient::GetCameraInfo(int cam_id, camera_info* info) {
  VLOGF_ENTER();
  if (!info) {
    return -EINVAL;
  }
  auto future = cros::Future<int32_t>::Create(nullptr);
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&CameraHalClient::GetCameraInfoOnIpcThread,
                                base::Unretained(this), cam_id, info,
                                cros::GetFutureCallback(future)));
  if (!future->Wait()) {
    ADD_FAILURE() << "Wait timeout";
    return -ENODEV;
  }
  return future->Get();
}

void CameraHalClient::GetCameraInfoOnIpcThread(
    int cam_id, camera_info* info, base::Callback<void(int32_t)> cb) {
  VLOGF_ENTER();
  if (!ipc_initialized_.IsSignaled()) {
    cb.Run(-ENODEV);
    return;
  }
  camera_module_->GetCameraInfo(
      cam_id, base::Bind(&CameraHalClient::OnGotCameraInfo,
                         base::Unretained(this), cam_id, info, cb));
}

void CameraHalClient::OnGotCameraInfo(int cam_id,
                                      camera_info* info,
                                      base::Callback<void(int32_t)> cb,
                                      int32_t result,
                                      cros::mojom::CameraInfoPtr info_ptr) {
  VLOGF_ENTER();
  if (result == 0) {
    memset(info, 0, sizeof(*info));
    info->facing = static_cast<int>(info_ptr->facing);
    info->orientation = info_ptr->orientation;
    info->device_version = info_ptr->device_version;
    if (!base::Contains(static_characteristics_map_, cam_id)) {
      static_characteristics_map_[cam_id] =
          cros::internal::DeserializeCameraMetadata(
              info_ptr->static_camera_characteristics);
    }
    info->static_camera_characteristics =
        static_characteristics_map_[cam_id].get();
    info->resource_cost = info_ptr->resource_cost->resource_cost;
    if (!base::Contains(conflicting_devices_map_, cam_id)) {
      for (const auto& it : *info_ptr->conflicting_devices) {
        conflicting_devices_char_map_[cam_id].emplace_back(it.begin(),
                                                           it.end());
        conflicting_devices_char_map_[cam_id].back().push_back('\0');
        conflicting_devices_map_[cam_id].push_back(
            conflicting_devices_char_map_[cam_id].back().data());
      }
    }
    info->conflicting_devices_length = conflicting_devices_map_[cam_id].size();
    info->conflicting_devices = conflicting_devices_map_[cam_id].data();
  }
  cb.Run(result);
}

cros::mojom::Camera3DeviceOpsPtr CameraHalClient::OpenDevice(int cam_id) {
  VLOGF_ENTER();
  cros::mojom::Camera3DeviceOpsPtr dev_ops;
  auto future = cros::Future<int32_t>::Create(nullptr);
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&CameraHalClient::OpenDeviceOnIpcThread,
                                base::Unretained(this), cam_id, &dev_ops,
                                cros::GetFutureCallback(future)));
  if (!future->Wait()) {
    ADD_FAILURE() << __func__ << " timeout";
    return nullptr;
  }
  return dev_ops;
}

void CameraHalClient::OpenDeviceOnIpcThread(
    int cam_id,
    cros::mojom::Camera3DeviceOpsPtr* dev_ops,
    base::Callback<void(int32_t)> cb) {
  VLOGF_ENTER();
  if (!ipc_initialized_.IsSignaled()) {
    cb.Run(-ENODEV);
    return;
  }
  cros::mojom::Camera3DeviceOpsRequest device_ops_request =
      mojo::MakeRequest(dev_ops);
  camera_module_->OpenDevice(cam_id, std::move(device_ops_request), cb);
}

bool CameraHalClient::GetVendorTagByName(const std::string name,
                                         uint32_t* tag) {
  if (!tag) {
    return false;
  }
  std::vector<uint32_t> tags(vendor_tag_manager_.GetTagCount());
  vendor_tag_manager_.GetAllTags(tags.data());
  for (const auto& t : tags) {
    if (name.compare(vendor_tag_manager_.GetTagName(t)) == 0) {
      *tag = t;
      return true;
    }
  }
  return false;
}

void CameraHalClient::CameraDeviceStatusChange(
    int32_t camera_id, cros::mojom::CameraDeviceStatus new_status) {
  VLOGF_ENTER();
  ASSERT_TRUE(ipc_thread_.task_runner()->BelongsToCurrentThread());
  camera_module_callbacks_->camera_device_status_change(
      camera_module_callbacks_, camera_id,
      static_cast<camera_device_status_t>(new_status));
}

void CameraHalClient::TorchModeStatusChange(
    int32_t camera_id, cros::mojom::TorchModeStatus new_status) {
  VLOGF_ENTER();
  ASSERT_TRUE(ipc_thread_.task_runner()->BelongsToCurrentThread());
  std::stringstream ss;
  ss << camera_id;
  camera_module_callbacks_->torch_mode_status_change(
      camera_module_callbacks_, ss.str().c_str(),
      static_cast<camera_device_status_t>(new_status));
}

void CameraHalClient::onIpcConnectionLost() {
  VLOGF_ENTER();
  ipc_initialized_.Reset();
  static_characteristics_map_.clear();
  vendor_tag_map_.clear();
  conflicting_devices_char_map_.clear();
  conflicting_devices_map_.clear();
}

}  // namespace camera3_test
