/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hal_adapter/camera_hal_adapter.h"

#include <algorithm>
#include <iomanip>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/logging.h>
#include <base/threading/thread_task_runner_handle.h>
#include <camera/camera_metadata.h>
#include <system/camera_metadata_hidden.h>

#include "common/utils/cros_camera_mojo_utils.h"
#include "cros-camera/camera_metrics.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"
#include "hal_adapter/camera_device_adapter.h"
#include "hal_adapter/camera_module_callbacks_associated_delegate.h"
#include "hal_adapter/camera_module_callbacks_delegate.h"
#include "hal_adapter/camera_module_delegate.h"
#include "hal_adapter/camera_trace_event.h"
#include "hal_adapter/vendor_tag_ops_delegate.h"

namespace cros {

namespace {

// A special id used in ResetModuleDelegateOnThread and
// ResetCallbacksDelegateOnThread to specify all the entries present in the
// |module_delegates_| and |callbacks_delegates_| maps.
const uint32_t kIdAll = 0xFFFFFFFF;

}  // namespace

CameraHalAdapter::CameraHalAdapter(std::vector<camera_module_t*> camera_modules,
                                   CameraMojoChannelManagerToken* token,
                                   CameraActivityCallback activity_callback)
    : camera_modules_(camera_modules),
      camera_module_thread_("CameraModuleThread"),
      camera_module_callbacks_thread_("CameraModuleCallbacksThread"),
      module_id_(0),
      callbacks_id_(0),
      vendor_tag_ops_id_(0),
      camera_metrics_(CameraMetrics::New()),
      mojo_manager_token_(token),
      activity_callback_(activity_callback) {
  VLOGF_ENTER();
}

CameraHalAdapter::~CameraHalAdapter() {
  VLOGF_ENTER();
  camera_module_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&CameraHalAdapter::ResetModuleDelegateOnThread,
                            base::Unretained(this), kIdAll));
  camera_module_callbacks_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&CameraHalAdapter::ResetCallbacksDelegateOnThread,
                            base::Unretained(this), kIdAll));
  camera_module_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraHalAdapter::ResetVendorTagOpsDelegateOnThread,
                 base::Unretained(this), kIdAll));
  camera_module_thread_.Stop();
  camera_module_callbacks_thread_.Stop();
  set_camera_metadata_vendor_ops(nullptr);
}

bool CameraHalAdapter::Start() {
  VLOGF_ENTER();
  TRACE_CAMERA_INSTANT();

  if (!camera_module_thread_.Start()) {
    LOGF(ERROR) << "Failed to start CameraModuleThread";
    return false;
  }
  if (!camera_module_callbacks_thread_.Start()) {
    LOGF(ERROR) << "Failed to start CameraCallbacksThread";
    return false;
  }

  auto future = cros::Future<bool>::Create(nullptr);
  camera_module_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraHalAdapter::StartOnThread, base::Unretained(this),
                 cros::GetFutureCallback(future)));
  return future->Get();
}

void CameraHalAdapter::OpenCameraHal(
    mojom::CameraModuleRequest camera_module_request,
    mojom::CameraClientType camera_client_type) {
  VLOGF_ENTER();
  TRACE_CAMERA_SCOPED();
  auto module_delegate = std::make_unique<CameraModuleDelegate>(
      this, camera_module_thread_.task_runner(), camera_client_type);
  uint32_t module_id = module_id_++;
  module_delegate->Bind(
      camera_module_request.PassMessagePipe(),
      base::Bind(&CameraHalAdapter::ResetModuleDelegateOnThread,
                 base::Unretained(this), module_id));
  base::AutoLock l(module_delegates_lock_);
  module_delegates_[module_id] = std::move(module_delegate);
  VLOGF(1) << "CameraModule " << module_id << " connected";
}

// Callback interface for camera_module_t APIs.

int32_t CameraHalAdapter::OpenDevice(
    int32_t camera_id,
    mojom::Camera3DeviceOpsRequest device_ops_request,
    mojom::CameraClientType camera_client_type) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED("camera_id", camera_id);

  session_timer_map_.emplace(std::piecewise_construct,
                             std::forward_as_tuple(camera_id),
                             std::forward_as_tuple());

  camera_module_t* camera_module;
  int internal_camera_id;
  std::tie(camera_module, internal_camera_id) =
      GetInternalModuleAndId(camera_id);

  LOGF(INFO) << camera_client_type << ", camera_id = " << camera_id
             << ", camera_module = " << camera_module->common.name
             << ", internal_camera_id = " << internal_camera_id;

  if (!camera_module) {
    return -EINVAL;
  }

  if (device_adapters_.find(camera_id) != device_adapters_.end()) {
    LOGF(WARNING) << "Multiple calls to OpenDevice on device " << camera_id;
    return -EBUSY;
  }

  hw_module_t* common = &camera_module->common;
  camera3_device_t* camera_device;
  int ret =
      common->methods->open(common, std::to_string(internal_camera_id).c_str(),
                            reinterpret_cast<hw_device_t**>(&camera_device));
  if (ret != 0) {
    LOGF(ERROR) << "Failed to open camera device " << camera_id;
    return ret;
  }
  activity_callback_.Run(camera_id, /*opened=*/true, camera_client_type);

  camera_info_t info;
  ret = camera_module->get_camera_info(internal_camera_id, &info);
  if (ret != 0) {
    LOGF(ERROR) << "Failed to get camera info of camera " << camera_id;
    return ret;
  }
  // This method is called by |camera_module_delegate_| on its mojo IPC
  // handler thread.
  // The CameraHalAdapter (and hence |camera_module_delegate_|) must out-live
  // the CameraDeviceAdapters, so it's safe to keep a reference to the task
  // runner of the current thread in the callback functor.
  base::Callback<void()> close_callback = base::Bind(
      &CameraHalAdapter::CloseDeviceCallback, base::Unretained(this),
      base::ThreadTaskRunnerHandle::Get(), camera_id, camera_client_type);
  device_adapters_[camera_id] = std::make_unique<CameraDeviceAdapter>(
      camera_device, info.static_camera_characteristics, close_callback);

  CameraDeviceAdapter::HasReprocessEffectVendorTagCallback
      has_reprocess_effect_vendor_tag_callback =
          base::Bind(&ReprocessEffectManager::HasReprocessEffectVendorTag,
                     base::Unretained(&reprocess_effect_manager_));
  CameraDeviceAdapter::ReprocessEffectCallback reprocess_effect_callback =
      base::Bind(&ReprocessEffectManager::ReprocessRequest,
                 base::Unretained(&reprocess_effect_manager_));
  if (!device_adapters_[camera_id]->Start(
          std::move(has_reprocess_effect_vendor_tag_callback),
          std::move(reprocess_effect_callback))) {
    device_adapters_.erase(camera_id);
    return -ENODEV;
  }
  device_adapters_.at(camera_id)->Bind(std::move(device_ops_request));
  camera_metrics_->SendCameraFacing(info.facing);
  camera_metrics_->SendOpenDeviceLatency(
      session_timer_map_[camera_id].Elapsed());

  return 0;
}

int32_t CameraHalAdapter::GetNumberOfCameras() {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();
  return num_builtin_cameras_;
}

int32_t CameraHalAdapter::GetCameraInfo(int32_t camera_id,
                                        mojom::CameraInfoPtr* camera_info) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED("camera_id", camera_id);

  camera_module_t* camera_module;
  int internal_camera_id;
  std::tie(camera_module, internal_camera_id) =
      GetInternalModuleAndId(camera_id);

  if (!camera_module) {
    camera_info->reset();
    return -EINVAL;
  }

  camera_info_t info;
  int ret = camera_module->get_camera_info(internal_camera_id, &info);
  if (ret != 0) {
    LOGF(ERROR) << "Failed to get info of camera " << camera_id;
    camera_info->reset();
    return ret;
  }

  LOGF(INFO) << "camera_id = " << camera_id << ", facing = " << info.facing;

  if (VLOG_IS_ON(2)) {
    dump_camera_metadata(info.static_camera_characteristics, 2, 3);
  }

  android::CameraMetadata metadata =
      clone_camera_metadata(info.static_camera_characteristics);
  reprocess_effect_manager_.UpdateStaticMetadata(&metadata);

  mojom::CameraInfoPtr info_ptr = mojom::CameraInfo::New();
  info_ptr->facing = static_cast<mojom::CameraFacing>(info.facing);
  info_ptr->orientation = info.orientation;
  info_ptr->device_version = info.device_version;
  info_ptr->static_camera_characteristics =
      internal::SerializeCameraMetadata(metadata.getAndLock());
  info_ptr->resource_cost = mojom::CameraResourceCost::New();
  info_ptr->resource_cost->resource_cost = info.resource_cost;

  std::vector<std::string> conflicting_devices;
  int module_id = camera_id_map_[camera_id].first;
  for (size_t i = 0; i < info.conflicting_devices_length; i++) {
    int conflicting_id =
        GetExternalId(module_id, atoi(info.conflicting_devices[i]));
    conflicting_devices.push_back(std::to_string(conflicting_id));
  }
  info_ptr->conflicting_devices = std::move(conflicting_devices);

  *camera_info = std::move(info_ptr);
  return 0;
}

int32_t CameraHalAdapter::SetCallbacks(
    mojom::CameraModuleCallbacksPtr callbacks) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();

  auto callbacks_delegate = std::make_unique<CameraModuleCallbacksDelegate>(
      camera_module_callbacks_thread_.task_runner());
  uint32_t callbacks_id = callbacks_id_++;
  callbacks_delegate->Bind(
      callbacks.PassInterface(),
      base::Bind(&CameraHalAdapter::ResetCallbacksDelegateOnThread,
                 base::Unretained(this), callbacks_id));

  // Send latest status to the new client, so all presented external cameras are
  // available to the client after SetCallbacks() returns.
  for (const auto& it : device_status_map_) {
    int camera_id = it.first;
    camera_device_status_t device_status = it.second;
    if (device_status != default_device_status_map_[camera_id]) {
      NotifyCameraDeviceStatusChange(callbacks_delegate.get(), camera_id,
                                     device_status);
    }
    torch_mode_status_t torch_status = torch_mode_status_map_[camera_id];
    if (torch_status != default_torch_mode_status_map_[camera_id]) {
      NotifyTorchModeStatusChange(callbacks_delegate.get(), camera_id,
                                  torch_status);
    }
  }

  base::AutoLock l(callbacks_delegates_lock_);
  callbacks_delegates_[callbacks_id] = std::move(callbacks_delegate);

  return 0;
}

int32_t CameraHalAdapter::SetTorchMode(int32_t camera_id, bool enabled) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();

  camera_module_t* camera_module;
  int internal_camera_id;
  std::tie(camera_module, internal_camera_id) =
      GetInternalModuleAndId(camera_id);

  if (!camera_module) {
    return -EINVAL;
  }

  if (auto fn = camera_module->set_torch_mode) {
    return fn(std::to_string(internal_camera_id).c_str(), enabled);
  }

  return -ENOSYS;
}

int32_t CameraHalAdapter::Init() {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();
  return 0;
}

void CameraHalAdapter::GetVendorTagOps(
    mojom::VendorTagOpsRequest vendor_tag_ops_request) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());

  auto vendor_tag_ops_delegate = std::make_unique<VendorTagOpsDelegate>(
      camera_module_thread_.task_runner(), &vendor_tag_manager_);
  uint32_t vendor_tag_ops_id = vendor_tag_ops_id_++;
  vendor_tag_ops_delegate->Bind(
      vendor_tag_ops_request.PassMessagePipe(),
      base::Bind(&CameraHalAdapter::ResetVendorTagOpsDelegateOnThread,
                 base::Unretained(this), vendor_tag_ops_id));
  vendor_tag_ops_delegates_[vendor_tag_ops_id] =
      std::move(vendor_tag_ops_delegate);
  VLOGF(1) << "VendorTagOps " << vendor_tag_ops_id << " connected";
}

void CameraHalAdapter::CloseDeviceCallback(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    int32_t camera_id,
    mojom::CameraClientType camera_client_type) {
  task_runner->PostTask(FROM_HERE, base::Bind(&CameraHalAdapter::CloseDevice,
                                              base::Unretained(this), camera_id,
                                              camera_client_type));
}

// static
void CameraHalAdapter::camera_device_status_change(
    const camera_module_callbacks_t* callbacks,
    int internal_camera_id,
    int new_status) {
  VLOGF_ENTER();
  TRACE_CAMERA_SCOPED();

  auto* aux = static_cast<const CameraModuleCallbacksAux*>(callbacks);
  CameraHalAdapter* self = aux->adapter;
  self->camera_module_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&CameraHalAdapter::CameraDeviceStatusChange,
                            base::Unretained(self), aux, internal_camera_id,
                            static_cast<camera_device_status_t>(new_status)));
}

// static
void CameraHalAdapter::torch_mode_status_change(
    const camera_module_callbacks_t* callbacks,
    const char* internal_camera_id,
    int new_status) {
  VLOGF_ENTER();
  TRACE_CAMERA_SCOPED();

  auto* aux = static_cast<const CameraModuleCallbacksAux*>(callbacks);
  CameraHalAdapter* self = aux->adapter;
  self->camera_module_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&CameraHalAdapter::TorchModeStatusChange,
                 base::Unretained(self), aux, atoi(internal_camera_id),
                 static_cast<torch_mode_status_t>(new_status)));
}

void CameraHalAdapter::CameraDeviceStatusChange(
    const CameraModuleCallbacksAux* aux,
    int internal_camera_id,
    camera_device_status_t new_status) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();

  int external_camera_id = GetExternalId(aux->module_id, internal_camera_id);

  LOGF(INFO) << "module_id = " << aux->module_id
             << ", internal_camera_id = " << internal_camera_id
             << ", new_status = " << new_status;

  switch (new_status) {
    case CAMERA_DEVICE_STATUS_PRESENT:
      if (external_camera_id == -1) {
        external_camera_id = next_external_camera_id_++;
        camera_id_map_[external_camera_id] =
            std::make_pair(aux->module_id, internal_camera_id);
        camera_id_inverse_map_[aux->module_id][internal_camera_id] =
            external_camera_id;
        device_status_map_[external_camera_id] = CAMERA_DEVICE_STATUS_PRESENT;
        default_device_status_map_[external_camera_id] =
            CAMERA_DEVICE_STATUS_NOT_PRESENT;
        torch_mode_status_map_[external_camera_id] =
            TORCH_MODE_STATUS_NOT_AVAILABLE;
        default_torch_mode_status_map_[external_camera_id] =
            TORCH_MODE_STATUS_NOT_AVAILABLE;
      } else {
        device_status_map_[external_camera_id] = CAMERA_DEVICE_STATUS_PRESENT;
      }
      LOGF(INFO) << "External camera plugged, external_camera_id = "
                 << external_camera_id;
      break;
    case CAMERA_DEVICE_STATUS_NOT_PRESENT:
      if (external_camera_id != -1) {
        device_status_map_[external_camera_id] =
            CAMERA_DEVICE_STATUS_NOT_PRESENT;
        torch_mode_status_map_[external_camera_id] =
            default_torch_mode_status_map_[external_camera_id];
        auto it = device_adapters_.find(external_camera_id);
        if (it != device_adapters_.end()) {
          device_adapters_.erase(it);
        }
        LOGF(INFO) << "External camera unplugged"
                   << ", external_camera_id = " << external_camera_id;
      } else {
        LOGF(WARNING) << "Ignore nonexistent camera";
      }
      break;
    default:
      // TODO(shik): What about CAMERA_DEVICE_STATUS_ENUMERATING?
      NOTREACHED() << "Unexpected new status " << new_status;
      break;
  }

  base::AutoLock l(callbacks_delegates_lock_);
  for (auto& it : callbacks_delegates_) {
    NotifyCameraDeviceStatusChange(it.second.get(), external_camera_id,
                                   new_status);
  }
  for (auto& it : callbacks_associated_delegates_) {
    NotifyCameraDeviceStatusChange(it.second.get(), external_camera_id,
                                   new_status);
  }
}

void CameraHalAdapter::TorchModeStatusChange(
    const CameraModuleCallbacksAux* aux,
    int internal_camera_id,
    torch_mode_status_t new_status) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();

  int camera_id = GetExternalId(aux->module_id, internal_camera_id);
  if (camera_id == -1) {
    LOGF(WARNING) << "Ignore nonexistent camera"
                  << ", module_id = " << aux->module_id
                  << ", camera_id = " << internal_camera_id;
    return;
  }

  torch_mode_status_map_[camera_id] = new_status;

  base::AutoLock l(callbacks_delegates_lock_);
  for (auto& it : callbacks_delegates_) {
    NotifyTorchModeStatusChange(it.second.get(), camera_id, new_status);
  }
  for (auto& it : callbacks_associated_delegates_) {
    NotifyTorchModeStatusChange(it.second.get(), camera_id, new_status);
  }
}

void CameraHalAdapter::StartOnThread(base::Callback<void(bool)> callback) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());

  if (reprocess_effect_manager_.Initialize(mojo_manager_token_) != 0) {
    LOGF(ERROR) << "Failed to initialize reprocess effect manager";
    callback.Run(false);
    return;
  }

  if (!vendor_tag_manager_.Add(&reprocess_effect_manager_)) {
    LOGF(ERROR) << "Failed to add the vendor tags of reprocess effect manager";
    callback.Run(false);
    return;
  }

  // The setup sequence for each camera HAL:
  //   1. get_vendor_tag_ops()
  //   2. init()
  //   3. get_number_of_cameras()
  //   4. set_callbacks()
  //   5. get_camera_info()
  //
  // Normally, init() is the first invoked method in the sequence.  But init()
  // might manipulate vendor tags with libcamera_metadata, which requires
  // set_camera_metadata_vendor_ops() to be invoked already.  To prepare the
  // aggregated |vendor_tag_ops| for set_camera_metadata_vendor_ops(), we need
  // to collect |vendor_tag_ops| from all camera modules by calling
  // get_vendor_tag_ops() first, which should be fine as it just set some
  // function pointers in the struct.
  //
  // Note that camera HALs MAY run callbacks before set_callbacks() returns.

  for (const auto& m : camera_modules_) {
    if (m->get_vendor_tag_ops) {
      vendor_tag_ops ops = {};
      m->get_vendor_tag_ops(&ops);
      if (ops.get_tag_count == nullptr) {
        continue;
      }
      if (!vendor_tag_manager_.Add(&ops)) {
        LOGF(ERROR) << "Failed to add the vendor tags of camera module "
                    << std::quoted(m->common.name);
        callback.Run(false);
        return;
      }
    }
  }

  if (set_camera_metadata_vendor_ops(&vendor_tag_manager_) != 0) {
    LOGF(ERROR) << "Failed to set vendor ops to camera metadata";
  }

  for (const auto& m : camera_modules_) {
    if (m->init) {
      int ret = m->init();
      if (ret != 0) {
        LOGF(ERROR) << "Failed to init camera module "
                    << std::quoted(m->common.name);
        callback.Run(false);
        return;
      }
    }
  }

  std::vector<std::tuple<int, int, int>> cameras;
  std::vector<std::vector<bool>> has_flash_unit(camera_modules_.size());

  camera_id_inverse_map_.resize(camera_modules_.size());
  for (size_t module_id = 0; module_id < camera_modules_.size(); module_id++) {
    camera_module_t* m = camera_modules_[module_id];

    int n = m->get_number_of_cameras();
    LOGF(INFO) << "Camera module " << std::quoted(m->common.name) << " has "
               << n << " built-in camera(s)";

    auto aux = std::make_unique<CameraModuleCallbacksAux>();
    aux->camera_device_status_change = camera_device_status_change;
    aux->torch_mode_status_change = torch_mode_status_change;
    aux->module_id = module_id;
    aux->adapter = this;
    if (m->set_callbacks(aux.get()) != 0) {
      LOGF(ERROR) << "Failed to set_callbacks on camera module " << module_id;
      callback.Run(false);
      return;
    }
    callbacks_auxs_.push_back(std::move(aux));

    for (int camera_id = 0; camera_id < n; camera_id++) {
      camera_info_t info;
      if (m->get_camera_info(camera_id, &info) != 0) {
        LOGF(ERROR) << "Failed to get info of camera " << camera_id
                    << " from module " << module_id;
        callback.Run(false);
        return;
      }

      camera_metadata_ro_entry_t entry;
      if (find_camera_metadata_ro_entry(info.static_camera_characteristics,
                                        ANDROID_FLASH_INFO_AVAILABLE,
                                        &entry) != 0) {
        LOGF(ERROR) << "Failed to get flash info in metadata of camera "
                    << camera_id << " from module " << module_id;
        callback.Run(false);
        return;
      }

      cameras.emplace_back(info.facing, static_cast<int>(module_id), camera_id);
      has_flash_unit[module_id].push_back(entry.data.u8[0] ==
                                          ANDROID_FLASH_INFO_AVAILABLE_TRUE);
    }
  }

  sort(cameras.begin(), cameras.end());
  for (size_t i = 0; i < cameras.size(); i++) {
    int module_id = std::get<1>(cameras[i]);
    int camera_id = std::get<2>(cameras[i]);
    camera_id_map_[i] = std::make_pair(module_id, camera_id);
    camera_id_inverse_map_[module_id][camera_id] = i;
    device_status_map_[i] = CAMERA_DEVICE_STATUS_PRESENT;
    default_device_status_map_[i] = device_status_map_[i];
    torch_mode_status_map_[i] = has_flash_unit[module_id][camera_id]
                                    ? TORCH_MODE_STATUS_AVAILABLE_OFF
                                    : TORCH_MODE_STATUS_NOT_AVAILABLE;
    default_torch_mode_status_map_[i] = torch_mode_status_map_[i];
  }

  num_builtin_cameras_ = cameras.size();
  next_external_camera_id_ = num_builtin_cameras_;

  LOGF(INFO) << "SuperHAL started with " << camera_modules_.size()
             << " modules and " << num_builtin_cameras_ << " built-in cameras";

  callback.Run(true);
}

void CameraHalAdapter::NotifyCameraDeviceStatusChange(
    CameraModuleCallbacksDelegate* delegate,
    int camera_id,
    camera_device_status_t status) {
  delegate->CameraDeviceStatusChange(camera_id, status);
}

void CameraHalAdapter::NotifyCameraDeviceStatusChange(
    CameraModuleCallbacksAssociatedDelegate* delegate,
    int camera_id,
    camera_device_status_t status) {
  delegate->CameraDeviceStatusChange(camera_id, status);
}

void CameraHalAdapter::NotifyTorchModeStatusChange(
    CameraModuleCallbacksDelegate* delegate,
    int camera_id,
    torch_mode_status_t status) {
  delegate->TorchModeStatusChange(camera_id, status);
}

void CameraHalAdapter::NotifyTorchModeStatusChange(
    CameraModuleCallbacksAssociatedDelegate* delegate,
    int camera_id,
    torch_mode_status_t status) {
  delegate->TorchModeStatusChange(camera_id, status);
}

std::pair<camera_module_t*, int> CameraHalAdapter::GetInternalModuleAndId(
    int camera_id) {
  if (camera_id_map_.find(camera_id) == camera_id_map_.end()) {
    LOGF(ERROR) << "Invalid camera id: " << camera_id;
    return {};
  }
  std::pair<int, int> idx = camera_id_map_[camera_id];
  return {camera_modules_[idx.first], idx.second};
}

int CameraHalAdapter::GetExternalId(int module_id, int camera_id) {
  if (module_id < 0 ||
      static_cast<size_t>(module_id) >= camera_id_inverse_map_.size()) {
    return -1;
  }

  std::map<int, int>& id_map = camera_id_inverse_map_[module_id];
  auto it = id_map.find(camera_id);
  return it != id_map.end() ? it->second : -1;
}

void CameraHalAdapter::CloseDevice(int32_t camera_id,
                                   mojom::CameraClientType camera_client_type) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED("camera_id", camera_id);
  LOGF(INFO) << camera_client_type << ", camera_id = " << camera_id;
  if (device_adapters_.find(camera_id) == device_adapters_.end()) {
    LOGF(ERROR) << "Failed to close camera device " << camera_id
                << ": device is not opened";
    return;
  }
  device_adapters_.erase(camera_id);

  activity_callback_.Run(camera_id, /*opened=*/false, camera_client_type);

  camera_metrics_->SendSessionDuration(session_timer_map_[camera_id].Elapsed());
  session_timer_map_.erase(camera_id);
}

void CameraHalAdapter::ResetModuleDelegateOnThread(uint32_t module_id) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  base::AutoLock l(module_delegates_lock_);
  if (module_id == kIdAll) {
    module_delegates_.clear();
  } else {
    module_delegates_.erase(module_id);
  }
}

void CameraHalAdapter::ResetCallbacksDelegateOnThread(uint32_t callbacks_id) {
  VLOGF_ENTER();
  DCHECK(
      camera_module_callbacks_thread_.task_runner()->BelongsToCurrentThread());
  base::AutoLock l(callbacks_delegates_lock_);
  if (callbacks_id == kIdAll) {
    callbacks_delegates_.clear();
    callbacks_associated_delegates_.clear();
  } else {
    callbacks_delegates_.erase(callbacks_id);
    callbacks_associated_delegates_.erase(callbacks_id);
  }
}

void CameraHalAdapter::ResetVendorTagOpsDelegateOnThread(
    uint32_t vendor_tag_ops_id) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  base::AutoLock l(module_delegates_lock_);
  if (vendor_tag_ops_id == kIdAll) {
    vendor_tag_ops_delegates_.clear();
  } else {
    vendor_tag_ops_delegates_.erase(vendor_tag_ops_id);
  }
}

int32_t CameraHalAdapter::SetCallbacksAssociated(
    mojom::CameraModuleCallbacksAssociatedPtrInfo callbacks_info) {
  VLOGF_ENTER();
  DCHECK(camera_module_thread_.task_runner()->BelongsToCurrentThread());
  TRACE_CAMERA_SCOPED();

  auto callbacks_delegate =
      std::make_unique<CameraModuleCallbacksAssociatedDelegate>(
          camera_module_callbacks_thread_.task_runner());
  uint32_t callbacks_id = callbacks_id_++;
  callbacks_delegate->Bind(
      std::move(callbacks_info),
      base::Bind(&CameraHalAdapter::ResetCallbacksDelegateOnThread,
                 base::Unretained(this), callbacks_id));

  // Send latest status to the new client, so all presented external cameras are
  // available to the client after SetCallbacks() returns.
  for (const auto& it : device_status_map_) {
    int camera_id = it.first;
    camera_device_status_t device_status = it.second;
    if (device_status != default_device_status_map_[camera_id]) {
      NotifyCameraDeviceStatusChange(callbacks_delegate.get(), camera_id,
                                     device_status);
    }
    torch_mode_status_t torch_status = torch_mode_status_map_[camera_id];
    if (torch_status != default_torch_mode_status_map_[camera_id]) {
      NotifyTorchModeStatusChange(callbacks_delegate.get(), camera_id,
                                  torch_status);
    }
  }

  base::AutoLock l(callbacks_delegates_lock_);
  callbacks_associated_delegates_[callbacks_id] = std::move(callbacks_delegate);

  return 0;
}
}  // namespace cros
