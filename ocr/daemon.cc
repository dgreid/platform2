// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ocr/daemon.h"

#include <sysexits.h>
#include <utility>

#include <base/logging.h>
#include <base/threading/thread_task_runner_handle.h>
#include <dbus/object_path.h>
#include <chromeos/dbus/service_constants.h>
#include <mojo/core/embedder/embedder.h>

namespace ocr {

OcrDaemon::OcrDaemon() : brillo::DBusServiceDaemon(kOcrServiceName) {}

OcrDaemon::~OcrDaemon() = default;

int OcrDaemon::OnInit() {
  int return_code = brillo::DBusServiceDaemon::OnInit();
  if (return_code != EX_OK)
    return return_code;

  // Initialize Mojo IPC.
  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get() /* io_thread_task_runner */,
      mojo::core::ScopedIPCSupport::ShutdownPolicy::
          CLEAN /* blocking shutdown */);

  return EX_OK;
}

void OcrDaemon::RegisterDBusObjectsAsync(
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  DCHECK(!dbus_object_);
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      nullptr /* object_manager */, bus_, dbus::ObjectPath(kOcrServicePath));
  brillo::dbus_utils::DBusInterface* dbus_interface =
      dbus_object_->AddOrGetInterface(kOcrServiceInterface);
  DCHECK(dbus_interface);
  dbus_interface->AddSimpleMethodHandler(kBootstrapMojoConnectionMethod,
                                         weak_ptr_factory_.GetWeakPtr(),
                                         &OcrDaemon::BootstrapMojoConnection);
  dbus_object_->RegisterAsync(sequencer->GetHandler(
      "Failed to register D-Bus object" /* descriptive_message */,
      true /* failure_is_fatal */));
}

void OcrDaemon::BootstrapMojoConnection(const base::ScopedFD& mojo_fd) {
  VLOG(1) << "Received BootstrapMojoConnection D-Bus request";
  // TODO(emavroudi): bootstrap Mojo connection between client and service.
  VLOG(1) << "Successfully bootstrapped Mojo connection";
}

void OcrDaemon::OnConnectionError() {
  // Die upon Mojo error. Reconnection can occur when the daemon is restarted.
  // (A future Mojo API may enable Mojo re-bootstrap without a process restart.)
  LOG(ERROR) << "OcrDaemon MojoConnectionError; quitting.";
  Quit();
}

}  // namespace ocr
