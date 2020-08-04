// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ocr/mojo_adapter/ocr_service_mojo_adapter_delegate_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/synchronization/waitable_event.h>
#include <brillo/dbus/dbus_method_invoker.h>
#include <brillo/dbus/file_descriptor.h>
#include <brillo/errors/error.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/message.h>
#include <dbus/object_path.h>
#include <dbus/object_proxy.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/bindings/remote.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/system/invitation.h>
#include <mojo/public/cpp/system/message_pipe.h>

namespace ocr {

namespace {

namespace mojo_ipc = chromeos::ocr::mojom;

// Sends |raw_fd| to OCR Daemon via D-Bus. Sets |token_out| to a unique token
// which can be used to create a message pipe to the OCR service.
void DoDBusBootstrap(int raw_fd,
                     base::WaitableEvent* event,
                     std::string* token_out) {
  dbus::Bus::Options bus_options;
  bus_options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus = new dbus::Bus(bus_options);

  CHECK(bus->Connect());

  dbus::ObjectProxy* ocr_service_proxy =
      bus->GetObjectProxy(kOcrServiceName, dbus::ObjectPath(kOcrServicePath));

  brillo::dbus_utils::FileDescriptor fd(raw_fd);
  brillo::ErrorPtr error;
  auto response = brillo::dbus_utils::CallMethodAndBlock(
      ocr_service_proxy, kOcrServiceInterface, kBootstrapMojoConnectionMethod,
      &error, fd, false /* should_accept_invitation */);

  if (!response) {
    LOG(ERROR) << "No response received.";
    return;
  }

  dbus::MessageReader reader(response.get());
  if (!reader.PopString(token_out)) {
    LOG(ERROR) << "Failed to extract token.";
    return;
  }

  event->Signal();
}

}  // namespace

OcrServiceMojoAdapterDelegateImpl::OcrServiceMojoAdapterDelegateImpl() {
  CHECK(mojo_thread_.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0)))
      << "Failed starting the Mojo thread.";

  CHECK(dbus_thread_.StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0)))
      << "Failed starting the D-Bus thread.";

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      mojo_thread_.task_runner(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::CLEAN);
}

OcrServiceMojoAdapterDelegateImpl::~OcrServiceMojoAdapterDelegateImpl() =
    default;

mojo::Remote<mojo_ipc::OpticalCharacterRecognitionService>
OcrServiceMojoAdapterDelegateImpl::GetOcrService() {
  mojo::PlatformChannel channel;
  std::string token;

  // Pass the other end of the pipe to the OCR daemon. Wait for this task to
  // run, since we need the resulting token to continue. The OCR daemon will
  // send an invitation to connect to the OCR service.
  base::WaitableEvent event(base::WaitableEvent::ResetPolicy::AUTOMATIC,
                            base::WaitableEvent::InitialState::NOT_SIGNALED);
  dbus_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(
          &DoDBusBootstrap,
          channel.TakeRemoteEndpoint().TakePlatformHandle().TakeFD().release(),
          &event, &token));
  event.Wait();

  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(channel.TakeLocalEndpoint());
  mojo::ScopedMessagePipeHandle pipe = invitation.ExtractMessagePipe(token);

  return mojo::Remote<mojo_ipc::OpticalCharacterRecognitionService>(
      mojo::PendingRemote<mojo_ipc::OpticalCharacterRecognitionService>(
          std::move(pipe), 0));
}

}  // namespace ocr
