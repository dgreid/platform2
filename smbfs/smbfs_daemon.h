// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBFS_SMBFS_DAEMON_H_
#define SMBFS_SMBFS_DAEMON_H_

#include <fuse_lowlevel.h>
#include <sys/types.h>

#include <memory>
#include <string>

#include <base/callback.h>
#include <base/files/file_path.h>
#include <base/files/scoped_temp_dir.h>
#include <base/macros.h>
#include <brillo/daemons/dbus_daemon.h>
#include <mojo/core/embedder/scoped_ipc_support.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "smbfs/smbfs_bootstrap_impl.h"

namespace smbfs {

class Filesystem;
class FuseSession;
class KerberosArtifactSynchronizer;
struct Options;
struct SmbCredential;

class SmbFsDaemon : public brillo::DBusDaemon,
                    public SmbFsBootstrapImpl::Delegate {
 public:
  SmbFsDaemon(fuse_chan* chan, const Options& options);
  ~SmbFsDaemon() override;

 protected:
  // brillo::Daemon overrides.
  int OnInit() override;
  int OnEventLoopStarted() override;

  // SmbFsBootstrapImpl::Delegate overrides.
  void SetupKerberos(mojom::KerberosConfigPtr kerberos_config,
                     base::OnceCallback<void(bool success)> callback) override;
  std::unique_ptr<SmbFilesystem> CreateSmbFilesystem(
      const std::string& share_path,
      std::unique_ptr<SmbCredential> credential,
      bool allow_ntlm) override;
  void OnBootstrapConnectionError() override;

 private:
  // Starts the fuse session using the filesystem |fs|. Returns true if the
  // session is successfully started.
  bool StartFuseSession(std::unique_ptr<Filesystem> fs);

  // Set up libsmbclient configuration files.
  bool SetupSmbConf();

  // Returns the full path to the given kerberos configuration file.
  base::FilePath KerberosConfFilePath(const std::string& file_name);

  // Initialise Mojo IPC system.
  bool InitMojo();

  fuse_chan* chan_;
  const bool use_test_fs_;
  const std::string share_path_;
  const uid_t uid_;
  const gid_t gid_;
  const std::string mojo_id_;
  std::unique_ptr<FuseSession> session_;
  std::unique_ptr<Filesystem> fs_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<KerberosArtifactSynchronizer> kerberos_sync_;

  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  std::unique_ptr<SmbFsBootstrapImpl> bootstrap_impl_;

  DISALLOW_COPY_AND_ASSIGN(SmbFsDaemon);
};

}  // namespace smbfs

#endif  // SMBFS_SMBFS_DAEMON_H_
