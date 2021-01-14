// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/archive_mounter.h"

#include <utility>

#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <brillo/scoped_mount_namespace.h>

#include "cros-disks/quote.h"

namespace cros_disks {

namespace {
constexpr char kOptionPassword[] = "password";
}  // namespace

ArchiveMounter::ArchiveMounter(
    const Platform* platform,
    brillo::ProcessReaper* process_reaper,
    std::string archive_type,
    Metrics* metrics,
    std::string metrics_name,
    std::vector<int> password_needed_exit_codes,
    std::unique_ptr<SandboxedProcessFactory> sandbox_factory)
    : FUSEMounter(
          platform, process_reaper, archive_type + "fs", {.read_only = true}),
      archive_type_(archive_type),
      extension_("." + archive_type),
      metrics_(metrics),
      metrics_name_(std::move(metrics_name)),
      password_needed_exit_codes_(std::move(password_needed_exit_codes)),
      sandbox_factory_(std::move(sandbox_factory)) {}

ArchiveMounter::~ArchiveMounter() = default;

bool ArchiveMounter::CanMount(const std::string& source,
                              const std::vector<std::string>& /*params*/,
                              base::FilePath* suggested_dir_name) const {
  base::FilePath path(source);
  if (path.IsAbsolute() &&
      base::CompareCaseInsensitiveASCII(path.Extension(), extension_) == 0) {
    *suggested_dir_name = path.BaseName();
    return true;
  }
  return false;
}

MountErrorType ArchiveMounter::InterpretReturnCode(int return_code) const {
  if (metrics_ && !metrics_name_.empty())
    metrics_->RecordFuseMounterErrorCode(metrics_name_, return_code);

  if (base::Contains(password_needed_exit_codes_, return_code))
    return MOUNT_ERROR_NEED_PASSWORD;
  return FUSEMounter::InterpretReturnCode(return_code);
}

std::unique_ptr<SandboxedProcess> ArchiveMounter::PrepareSandbox(
    const std::string& source,
    const base::FilePath& /*target_path*/,
    std::vector<std::string> params,
    MountErrorType* error) const {
  metrics_->RecordArchiveType(archive_type_);

  base::FilePath path(source);
  if (!path.IsAbsolute() || path.ReferencesParent()) {
    LOG(ERROR) << "Invalid archive path " << redact(path);
    *error = MOUNT_ERROR_INVALID_ARGUMENT;
    return nullptr;
  }

  auto sandbox = sandbox_factory_->CreateSandboxedProcess();

  std::unique_ptr<brillo::ScopedMountNamespace> mount_ns;
  if (!platform()->PathExists(path.value())) {
    // Try to locate the file in Chrome's mount namespace.
    mount_ns = brillo::ScopedMountNamespace::CreateFromPath(
        base::FilePath(kChromeNamespace));
    if (!mount_ns) {
      PLOG(ERROR) << "Cannot find archive " << redact(path)
                  << " in mount namespace " << quote(kChromeNamespace);

      // TODO(dats): These probably should be MOUNT_ERROR_INVALID_DEVICE_PATH or
      //             something like that, but tast tests expect
      //             MOUNT_ERROR_MOUNT_PROGRAM_FAILED.
      *error = MOUNT_ERROR_MOUNT_PROGRAM_FAILED;
      return nullptr;
    }
    if (!platform()->PathExists(path.value())) {
      PLOG(ERROR) << "Cannot find archive " << redact(path);
      *error = MOUNT_ERROR_MOUNT_PROGRAM_FAILED;
      return nullptr;
    }
  }

  // Archives are typically under /home, /media or /run. To bind-mount the
  // source those directories must be writable, but by default only /run is.
  for (const char* const dir : {"/home", "/media"}) {
    if (!sandbox->Mount("tmpfs", dir, "tmpfs", "mode=0755,size=1M")) {
      LOG(ERROR) << "Cannot mount " << quote(dir);
      *error = MOUNT_ERROR_INTERNAL;
      return nullptr;
    }
  }

  // Is the process "password-aware"?
  if (!password_needed_exit_codes_.empty()) {
    std::string password;
    if (GetParamValue(params, kOptionPassword, &password)) {
      sandbox->SetStdIn(password);
    }
  }

  *error = FormatInvocationCommand(path, std::move(params), sandbox.get());
  if (*error != MOUNT_ERROR_NONE) {
    return nullptr;
  }

  if (mount_ns) {
    // Sandbox will need to enter Chrome's namespace too to access files.
    mount_ns.reset();
    sandbox->EnterExistingMountNamespace(kChromeNamespace);
  }

  return sandbox;
}

MountErrorType ArchiveMounter::FormatInvocationCommand(
    const base::FilePath& archive,
    std::vector<std::string> /*params*/,
    SandboxedProcess* sandbox) const {
  // Make the source available in the sandbox.
  if (!sandbox->BindMount(archive.value(), archive.value(),
                          /* writeable= */ false,
                          /* recursive= */ false)) {
    LOG(ERROR) << "Cannot bind-mount archive " << redact(archive);
    return MOUNT_ERROR_INTERNAL;
  }

  std::vector<std::string> opts = {
      "ro", "umask=0222", base::StringPrintf("uid=%d", kChronosUID),
      base::StringPrintf("gid=%d", kChronosAccessGID)};

  sandbox->AddArgument("-o");
  sandbox->AddArgument(base::JoinString(opts, ","));
  sandbox->AddArgument(archive.value());

  return MOUNT_ERROR_NONE;
}

}  // namespace cros_disks
