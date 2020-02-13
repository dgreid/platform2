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
#include <mojo/public/cpp/system/platform_handle.h>

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

mojom::MountError ConnectErrorToMountError(SmbFilesystem::ConnectError error) {
  switch (error) {
    case SmbFilesystem::ConnectError::kNotFound:
      return mojom::MountError::kNotFound;
    case SmbFilesystem::ConnectError::kAccessDenied:
      return mojom::MountError::kAccessDenied;
    case SmbFilesystem::ConnectError::kSmb1Unsupported:
      return mojom::MountError::kInvalidProtocol;
    default:
      return mojom::MountError::kUnknown;
  }
}

std::unique_ptr<password_provider::Password> MakePasswordFromMojoHandle(
    mojo::ScopedHandle handle, int32_t length) {
  base::ScopedFD fd = mojo::UnwrapPlatformHandle(std::move(handle)).TakeFD();
  return password_provider::Password::CreateFromFileDescriptor(fd.get(),
                                                               length);
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
    auto fs = std::make_unique<SmbFilesystem>(share_path_, uid_, gid_, nullptr);
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

void SmbFsDaemon::MountShare(mojom::MountOptionsPtr options,
                             mojom::SmbFsDelegatePtr delegate,
                             const MountShareCallback& callback) {
  if (session_) {
    LOG(ERROR) << "smbfs already connected to a share";
    callback.Run(mojom::MountError::kUnknown, nullptr);
    return;
  }

  if (options->share_path.find("smb://") != 0) {
    // TODO(amistry): More extensive URL validation.
    LOG(ERROR) << "Invalid share path: " << options->share_path;
    callback.Run(mojom::MountError::kInvalidUrl, nullptr);
    return;
  }

  std::unique_ptr<SmbCredential> credential = std::make_unique<SmbCredential>(
      options->workgroup, options->username, nullptr);
  if (options->kerberos_config) {
    SetupKerberos(
        std::move(options->kerberos_config),
        base::BindOnce(&SmbFsDaemon::OnCredentialsSetup, base::Unretained(this),
                       std::move(options), std::move(delegate), callback,
                       std::move(credential)));
    return;
  }

  if (options->password) {
    credential->password = MakePasswordFromMojoHandle(
        std::move(options->password->fd), options->password->length);
  }

  OnCredentialsSetup(std::move(options), std::move(delegate), callback,
                     std::move(credential), true /* setup_success */);
}

void SmbFsDaemon::OnCredentialsSetup(mojom::MountOptionsPtr options,
                                     mojom::SmbFsDelegatePtr delegate,
                                     const MountShareCallback& callback,
                                     std::unique_ptr<SmbCredential> credential,
                                     bool setup_success) {
  if (!setup_success) {
    callback.Run(mojom::MountError::kUnknown, nullptr);
    return;
  }

  auto fs = std::make_unique<SmbFilesystem>(options->share_path, uid_, gid_,
                                            std::move(credential));
  // Don't use the resolved address if Kerberos is set up. Kerberos requires the
  // full hostname to obtain auth tickets.
  if (options->resolved_host && !kerberos_sync_) {
    if (options->resolved_host->address_bytes.size() != 4) {
      LOG(ERROR) << "Invalid IP address size: "
                 << options->resolved_host->address_bytes.size();
      callback.Run(mojom::MountError::kInvalidOptions, nullptr);
      return;
    }
    fs->SetResolvedAddress(options->resolved_host->address_bytes);
  }
  if (!options->skip_connect) {
    SmbFilesystem::ConnectError error = fs->EnsureConnected();
    if (error != SmbFilesystem::ConnectError::kOk) {
      LOG(ERROR) << "Unable to connect to SMB share " << options->share_path
                 << ": " << error;
      callback.Run(ConnectErrorToMountError(error), nullptr);
      return;
    }
  }

  mojom::SmbFsPtr smbfs_ptr;
  fs->SetSmbFsImpl(
      std::make_unique<SmbFsImpl>(fs.get(), mojo::MakeRequest(&smbfs_ptr)));

  if (!StartFuseSession(std::move(fs))) {
    callback.Run(mojom::MountError::kUnknown, nullptr);
    return;
  }

  delegate_ = std::move(delegate);
  callback.Run(mojom::MountError::kOk, std::move(smbfs_ptr));
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

  bootstrap_binding_.Bind(mojom::SmbFsBootstrapRequest(
      invitation.ExtractMessagePipe(mojom::kBootstrapPipeName)));
  bootstrap_binding_.set_connection_error_handler(
      base::BindOnce(&SmbFsDaemon::OnConnectionError, base::Unretained(this)));

  return true;
}

void SmbFsDaemon::OnConnectionError() {
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

}  // namespace smbfs
