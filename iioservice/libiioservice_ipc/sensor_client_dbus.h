// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_CLIENT_DBUS_H_
#define IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_CLIENT_DBUS_H_

#include <base/memory/weak_ptr.h>

#include "iioservice/include/export.h"
#include "iioservice/libiioservice_ipc/sensor_dbus.h"
#include "mojo/cros_sensor_service.mojom.h"

namespace iioservice {

class IIOSERVICE_EXPORT SensorClientDbus : public SensorDbus {
 public:
  virtual ~SensorClientDbus() = default;

  void BootstrapMojoConnection() override;

 protected:
  SensorClientDbus() = default;

  void OnInvitationReceived(mojo::IncomingInvitation invitation) override;

  virtual void OnClientReceived(
      mojo::PendingReceiver<cros::mojom::SensorHalClient> client) = 0;

 private:
  base::WeakPtrFactory<SensorClientDbus> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_CLIENT_DBUS_H_
