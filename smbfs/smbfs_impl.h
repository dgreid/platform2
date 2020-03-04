// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SMBFS_SMBFS_IMPL_H_
#define SMBFS_SMBFS_IMPL_H_

#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "smbfs/mojom/smbfs.mojom.h"

namespace smbfs {

class SmbFilesystem;

// Implementation of the mojom::SmbFs Mojo interface to provide SMB share
// control to the browser.
class SmbFsImpl : public mojom::SmbFs {
 public:
  explicit SmbFsImpl(base::WeakPtr<SmbFilesystem> fs,
                     mojom::SmbFsRequest request);
  ~SmbFsImpl() override;

 private:
  base::WeakPtr<SmbFilesystem> fs_;
  mojo::Binding<mojom::SmbFs> binding_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(SmbFsImpl);
};

}  // namespace smbfs

#endif  // SMBFS_SMBFS_IMPL_H_
