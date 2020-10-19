// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_DBUS_H_
#define IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_DBUS_H_

#include <base/memory/weak_ptr.h>
#include <base/sequence_checker.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <mojo/public/cpp/system/invitation.h>

#include "iioservice/include/export.h"

namespace iioservice {

class IIOSERVICE_EXPORT SensorDbus {
 public:
  virtual ~SensorDbus() = default;

  // |SetBus| before using |BootstrapMojoConnection|.
  void SetBus(dbus::Bus* sensor_bus);

  virtual void BootstrapMojoConnection() = 0;

 protected:
  SensorDbus() = default;

  void OnBootstrapMojoResponse(dbus::Response* response);
  virtual void ReconnectMojoWithDelay();

  virtual void OnInvitationReceived(mojo::IncomingInvitation invitation) = 0;

  dbus::Bus* sensor_bus_;

  SEQUENCE_CHECKER(sensor_sequence_checker_);

 private:
  base::WeakPtrFactory<SensorDbus> weak_factory_{this};
};

}  // namespace iioservice

#endif  // IIOSERVICE_LIBIIOSERVICE_IPC_SENSOR_DBUS_H_
