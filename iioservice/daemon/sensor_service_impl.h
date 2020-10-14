// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_DAEMON_SENSOR_SERVICE_IMPL_H_
#define IIOSERVICE_DAEMON_SENSOR_SERVICE_IMPL_H_

#include <map>
#include <memory>
#include <set>
#include <vector>

#include <base/bind.h>
#include <base/sequenced_task_runner.h>
#include <base/single_thread_task_runner.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include <libmems/iio_context.h>

#include "iioservice/daemon/sensor_device_impl.h"
#include "mojo/sensor.mojom.h"

namespace iioservice {

class SensorServiceImpl : public cros::mojom::SensorService {
 public:
  static void SensorServiceImplDeleter(SensorServiceImpl* service);
  using ScopedSensorServiceImpl =
      std::unique_ptr<SensorServiceImpl, decltype(&SensorServiceImplDeleter)>;

  static ScopedSensorServiceImpl Create(
      scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
      std::unique_ptr<libmems::IioContext> context);

  virtual void AddReceiver(
      mojo::PendingReceiver<cros::mojom::SensorService> request);

  // cros::mojom::SensorService overrides:
  void GetDeviceIds(cros::mojom::DeviceType type,
                    GetDeviceIdsCallback callback) override;
  void GetAllDeviceIds(GetAllDeviceIdsCallback callback) override;
  void GetDevice(
      int32_t iio_device_id,
      mojo::PendingReceiver<cros::mojom::SensorDevice> device_request) override;

 protected:
  SensorServiceImpl(scoped_refptr<base::SequencedTaskRunner> ipc_task_runner,
                    std::unique_ptr<libmems::IioContext> context,
                    SensorDeviceImpl::ScopedSensorDeviceImpl sensor_device);

 private:
  void SetDeviceTypes();

  scoped_refptr<base::SequencedTaskRunner> ipc_task_runner_;
  std::unique_ptr<libmems::IioContext> context_;
  SensorDeviceImpl::ScopedSensorDeviceImpl sensor_device_;

  // First is the iio_device's id, second is the types.
  std::map<int32_t, std::vector<cros::mojom::DeviceType>> device_types_map_;

  mojo::ReceiverSet<cros::mojom::SensorService> receiver_set_;
};

}  // namespace iioservice

#endif  // IIOSERVICE_DAEMON_SENSOR_SERVICE_IMPL_H_
