// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/daemon/sensor_service_impl.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <base/containers/flat_map.h>
#include <brillo/udev/udev.h>
#include <libmems/iio_device.h>
#include <libmems/iio_channel.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "iioservice/include/common.h"

namespace iioservice {

// Add a namespace here to not leak |DeviceHasType|.
namespace {

constexpr int kPermTrialDelayInMilliseconds = 100;

// Prefixes for each cros::mojom::DeviceType channel.
constexpr char kChnPrefixes[][12] = {
    "",             // NONE
    "accel_",       // ACCEL
    "anglvel_",     // ANGLVEL
    "illuminance",  // LIGHT
    "count",        // COUNT
    "magn_",        // MAGN
    "angl",         // ANGL
    "pressure",     // BARO
};

bool DeviceHasType(libmems::IioDevice* iio_device,
                   cros::mojom::DeviceType type) {
  auto channels = iio_device->GetAllChannels();
  int type_int = static_cast<int>(type);
  switch (type) {
    case cros::mojom::DeviceType::ACCEL:
    case cros::mojom::DeviceType::ANGLVEL:
    case cros::mojom::DeviceType::MAGN:
      for (auto chn : channels) {
        if (strncmp(chn->GetId(), kChnPrefixes[type_int],
                    strlen(kChnPrefixes[type_int])) == 0)
          return true;
      }

      return false;

    case cros::mojom::DeviceType::LIGHT:
    case cros::mojom::DeviceType::COUNT:
    case cros::mojom::DeviceType::ANGL:
    case cros::mojom::DeviceType::BARO:
      for (auto chn : channels) {
        if (strcmp(chn->GetId(), kChnPrefixes[type_int]) == 0)
          return true;
      }

      return false;

    default:
      return false;
  }
}

}  // namespace

// static
void SensorServiceImpl::SensorServiceImplDeleter(SensorServiceImpl* service) {
  if (service == nullptr)
    return;

  if (!service->ipc_task_runner_->RunsTasksInCurrentSequence()) {
    service->ipc_task_runner_->PostTask(
        FROM_HERE,
        base::BindOnce(&SensorServiceImpl::SensorServiceImplDeleter, service));
    return;
  }

  delete service;
}

// static
SensorServiceImpl::ScopedSensorServiceImpl SensorServiceImpl::Create(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    std::unique_ptr<libmems::IioContext> context,
    std::unique_ptr<brillo::Udev> udev) {
  DCHECK(ipc_task_runner->RunsTasksInCurrentSequence());

  auto sensor_device = SensorDeviceImpl::Create(ipc_task_runner, context.get());

  if (!sensor_device) {
    LOGF(ERROR) << "Failed to get SensorDevice";
    return ScopedSensorServiceImpl(nullptr, SensorServiceImplDeleter);
  }

  return ScopedSensorServiceImpl(
      new SensorServiceImpl(std::move(ipc_task_runner), std::move(context),
                            std::move(udev), std::move(sensor_device)),
      SensorServiceImplDeleter);
}

void SensorServiceImpl::AddReceiver(
    mojo::PendingReceiver<cros::mojom::SensorService> request) {
  receiver_set_.Add(this, std::move(request), ipc_task_runner_);
}

void SensorServiceImpl::GetDeviceIds(cros::mojom::DeviceType type,
                                     GetDeviceIdsCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  std::vector<int32_t> ids;

  for (auto device_types : device_types_map_) {
    for (cros::mojom::DeviceType device_type : device_types.second) {
      if (device_type == type) {
        ids.push_back(device_types.first);
        break;
      }
    }
  }

  std::move(callback).Run(std::move(ids));
}

void SensorServiceImpl::GetAllDeviceIds(GetAllDeviceIdsCallback callback) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  base::flat_map<int32_t, std::vector<cros::mojom::DeviceType>> ids(
      device_types_map_.begin(), device_types_map_.end());

  std::move(callback).Run(ids);
}

void SensorServiceImpl::GetDevice(
    int32_t iio_device_id,
    mojo::PendingReceiver<cros::mojom::SensorDevice> device_request) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (!sensor_device_) {
    LOGF(ERROR) << "No available SensorDevice";
    return;
  }

  sensor_device_->AddReceiver(iio_device_id, std::move(device_request));
}

void SensorServiceImpl::RegisterNewDevicesObserver(
    mojo::PendingRemote<cros::mojom::SensorServiceNewDevicesObserver>
        observer) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  observers_.emplace_back(
      mojo::Remote<cros::mojom::SensorServiceNewDevicesObserver>(
          std::move(observer)));
}

void SensorServiceImpl::OnDeviceAdded(int iio_device_id) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  // Reload to check if there are new devices available.
  context_->Reload();
  auto device = context_->GetDeviceById(iio_device_id);
  if (!device) {
    LOG(ERROR) << "Failed to load device with id: " << iio_device_id;
    return;
  }

  iio_device_permission_trials_[iio_device_id] = 0;
  AddDevice(device);
}

SensorServiceImpl::SensorServiceImpl(
    scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
    std::unique_ptr<libmems::IioContext> context,
    std::unique_ptr<brillo::Udev> udev,
    SensorDeviceImpl::ScopedSensorDeviceImpl sensor_device)
    : ipc_task_runner_(ipc_task_runner),
      context_(std::move(context)),
      udev_watcher_(UdevWatcher::Create(this, std::move(udev))),
      sensor_device_(std::move(sensor_device)) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  if (!sensor_device_)
    LOGF(ERROR) << "Failed to get SensorDevice";

  if (!udev_watcher_.get())
    LOGF(ERROR) << "Late-present sensors won't be tracked.";

  for (auto device : context_->GetAllDevices()) {
    if (device_types_map_.find(device->GetId()) != device_types_map_.end())
      continue;

    AddDevice(device);
  }
}

void SensorServiceImpl::FailedToLoadDevice(libmems::IioDevice* device) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  const int32_t id = device->GetId();
  if (++iio_device_permission_trials_[id] >=
      kNumFailedPermTrialsBeforeGivingUp) {
    LOGF(ERROR) << "Too many failed permission trials. Giving up on device: "
                << id;
    return;
  }

  LOGF(WARNING) << "Permissions and ownerships may not be set yet for device: "
                << id;
  ipc_task_runner_->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SensorServiceImpl::AddDevice, weak_factory_.GetWeakPtr(),
                     device),
      base::TimeDelta::FromMilliseconds(kPermTrialDelayInMilliseconds));
}

void SensorServiceImpl::AddDevice(libmems::IioDevice* device) {
  DCHECK(ipc_task_runner_->RunsTasksInCurrentSequence());

  const int32_t id = device->GetId();
  if (!device->DisableBuffer()) {
    FailedToLoadDevice(device);
    return;
  }

  if (strcmp(device->GetName(), "acpi-als") == 0 && !device->GetTrigger()) {
    // Reloads context for the hrtimer, as it'd be added after mems_setup.
    context_->Reload();

    if (!device->GetTrigger()) {
      FailedToLoadDevice(device);
      return;
    }
  }

  std::vector<cros::mojom::DeviceType> types;
  for (int32_t i = static_cast<int32_t>(cros::mojom::DeviceType::ACCEL);
       i < static_cast<int32_t>(cros::mojom::DeviceType::MAX); ++i) {
    auto type = static_cast<cros::mojom::DeviceType>(i);
    if (DeviceHasType(device, type))
      types.push_back(type);
  }

  device_types_map_.emplace(id, types);

  for (auto& observer : observers_)
    observer->OnNewDeviceAdded(id, types);
}

}  // namespace iioservice
