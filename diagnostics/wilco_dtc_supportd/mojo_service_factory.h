// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_MOJO_SERVICE_FACTORY_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_MOJO_SERVICE_FACTORY_H_

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/files/scoped_file.h>
#include <base/optional.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "mojo/wilco_dtc_supportd.mojom.h"

namespace diagnostics {

class MojoService;
class MojoGrpcAdapter;

// Bootstraps Mojo connection between Chrome and wilco_dtc_supportd daemon over
// D-Bus connection.
//
// Implements the "WilcoDtcSupportdServiceFactory" Mojo interface exposed by the
// wilco_dtc_supportd daemon (see the API definition at
// mojo/wilco_dtc_supportd.mojom).
class MojoServiceFactory final : public chromeos::wilco_dtc_supportd::mojom::
                                     WilcoDtcSupportdServiceFactory {
 public:
  using MojoBinding = mojo::Binding<
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactory>;
  using MojoBindingPtr = std::unique_ptr<MojoBinding>;
  using WilcoServiceFactory =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactory;
  using BindFactoryCallback =
      base::OnceCallback<MojoBindingPtr(WilcoServiceFactory*, base::ScopedFD)>;

  MojoServiceFactory(MojoGrpcAdapter* grpc_adapter,
                     base::RepeatingClosure shutdown,
                     BindFactoryCallback = CreateBindFactoryCallback());
  MojoServiceFactory(const MojoServiceFactory&) = delete;
  MojoServiceFactory& operator=(const MojoServiceFactory&) = delete;
  virtual ~MojoServiceFactory();

  // Returns the mojo service (can be null, if |Start| has not been called yet).
  MojoService* Get() const;

  // Implements D-Bus call BootstrapMojoConnection().
  // Returns an error message in case an error occurred.
  base::Optional<std::string> BootstrapMojoConnection(
      const base::ScopedFD& mojo_fd);

 private:
  // Initializes the service factory.
  base::Optional<std::string> Start(base::ScopedFD mojo_pipe_fd);

  // Creates the |BindFactoryCallback| to be used in production:
  //
  // The callback binds the given |mojo_service_factory| to the Mojo message
  // pipe that works via the given |mojo_pipe_fd|. The pipe has to contain a
  // valid invitation. On success, returns the created Mojo binding, otherwise
  // returns nullptr.
  //
  // This is a OnceCallback, since Mojo EDK gives no guarantee to support
  // repeated initialization with different parent handles.
  static BindFactoryCallback CreateBindFactoryCallback();

  // Shuts down the self instance after a Mojo fatal error happens.
  void ShutdownDueToMojoError(const std::string& debug_reason);

  // chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactory
  // overrides:
  void GetService(
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceRequest
          service,
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdClientPtr client,
      GetServiceCallback callback) override;

  // Unowned. The mojo_grpc_adapter must outlive this instance.
  MojoGrpcAdapter* grpc_adapter_ = nullptr;
  // To be called in case of an unrecoverable mojo error.
  base::RepeatingClosure shutdown_;

  // OnceCallback to populate the |mojo_service_factory_binding_|.
  BindFactoryCallback bind_factory_callback_;
  // Binding that connects this instance (which is an implementation of
  // chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactory) with
  // the message pipe set up on top of the received file descriptor.
  //
  // Gets created after the BootstrapMojoConnection D-Bus method is called.
  std::unique_ptr<mojo::Binding<WilcoServiceFactory>>
      mojo_service_factory_binding_;
  // Implementation of the Mojo interface exposed by the wilco_dtc_supportd
  // daemon and a proxy that allows sending outgoing Mojo requests.
  //
  // Gets created after the GetService() Mojo method is called.
  std::unique_ptr<MojoService> mojo_service_;
  // Whether binding of the Mojo service was attempted.
  //
  // This flag is needed for detecting repeated Mojo bootstrapping attempts
  // (alternative ways, like checking |mojo_service_factory_binding_|, are
  // unreliable during shutdown).
  bool mojo_service_bind_attempted_ = false;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_MOJO_SERVICE_FACTORY_H_
