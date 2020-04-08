// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/daemon.h"

#include <memory>
#include <utility>

#include <base/message_loop/message_loop.h>
#include <chromeos/dbus/service_constants.h>
#include <google-lpa/lpa/core/lpa.h>

#include "hermes/card_qrtr.h"
#include "hermes/socket_qrtr.h"

namespace hermes {

Daemon::Daemon()
    : DBusServiceDaemon(kHermesServiceName),
      executor_(base::MessageLoop::current()),
      smdp_(&logger_, &executor_) {
  card_ =
      CardQrtr::Create(std::make_unique<SocketQrtr>(), &logger_, &executor_);

  lpa::core::Lpa::Builder b;
  b.SetEuiccCard(card_.get())
      .SetSmdpClientFactory(&smdp_)
      .SetSmdsClientFactory(&smds_)
      .SetLogger(&logger_);
  lpa_ = b.Build();

  context_.executor = &executor_;
  context_.lpa = lpa_.get();
}

void Daemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  manager_ = std::make_unique<Manager>(bus_, &context_);
}

}  // namespace hermes
