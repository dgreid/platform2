// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBFS_SMBFS_BOOTSTRAP_IMPL_H_
#define SMBFS_SMBFS_BOOTSTRAP_IMPL_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/callback.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "smbfs/mojom/smbfs.mojom.h"

namespace smbfs {

class Filesystem;
class SmbFilesystem;
struct SmbCredential;

// Implements mojom::SmbFsBootstrap to mount an SMB share.
class SmbFsBootstrapImpl : public mojom::SmbFsBootstrap {
 public:
  class Delegate {
   public:
    // Sets up Kerberos authentication.
    virtual void SetupKerberos(
        mojom::KerberosConfigPtr kerberos_config,
        base::OnceCallback<void(bool success)> callback) = 0;

    // Creates a new SmbFilesystem. Must always succeed and return a new
    // SmbFilesystem.
    virtual std::unique_ptr<SmbFilesystem> CreateSmbFilesystem(
        const std::string& share_path,
        std::unique_ptr<SmbCredential> credential,
        bool allow_ntlm) = 0;

    // Mojo connection error handler.
    virtual void OnBootstrapConnectionError() = 0;
  };

  using BootstrapCompleteCallback =
      base::OnceCallback<void(std::unique_ptr<SmbFilesystem> fs)>;

  SmbFsBootstrapImpl(mojom::SmbFsBootstrapRequest request, Delegate* delegate);
  ~SmbFsBootstrapImpl() override;

  // Start the bootstrap process and run |callback| when successfully completed.
  void Start(BootstrapCompleteCallback callback);

 private:
  // mojom::SmbFsBootstrap overrides.
  void MountShare(mojom::MountOptionsPtr options,
                  mojom::SmbFsDelegatePtr smbfs_delegate,
                  const MountShareCallback& callback) override;

  // Callback to continue MountShare after setting up credentials
  // (username/password, or kerberos).
  void OnCredentialsSetup(mojom::MountOptionsPtr options,
                          mojom::SmbFsDelegatePtr smbfs_delegate,
                          const MountShareCallback& callback,
                          std::unique_ptr<SmbCredential> credential,
                          bool use_kerberos,
                          bool setup_success);

  mojo::Binding<mojom::SmbFsBootstrap> binding_;
  base::OnceClosure disconnect_callback_;

  Delegate* const delegate_;
  BootstrapCompleteCallback completion_callback_;

  DISALLOW_COPY_AND_ASSIGN(SmbFsBootstrapImpl);
};

}  // namespace smbfs

#endif  // SMBFS_SMBFS_BOOTSTRAP_IMPL_H_
