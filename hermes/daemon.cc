// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/daemon.h"

#include <memory>
#include <utility>

#include <base/threading/thread_task_runner_handle.h>
#include <chromeos/dbus/service_constants.h>
#include <google-lpa/lpa/core/lpa.h>

#include "hermes/context.h"
#include "hermes/modem_qrtr.h"
#include "hermes/socket_qrtr.h"

namespace hermes {

Daemon::Daemon()
    : DBusServiceDaemon(kHermesServiceName),
      executor_(base::ThreadTaskRunnerHandle::Get()),
      smdp_(&logger_, &executor_) {}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  modem_ =
      ModemQrtr::Create(std::make_unique<SocketQrtr>(), &logger_, &executor_);

  lpa::core::Lpa::Builder b;
  b.SetEuiccCard(modem_.get())
      .SetSmdpClientFactory(&smdp_)
      .SetSmdsClientFactory(&smds_)
      .SetLogger(&logger_);
  lpa_ = b.Build();

  Context::Initialize(bus_, lpa_.get(), &executor_, &adaptor_factory_,
                      dynamic_cast<ModemControlInterface*>(modem_.get()));
  manager_ = std::make_unique<Manager>();
  // TODO(crbug.com/1085825) Once a Channel class is created to abstract out the
  // logical channel logic in ModemQrtr, a Channel (subclass?) can be used as an
  // EuiccCard rather than the ModemQrtr instance.
  static_cast<ModemQrtr*>(modem_.get())->Initialize(manager_.get());
}

}  // namespace hermes
