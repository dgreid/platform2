// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbfs/smb_filesystem.h"
#include "smbfs/smbfs_impl.h"

#include <utility>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>

namespace smbfs {

SmbFsImpl::SmbFsImpl(base::WeakPtr<SmbFilesystem> fs,
                     mojom::SmbFsRequest request,
                     const base::FilePath& password_file_path)
    : fs_(fs),
      binding_(this, std::move(request)),
      password_file_path_(password_file_path) {
  DCHECK(fs_);
}

SmbFsImpl::~SmbFsImpl() = default;

void SmbFsImpl::RemoveSavedCredentials(
    RemoveSavedCredentialsCallback callback) {
  if (password_file_path_.empty()) {
    std::move(callback).Run(true /* success */);
    return;
  }

  bool success = base::DeleteFile(password_file_path_);
  LOG_IF(WARNING, !success) << "Unable to erase credential file";
  std::move(callback).Run(success);
}

void SmbFsImpl::DeleteRecursively(const base::FilePath& path,
                                  DeleteRecursivelyCallback callback) {
  CHECK(path.IsAbsolute());
  CHECK(!path.ReferencesParent());

  fs_->DeleteRecursively(path, std::move(callback));
}

}  // namespace smbfs
