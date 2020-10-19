// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_SERVER_DBUS_H_
#define IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_SERVER_DBUS_H_

#include <base/memory/weak_ptr.h>

#include "iioservice/include/export.h"
#include "iioservice/libiioservice_ipc/sensor_dbus.h"
#include "mojo/cros_sensor_service.mojom.h"

namespace iioservice {

class IIOSERVICE_EXPORT SensorServerDbus : public SensorDbus {
 public:
  virtual ~SensorServerDbus() = default;

  void BootstrapMojoConnection() override;

 protected:
  SensorServerDbus() = default;

  void OnInvitationReceived(mojo::IncomingInvitation invitation) override;

  virtual void OnServerReceived(
      mojo::PendingReceiver<cros::mojom::SensorHalServer> server) = 0;

 private:
  base::WeakPtrFactory<SensorServerDbus> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_SERVER_DBUS_H_
