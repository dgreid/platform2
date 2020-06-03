// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/mojo_service_factory.h"

#include <utility>

#include <base/files/file_util.h>
#include <base/posix/eintr_wrapper.h>
#include <mojo/public/cpp/bindings/interface_request.h>
#include <mojo/public/cpp/system/invitation.h>
#include <dbus/wilco_dtc_supportd/dbus-constants.h>

#include "diagnostics/wilco_dtc_supportd/mojo_service.h"

namespace diagnostics {
namespace {

MojoServiceFactory::MojoBindingPtr BindMojoServiceFactory(
    MojoServiceFactory::WilcoServiceFactory* mojo_service_factory,
    base::ScopedFD mojo_pipe_fd) {
  DCHECK(mojo_service_factory);
  DCHECK(mojo_pipe_fd.is_valid());

  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(mojo::PlatformChannelEndpoint(
          mojo::PlatformHandle(std::move(mojo_pipe_fd))));

  mojo::ScopedMessagePipeHandle mojo_pipe_handle =
      invitation.ExtractMessagePipe(
          kWilcoDtcSupportdMojoConnectionChannelToken);
  if (!mojo_pipe_handle.is_valid()) {
    return nullptr;
  }

  return std::make_unique<mojo::Binding<
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactory>>(
      mojo_service_factory,
      mojo::InterfaceRequest<
          chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactory>(
          std::move(mojo_pipe_handle)));
}

}  // namespace

MojoServiceFactory::MojoServiceFactory(
    MojoGrpcAdapter* grpc_adapter,
    base::RepeatingClosure shutdown,
    BindFactoryCallback bind_factory_callback)
    : grpc_adapter_(grpc_adapter),
      shutdown_(shutdown),
      bind_factory_callback_(std::move(bind_factory_callback)) {
  DCHECK(grpc_adapter_);
}

MojoServiceFactory::~MojoServiceFactory() = default;

MojoService* MojoServiceFactory::Get() const {
  return mojo_service_.get();
}

base::Optional<std::string> MojoServiceFactory::BootstrapMojoConnection(
    const base::ScopedFD& mojo_fd) {
  if (!mojo_fd.is_valid()) {
    LOG(ERROR) << "Invalid Mojo file descriptor";
    return "Invalid file descriptor";
  }

  // We need a file descriptor that stays alive after the current method
  // finishes, but libbrillo's D-Bus wrappers currently don't support passing
  // base::ScopedFD by value.
  base::ScopedFD mojo_fd_copy(HANDLE_EINTR(dup(mojo_fd.get())));
  if (!mojo_fd_copy.is_valid()) {
    PLOG(ERROR) << "Failed to duplicate the Mojo file descriptor";
    return "Failed to duplicate file descriptor";
  }

  return Start(std::move(mojo_fd_copy));
}

base::Optional<std::string> MojoServiceFactory::Start(
    base::ScopedFD mojo_pipe_fd) {
  DCHECK(mojo_pipe_fd.is_valid());

  if (mojo_service_bind_attempted_) {
    // This should not normally be triggered, since the other endpoint - the
    // browser process - should bootstrap the Mojo connection only once, and
    // when that process is killed the Mojo shutdown notification should have
    // been received earlier. But handle this case to be on the safe side. After
    // our restart the browser process is expected to invoke the bootstrapping
    // again.
    ShutdownDueToMojoError(
        "Repeated Mojo bootstrap request received" /* debug_reason */);
    return "Mojo connection was already bootstrapped";
  }

  if (!base::SetCloseOnExec(mojo_pipe_fd.get())) {
    PLOG(ERROR) << "Failed to set FD_CLOEXEC on Mojo file descriptor";
    return "Failed to set FD_CLOEXEC";
  }

  mojo_service_bind_attempted_ = true;
  mojo_service_factory_binding_ =
      std::move(bind_factory_callback_).Run(this, std::move(mojo_pipe_fd));
  if (!mojo_service_factory_binding_) {
    ShutdownDueToMojoError("Mojo bootstrap failed" /* debug_reason */);
    return "Failed to bootstrap Mojo";
  }
  mojo_service_factory_binding_->set_connection_error_handler(base::Bind(
      &MojoServiceFactory::ShutdownDueToMojoError, base::Unretained(this),
      "Mojo connection error" /* debug_reason */));

  LOG(INFO) << "Successfully bootstrapped Mojo connection";
  return base::nullopt;
}

MojoServiceFactory::BindFactoryCallback
MojoServiceFactory::CreateBindFactoryCallback() {
  return base::BindOnce(&BindMojoServiceFactory);
}

void MojoServiceFactory::ShutdownDueToMojoError(
    const std::string& debug_reason) {
  // Our daemon has to be restarted to be prepared for future Mojo connection
  // bootstraps. We can't do this without a restart since Mojo EDK gives no
  // guarantee to support repeated bootstraps. Therefore tear down and exit from
  // our process and let upstart to restart us again.
  LOG(INFO) << "Shutting down due to: " << debug_reason;

  mojo_service_.reset();
  mojo_service_factory_binding_.reset();

  shutdown_.Run();
}

void MojoServiceFactory::GetService(
    chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceRequest service,
    chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdClientPtr client,
    GetServiceCallback callback) {
  // Mojo guarantees that these parameters are nun-null (see
  // VALIDATION_ERROR_UNEXPECTED_INVALID_HANDLE).
  DCHECK(service.is_pending());
  DCHECK(client);

  if (mojo_service_) {
    LOG(WARNING) << "GetService Mojo method called multiple times";
    // We should not normally be called more than once, so don't bother with
    // trying to reuse objects from the previous call. However, make sure we
    // don't have duplicate instances of the service at any moment of time.
    mojo_service_.reset();
  }

  // Create an instance of MojoService that will handle incoming
  // Mojo calls. Pass |service| to it to fulfill the remote endpoint's request,
  // allowing it to call into |mojo_service_|. Pass also |client| to allow
  // |mojo_service_| to do calls in the opposite direction.
  mojo_service_ = std::make_unique<MojoService>(
      grpc_adapter_, std::move(service), std::move(client));

  std::move(callback).Run();
}

}  // namespace diagnostics
