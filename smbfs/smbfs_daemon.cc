// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbfs/smbfs_daemon.h"

#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <utility>

#include <base/bind.h>
#include <base/files/file_util.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/message_loops/message_loop.h>
#include <brillo/daemons/dbus_daemon.h>
#include <chromeos/dbus/service_constants.h>
#include <mojo/core/embedder/embedder.h>
#include <mojo/public/cpp/platform/platform_channel.h>
#include <mojo/public/cpp/system/invitation.h>

#include "smbfs/authpolicy_client.h"
#include "smbfs/dbus-proxies.h"
#include "smbfs/fuse_session.h"
#include "smbfs/kerberos_artifact_synchronizer.h"
#include "smbfs/kerberos_client.h"
#include "smbfs/smb_credential.h"
#include "smbfs/smb_filesystem.h"
#include "smbfs/smbfs.h"
#include "smbfs/smbfs_impl.h"
#include "smbfs/test_filesystem.h"

namespace smbfs {
namespace {

constexpr char kSmbConfDir[] = ".smb";
constexpr char kSmbConfFile[] = "smb.conf";
constexpr char kKerberosConfDir[] = ".krb";
constexpr char kKrb5ConfFile[] = "krb5.conf";
constexpr char kCCacheFile[] = "ccache";
constexpr char kKrbTraceFile[] = "krb_trace.txt";

constexpr char kSmbConfData[] = R"(
[global]
  client min protocol = SMB2
  client max protocol = SMB3
  security = user
)";

bool CreateDirectoryAndLog(const base::FilePath& path) {
  CHECK(path.IsAbsolute());
  base::File::Error error;
  bool success = base::CreateDirectoryAndGetError(path, &error);
  LOG_IF(ERROR, !success) << "Failed to create directory " << path.value()
                          << ": " << base::File::ErrorToString(error);
  return success;
}

}  // namespace

SmbFsDaemon::SmbFsDaemon(fuse_chan* chan, const Options& options)
    : chan_(chan),
      use_test_fs_(options.use_test),
      share_path_(options.share_path),
      uid_(options.uid ? options.uid : getuid()),
      gid_(options.gid ? options.gid : getgid()),
      mojo_id_(options.mojo_id ? options.mojo_id : "") {
  DCHECK(chan_);
}

SmbFsDaemon::~SmbFsDaemon() = default;

int SmbFsDaemon::OnInit() {
  int ret = brillo::DBusDaemon::OnInit();
  if (ret != EX_OK) {
    return ret;
  }

  if (!SetupSmbConf()) {
    return EX_SOFTWARE;
  }

  if (!share_path_.empty()) {
    SmbFilesystem::Options options;
    options.share_path = share_path_;
    options.uid = uid_;
    options.gid = gid_;
    options.allow_ntlm = true;
    std::unique_ptr<SmbFilesystem> fs =
        std::make_unique<SmbFilesystem>(std::move(options));
    SmbFilesystem::ConnectError error = fs->EnsureConnected();
    if (error != SmbFilesystem::ConnectError::kOk) {
      LOG(ERROR) << "Unable to connect to SMB filesystem: " << error;
      return EX_SOFTWARE;
    }
    fs_ = std::move(fs);
  }

  return EX_OK;
}

int SmbFsDaemon::OnEventLoopStarted() {
  int ret = brillo::DBusDaemon::OnEventLoopStarted();
  if (ret != EX_OK) {
    return ret;
  }

  std::unique_ptr<Filesystem> fs;
  if (use_test_fs_) {
    fs = std::make_unique<TestFilesystem>(uid_, gid_);
  } else if (fs_) {
    fs = std::move(fs_);
  } else if (!mojo_id_.empty()) {
    if (!InitMojo()) {
      return EX_SOFTWARE;
    }
    return EX_OK;
  } else {
    NOTREACHED();
  }

  if (!StartFuseSession(std::move(fs))) {
    return EX_SOFTWARE;
  }

  return EX_OK;
}

bool SmbFsDaemon::StartFuseSession(std::unique_ptr<Filesystem> fs) {
  DCHECK(!session_);
  DCHECK(chan_);

  session_ = std::make_unique<FuseSession>(std::move(fs), chan_);
  chan_ = nullptr;
  return session_->Start(base::BindOnce(&Daemon::Quit, base::Unretained(this)));
}

base::FilePath SmbFsDaemon::KerberosConfFilePath(const std::string& file_name) {
  DCHECK(temp_dir_.IsValid());
  return temp_dir_.GetPath().Append(kKerberosConfDir).Append(file_name);
}

bool SmbFsDaemon::SetupSmbConf() {
  // Create a temporary "home" directory where configuration files used by
  // libsmbclient will be placed.
  CHECK(temp_dir_.CreateUniqueTempDir());
  PCHECK(setenv("HOME", temp_dir_.GetPath().value().c_str(),
                1 /* overwrite */) == 0);
  PCHECK(setenv("KRB5_CONFIG",
                KerberosConfFilePath(kKrb5ConfFile).value().c_str(),
                1 /* overwrite */) == 0);
  PCHECK(setenv("KRB5CCNAME", KerberosConfFilePath(kCCacheFile).value().c_str(),
                1 /* overwrite */) == 0);
  PCHECK(setenv("KRB5_TRACE",
                KerberosConfFilePath(kKrbTraceFile).value().c_str(),
                1 /* overwrite */) == 0);
  LOG(INFO) << "Storing SMB configuration files in: "
            << temp_dir_.GetPath().value();

  bool success =
      CreateDirectoryAndLog(temp_dir_.GetPath().Append(kSmbConfDir)) &&
      CreateDirectoryAndLog(temp_dir_.GetPath().Append(kKerberosConfDir));
  if (!success) {
    return false;
  }

  // TODO(amistry): Replace with smbc_setOptionProtocols() when Samba is
  // updated.
  return base::WriteFile(
             temp_dir_.GetPath().Append(kSmbConfDir).Append(kSmbConfFile),
             kSmbConfData, sizeof(kSmbConfData)) == sizeof(kSmbConfData);
}

bool SmbFsDaemon::InitMojo() {
  LOG(INFO) << "Boostrapping connection using Mojo";

  mojo::core::Init();
  ipc_support_ = std::make_unique<mojo::core::ScopedIPCSupport>(
      base::ThreadTaskRunnerHandle::Get(),
      mojo::core::ScopedIPCSupport::ShutdownPolicy::FAST);

  mojo::PlatformChannel channel;

  // The SmbFs service is hosted in the browser, so is expected to
  // already be running when this starts. If this is not the case, the D-Bus
  // IPC below will fail and this process will shut down.
  org::chromium::SmbFsProxy dbus_proxy(bus_, kSmbFsServiceName);
  brillo::ErrorPtr error;
  if (!dbus_proxy.OpenIpcChannel(
          mojo_id_, channel.TakeRemoteEndpoint().TakePlatformHandle().TakeFD(),
          &error)) {
    return false;
  }

  mojo::IncomingInvitation invitation =
      mojo::IncomingInvitation::Accept(channel.TakeLocalEndpoint());
  bootstrap_impl_ = std::make_unique<SmbFsBootstrapImpl>(
      mojom::SmbFsBootstrapRequest(
          invitation.ExtractMessagePipe(mojom::kBootstrapPipeName)),
      this);
  bootstrap_impl_->Start(base::BindOnce(
      [](SmbFsDaemon* daemon, std::unique_ptr<SmbFilesystem> fs) {
        // Trivial adapter lambda due to argument type (Filesystem instead of
        // SmbFilesystem).
        CHECK(daemon->StartFuseSession(std::move(fs)));
      },
      this));

  return true;
}

void SmbFsDaemon::OnBootstrapConnectionError() {
  if (session_) {
    // Do nothing because the session is running.
    return;
  }

  LOG(ERROR) << "Connection error during Mojo bootstrap. Exiting.";
  QuitWithExitCode(EX_SOFTWARE);
}

void SmbFsDaemon::SetupKerberos(
    mojom::KerberosConfigPtr kerberos_config,
    base::OnceCallback<void(bool success)> callback) {
  DCHECK(!kerberos_sync_);
  DCHECK(kerberos_config);

  std::unique_ptr<KerberosArtifactClientInterface> client;
  switch (kerberos_config->source) {
    case mojom::KerberosConfig::Source::kActiveDirectory:
      client = std::make_unique<AuthPolicyClient>(bus_);
      break;
    case mojom::KerberosConfig::Source::kKerberos:
      client = std::make_unique<KerberosClient>(bus_);
      break;
  }

  DCHECK(client);
  kerberos_sync_ = std::make_unique<KerberosArtifactSynchronizer>(
      KerberosConfFilePath(kKrb5ConfFile), KerberosConfFilePath(kCCacheFile),
      kerberos_config->identity, std::move(client));
  kerberos_sync_->SetupKerberos(std::move(callback));
}

std::unique_ptr<SmbFilesystem> SmbFsDaemon::CreateSmbFilesystem(
    const std::string& share_path,
    std::unique_ptr<SmbCredential> credentials,
    bool allow_ntlm) {
  SmbFilesystem::Options options;
  options.share_path = share_path;
  options.uid = uid_;
  options.gid = gid_;
  options.credentials = std::move(credentials);
  options.allow_ntlm = allow_ntlm;
  return std::make_unique<SmbFilesystem>(std::move(options));
}

}  // namespace smbfs
