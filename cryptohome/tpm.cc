// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm.h"

#include "cryptohome/tpm_impl.h"

namespace cryptohome {

Tpm* Tpm::singleton_ = NULL;
base::Lock Tpm::singleton_lock_;

ScopedKeyHandle::ScopedKeyHandle()
    : tpm_(nullptr), handle_(kInvalidKeyHandle) {}

ScopedKeyHandle::~ScopedKeyHandle() {
  if (tpm_ != nullptr && handle_ != kInvalidKeyHandle) {
    tpm_->CloseHandle(handle_);
  }
}

TpmKeyHandle ScopedKeyHandle::value() {
  return handle_;
}

TpmKeyHandle ScopedKeyHandle::release() {
  TpmKeyHandle return_handle = handle_;
  tpm_ = nullptr;
  handle_ = kInvalidKeyHandle;
  return return_handle;
}

void ScopedKeyHandle::reset(Tpm* tpm, TpmKeyHandle handle) {
  tpm_ = tpm;
  handle_ = handle;
}

Tpm* Tpm::GetSingleton() {
  // TODO(fes): Replace with a better atomic operation
  singleton_lock_.Acquire();
  if (!singleton_)
    singleton_ = new TpmImpl();
  singleton_lock_.Release();
  return singleton_;
}

}  // namespace cryptohome
