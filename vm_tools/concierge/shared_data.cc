// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vm_tools/concierge/shared_data.h"

#include <base/files/file_util.h>

#include "vm_tools/common/naming.h"

namespace vm_tools {
namespace concierge {

base::Optional<base::FilePath> GetFilePathFromName(
    const std::string& cryptohome_id,
    const std::string& vm_name,
    StorageLocation storage_location,
    const std::string& extension,
    bool create_parent_dir) {
  if (!base::ContainsOnlyChars(cryptohome_id, kValidCryptoHomeCharacters)) {
    LOG(ERROR) << "Invalid cryptohome_id specified";
    return base::nullopt;
  }
  // Encode the given disk name to ensure it only has valid characters.
  std::string encoded_name = GetEncodedName(vm_name);

  base::FilePath storage_dir = base::FilePath(kCryptohomeRoot);
  switch (storage_location) {
    case STORAGE_CRYPTOHOME_ROOT: {
      storage_dir = storage_dir.Append(kCrosvmDir);
      break;
    }
    case STORAGE_CRYPTOHOME_PLUGINVM: {
      storage_dir = storage_dir.Append(kPluginVmDir);
      break;
    }
    default: {
      LOG(ERROR) << "Unknown storage location type";
      return base::nullopt;
    }
  }
  storage_dir = storage_dir.Append(cryptohome_id);

  if (!base::DirectoryExists(storage_dir)) {
    if (!create_parent_dir) {
      return base::nullopt;
    }
    base::File::Error dir_error;

    if (!base::CreateDirectoryAndGetError(storage_dir, &dir_error)) {
      LOG(ERROR) << "Failed to create storage directory " << storage_dir << ": "
                 << base::File::ErrorToString(dir_error);
      return base::nullopt;
    }
  }
  return storage_dir.Append(encoded_name).AddExtension(extension);
}

bool GetPluginDirectory(const base::FilePath& prefix,
                        const std::string& extension,
                        const std::string& vm_id,
                        bool create,
                        base::FilePath* path_out) {
  std::string dirname = GetEncodedName(vm_id);

  base::FilePath path = prefix.Append(dirname).AddExtension(extension);
  if (create && !base::DirectoryExists(path)) {
    base::File::Error dir_error;
    if (!base::CreateDirectoryAndGetError(path, &dir_error)) {
      LOG(ERROR) << "Failed to create plugin directory " << path.value() << ": "
                 << base::File::ErrorToString(dir_error);
      return false;
    }
  }

  *path_out = path;
  return true;
}

bool GetPluginIsoDirectory(const std::string& vm_id,
                           const std::string& cryptohome_id,
                           bool create,
                           base::FilePath* path_out) {
  return GetPluginDirectory(base::FilePath(kCryptohomeRoot)
                                .Append(kPluginVmDir)
                                .Append(cryptohome_id),
                            "iso", vm_id, create, path_out);
}

Future<bool> KillCrosvmProcess(std::weak_ptr<SigchldHandler> weak_handler,
                               uint32_t pid,
                               uint32_t cid,
                               Future<bool> future) {
  return future
      .Then(base::BindOnce(
          [](std::weak_ptr<SigchldHandler> weak_handler, uint32_t pid,
             uint32_t cid, bool exited) {
            if (exited) {
              return Reject<Future<bool>>();
            }

            LOG(WARNING) << "Failed to stop VM " << cid << " via crosvm socket";

            if (kill(pid, SIGTERM) < 0) {
              if (errno == ESRCH) {
                // Process is already gone.
                return Reject<Future<bool>>();
              } else {
                LOG(ERROR) << "Unable to send SIGTERM to process " << pid;
                return Resolve(ResolvedFuture(false));
              }
            }

            return Resolve(WatchSigchld(weak_handler, pid, kChildExitTimeout));
          },
          weak_handler, pid, cid))
      .Flatten()
      .Then(base::BindOnce(
          [](std::weak_ptr<SigchldHandler> weak_handler, uint32_t pid,
             uint32_t cid, bool exited) {
            if (exited) {
              return Reject<Future<bool>>();
            }

            LOG(WARNING) << "Failed to kill VM " << cid << " with SIGTERM";

            // Kill it with fire.
            if (kill(pid, SIGKILL) < 0) {
              if (errno == ESRCH) {
                // Process is already gone.
                return Reject<Future<bool>>();
              } else {
                PLOG(ERROR) << "Unable to send SIGKILL to process " << pid;
                return Resolve(ResolvedFuture(false));
              }
            }

            return Resolve(WatchSigchld(weak_handler, pid, kChildExitTimeout));
          },
          weak_handler, pid, cid))
      .Flatten()
      .Then(base::BindOnce(
          [](uint32_t vsock_cid, bool exited) {
            if (exited) {
              return Reject<bool>();
            }

            LOG(ERROR) << "Failed to kill VM " << vsock_cid << " with SIGKILL";
            return Resolve<bool>(false);
          },
          cid))
      .OnReject(base::BindOnce([]() {
        // We rejected when exit = true. This pattern is needed to avoid
        // code pyramid
        return Resolve(true);
      }));
}

Future<bool> WatchSigchld(std::weak_ptr<SigchldHandler> weak_handler,
                          pid_t pid,
                          base::TimeDelta timeout) {
  std::shared_ptr<SigchldHandler> handler = weak_handler.lock();
  if (!handler) {
    LOG(WARNING) << "Service has already been destroyed";
    return ResolvedFuture(false);
  }
  return handler->GetFutureForProc(pid, timeout);
}

bool CancelWatchSigchld(std::weak_ptr<SigchldHandler> weak_handler, pid_t pid) {
  std::shared_ptr<SigchldHandler> handler = weak_handler.lock();
  if (!handler) {
    LOG(WARNING) << "Service has already been destroyed";
    return false;
  }
  return handler->Cancel(pid);
}

}  // namespace concierge
}  // namespace vm_tools
