// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBPROVIDER_AUTHPOLICY_KERBEROS_ARTIFACT_CLIENT_H_
#define SMBPROVIDER_AUTHPOLICY_KERBEROS_ARTIFACT_CLIENT_H_

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>
#include <brillo/dbus/dbus_object.h>
#include <dbus/object_proxy.h>

#include "smbprovider/kerberos_artifact_client_interface.h"

namespace smbprovider {

// AuthPolicyKerberosArtifactClient is used to communicate with the
// org.chromium.AuthPolicy service.
class AuthPolicyKerberosArtifactClient
    : public KerberosArtifactClientInterface {
 public:
  explicit AuthPolicyKerberosArtifactClient(scoped_refptr<dbus::Bus> bus);
  AuthPolicyKerberosArtifactClient(const AuthPolicyKerberosArtifactClient&) =
      delete;
  AuthPolicyKerberosArtifactClient& operator=(
      const AuthPolicyKerberosArtifactClient&) = delete;

  // KerberosArtifactClientInterface overrides.
  void GetUserKerberosFiles(const std::string& object_guid,
                            GetUserKerberosFilesCallback callback) override;
  void ConnectToKerberosFilesChangedSignal(
      dbus::ObjectProxy::SignalCallback signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override;

 private:
  void HandleGetUserKeberosFiles(GetUserKerberosFilesCallback callback,
                                 dbus::Response* response);

  dbus::ObjectProxy* authpolicy_object_proxy_ = nullptr;
  base::WeakPtrFactory<AuthPolicyKerberosArtifactClient> weak_ptr_factory_{
      this};
};

}  // namespace smbprovider

#endif  // SMBPROVIDER_AUTHPOLICY_KERBEROS_ARTIFACT_CLIENT_H_
