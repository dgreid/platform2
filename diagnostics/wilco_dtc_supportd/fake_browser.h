// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_BROWSER_H_
#define DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_BROWSER_H_

#include <string>

#include <base/callback.h>
#include <base/macros.h>
#include <dbus/mock_exported_object.h>
#include <gmock/gmock.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "diagnostics/common/mojo_test_utils.h"
#include "diagnostics/wilco_dtc_supportd/mock_mojo_client.h"
#include "mojo/wilco_dtc_supportd.mojom.h"

namespace diagnostics {

// Helper class that allows to test communication between the browser and the
// tested code of the wilco_dtc_supportd daemon.
class FakeBrowser final {
 public:
  using DBusMethodCallCallback = dbus::ExportedObject::MethodCallCallback;

  using MojomWilcoDtcSupportdClient =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdClient;
  using MojomWilcoDtcSupportdClientPtr =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdClientPtr;
  using MojomWilcoDtcSupportdServicePtr =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServicePtr;
  using MojomWilcoDtcSupportdServiceFactoryPtr =
      chromeos::wilco_dtc_supportd::mojom::WilcoDtcSupportdServiceFactoryPtr;

  // |wilco_dtc_supportd_service_factory_ptr| is a pointer to the tested
  // WilcoDtcSupportdServiceFactory instance.
  // |bootstrap_mojo_connection_dbus_method| is the callback that the tested
  // code exposed as the BootstrapMojoConnection D-Bus method.
  FakeBrowser(MojomWilcoDtcSupportdServiceFactoryPtr*
                  wilco_dtc_supportd_service_factory_ptr,
              DBusMethodCallCallback bootstrap_mojo_connection_dbus_method);
  FakeBrowser(const FakeBrowser&) = delete;
  FakeBrowser& operator=(const FakeBrowser&) = delete;

  ~FakeBrowser();

  // Returns a mock WilcoDtcSupportdClient instance, whose methods are invoked
  // when FakeBrowser receives incoming Mojo calls from the tested code.
  testing::StrictMock<MockMojoClient>* wilco_dtc_supportd_client() {
    return &wilco_dtc_supportd_client_;
  }

  // Call the BootstrapMojoConnection D-Bus method. Returns whether the D-Bus
  // call returned success.
  // |fake_mojo_fd_generator| is the fake file descriptor generator.
  // |bootstrap_mojo_connection_callback| is called when the boostrapping of the
  // mojo connection succeeds or fails.
  //
  // It's not allowed to call this method again after a successful completion.
  bool BootstrapMojoConnection(
      FakeMojoFdGenerator* fake_mojo_fd_generator,
      const base::Closure& bootstrap_mojo_connection_callback);

  // Call the |SendUiMessageToWilcoDtc| Mojo method
  // on wilco_dtc_supportd daemon, which will call the |HandleMessageFromUi|
  // gRPC method on wilco_dtc.
  //
  // It simulates message sent from diagnostics UI extension to wilco_dtc.
  //
  // Returns false when we were not able to copy |json_message| into shared
  // buffer.
  //
  // Must be called only after a successful invocation of
  // BootstrapMojoConnection().
  bool SendUiMessageToWilcoDtc(
      const std::string& json_message,
      const base::Callback<void(mojo::ScopedHandle)>& callback);

  // Call the |NotifyConfigurationDataChanged| Mojo method on wilco_dtc_supportd
  // daemon, which will call the corresponding gRPC method on wilco_dtc.
  //
  // It simulates the notification sent from the browser to wilco_dtc.
  //
  // Must be called only after a successful invocation of
  // BootstrapMojoConnection().
  void NotifyConfigurationDataChanged();

 private:
  // Calls |bootstrap_mojo_connection_dbus_method_| with a fake file descriptor.
  // Returns whether the method call succeeded (it's expected to happen
  // synchronously).
  bool CallBootstrapMojoConnectionDBusMethod(
      FakeMojoFdGenerator* fake_mojo_fd_generator);

  // Calls GetService() Mojo method on
  // |wilco_dtc_supportd_service_factory_ptr_|, initializes
  // |wilco_dtc_supportd_service_ptr_| so that it points to the tested service,
  // registers |wilco_dtc_supportd_client_| to handle incoming Mojo requests.
  // |get_service_mojo_method_callback| is called when the full-duplex Mojo
  // communication with the tested Mojo service is established.
  void CallGetServiceMojoMethod(
      const base::Closure& get_service_mojo_method_callback);

  // Unowned. Points to the tested WilcoDtcSupportdServiceFactory instance.
  MojomWilcoDtcSupportdServiceFactoryPtr* const
      wilco_dtc_supportd_service_factory_ptr_;
  // Fake substitute for the BootstrapMojoConnection() D-Bus method.
  DBusMethodCallCallback bootstrap_mojo_connection_dbus_method_;

  // Mock WilcoDtcSupportdClient instance. After an invocation of
  // CallGetServiceMojoMethod() it becomes registered to receive incoming Mojo
  // requests from the tested code.
  testing::StrictMock<MockMojoClient> wilco_dtc_supportd_client_;
  // Mojo binding that is associated with |wilco_dtc_supportd_client_|.
  mojo::Binding<MojomWilcoDtcSupportdClient> wilco_dtc_supportd_client_binding_;

  // Mojo interface pointer to the WilcoDtcSupportdService service exposed by
  // the tested code. Gets initialized after a call to
  // CallGetServiceMojoMethod().
  MojomWilcoDtcSupportdServicePtr wilco_dtc_supportd_service_ptr_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_WILCO_DTC_SUPPORTD_FAKE_BROWSER_H_
