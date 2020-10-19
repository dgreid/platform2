// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/libiioservice_ipc/sensor_dbus.h"

#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/threading/thread_task_runner_handle.h>
#include <chromeos/dbus/service_constants.h>

namespace iioservice {

namespace {

constexpr int kDelayBootstrapInMilliseconds = 1000;

}

void SensorDbus::SetBus(dbus::Bus* sensor_bus) {
  sensor_bus_ = sensor_bus;
}

void SensorDbus::OnBootstrapMojoResponse(dbus::Response* response) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sensor_sequence_checker_);

  if (!response) {
    LOG(ERROR) << ::mojo_connection_service::kMojoConnectionServiceServiceName
               << " D-Bus call failed";
    ReconnectMojoWithDelay();
    return;
  }

  base::ScopedFD file_handle;
  dbus::MessageReader reader(response);

  if (!reader.PopFileDescriptor(&file_handle)) {
    LOG(ERROR) << "Couldn't extract file descriptor from D-Bus call";
    ReconnectMojoWithDelay();
    return;
  }

  if (!file_handle.is_valid()) {
    LOG(ERROR) << "ScopedFD extracted from D-Bus call was invalid (i.e. empty)";
    ReconnectMojoWithDelay();
    return;
  }

  if (!base::SetCloseOnExec(file_handle.get())) {
    PLOG(ERROR) << "Failed setting FD_CLOEXEC on file descriptor";
    ReconnectMojoWithDelay();
    return;
  }

  // Connect to mojo in the requesting process.
  OnInvitationReceived(
      mojo::IncomingInvitation::Accept(mojo::PlatformChannelEndpoint(
          mojo::PlatformHandle(std::move(file_handle)))));
}

void SensorDbus::ReconnectMojoWithDelay() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sensor_sequence_checker_);

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&SensorDbus::BootstrapMojoConnection,
                     weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(kDelayBootstrapInMilliseconds));
}

}  // namespace iioservice
