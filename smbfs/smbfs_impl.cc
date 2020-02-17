// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "smbfs/smbfs_impl.h"

#include <utility>

#include <base/logging.h>

namespace smbfs {

SmbFsImpl::SmbFsImpl(SmbFilesystem* fs, mojom::SmbFsRequest request)
    : fs_(fs), binding_(this, std::move(request)) {
  DCHECK(fs_);
}

SmbFsImpl::~SmbFsImpl() = default;

}  // namespace smbfs
