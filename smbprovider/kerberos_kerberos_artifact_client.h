// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBPROVIDER_KERBEROS_KERBEROS_ARTIFACT_CLIENT_H_
#define SMBPROVIDER_KERBEROS_KERBEROS_ARTIFACT_CLIENT_H_

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/memory/weak_ptr.h>
#include <dbus/bus.h>
#include <dbus/object_proxy.h>

#include "smbprovider/kerberos_artifact_client_interface.h"

namespace smbprovider {

// KerberosKerberosArtifactClient is used to communicate with the
// org.chromium.Kerberos service.
class KerberosKerberosArtifactClient : public KerberosArtifactClientInterface {
 public:
  explicit KerberosKerberosArtifactClient(scoped_refptr<dbus::Bus> bus);
  KerberosKerberosArtifactClient(const KerberosKerberosArtifactClient&) =
      delete;
  KerberosKerberosArtifactClient& operator=(
      const KerberosKerberosArtifactClient&) = delete;

  // KerberosArtifactClientInterface overrides.
  void GetUserKerberosFiles(const std::string& principal_name,
                            GetUserKerberosFilesCallback callback) override;
  void ConnectToKerberosFilesChangedSignal(
      dbus::ObjectProxy::SignalCallback signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) override;

 private:
  void HandleGetUserKeberosFiles(GetUserKerberosFilesCallback callback,
                                 dbus::Response* response);

  dbus::ObjectProxy* kerberos_object_proxy_ = nullptr;
  base::WeakPtrFactory<KerberosKerberosArtifactClient> weak_ptr_factory_{this};
};

}  // namespace smbprovider

#endif  // SMBPROVIDER_KERBEROS_KERBEROS_ARTIFACT_CLIENT_H_
