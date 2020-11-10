// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBPROVIDER_KERBEROS_ARTIFACT_CLIENT_INTERFACE_H_
#define SMBPROVIDER_KERBEROS_ARTIFACT_CLIENT_INTERFACE_H_

#include <string>

#include <dbus/object_proxy.h>

namespace smbprovider {

class KerberosArtifactClientInterface {
 public:
  using GetUserKerberosFilesCallback =
      base::Callback<void(bool success,
                          const std::string& krb5_ccache,
                          const std::string& krb5_conf)>;

  virtual ~KerberosArtifactClientInterface() = default;

  // Gets Kerberos files for the user determined by |account_identifier|.
  // If authpolicyd or kerberosd has Kerberos files for the user specified by
  // |account_identifier| it sends them in response: credential cache and krb5
  // config files. For authpolicyd expected |account_identifier| is object guid,
  // while for kerberosd it is principal name.
  virtual void GetUserKerberosFiles(const std::string& account_identifier,
                                    GetUserKerberosFilesCallback callback) = 0;

  // Connects callbacks to OnKerberosFilesChanged D-Bus signal sent by
  // authpolicyd.
  virtual void ConnectToKerberosFilesChangedSignal(
      dbus::ObjectProxy::SignalCallback signal_callback,
      dbus::ObjectProxy::OnConnectedCallback on_connected_callback) = 0;

 protected:
  KerberosArtifactClientInterface() = default;
  KerberosArtifactClientInterface(const KerberosArtifactClientInterface&) =
      delete;
  KerberosArtifactClientInterface& operator=(
      const KerberosArtifactClientInterface&) = delete;
};

}  // namespace smbprovider

#endif  // SMBPROVIDER_KERBEROS_ARTIFACT_CLIENT_INTERFACE_H_
