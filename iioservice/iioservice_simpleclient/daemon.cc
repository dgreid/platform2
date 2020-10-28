// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "iioservice/iioservice_simpleclient/daemon.h"

#include <sysexits.h>

#include <memory>
#include <utility>

#include <base/bind.h>
#include <mojo/core/embedder/embedder.h>

#include "iioservice/iioservice_simpleclient/observer_impl.h"
#include "iioservice/include/common.h"

namespace iioservice {

TestDaemon::TestDaemon(int device_id,
                       cros::mojom::DeviceType device_type,
                       std::vector<std::string> channel_ids,
                       double frequency,
                       int timeout,
                       int samples)
    : device_id_(device_id),
      device_type_(device_type),
      channel_ids_(std::move(channel_ids)),
      frequency_(frequency),
      timeout_(timeout),
      samples_(samples),
      weak_ptr_factory_(this) {}

TestDaemon::~TestDaemon() {}

int TestDaemon::OnInit() {
  int exit_code = DBusDaemon::OnInit();
  if (exit_code != EX_OK)
    return exit_code;

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);

  SetBus(bus_.get());
  BootstrapMojoConnection();

  observer_ = iioservice::ObserverImpl::Create(
      base::ThreadTaskRunnerHandle::Get(), device_id_, device_type_,
      std::move(channel_ids_), frequency_, timeout_, samples_,
      base::BindOnce(&TestDaemon::OnMojoDisconnect,
                     weak_ptr_factory_.GetWeakPtr()));

  return exit_code;
}

void TestDaemon::OnClientReceived(
    mojo::PendingReceiver<cros::mojom::SensorHalClient> client) {
  observer_->BindClient(std::move(client));
}

void TestDaemon::OnMojoDisconnect() {
  LOGF(INFO) << "Quiting this process.";
  Quit();
}

}  // namespace iioservice
