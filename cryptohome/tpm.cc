// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/tpm.h"

#include <base/command_line.h>
#include <base/strings/stringprintf.h>
#include <inttypes.h>

#include <string>

#include "cryptohome/cryptolib.h"

#if USE_TPM2
#include "cryptohome/tpm2_impl.h"
#else
#include "cryptohome/tpm_impl.h"
#include "cryptohome/tpm_new_impl.h"
#endif

namespace cryptohome {

Tpm* Tpm::singleton_ = NULL;
base::Lock Tpm::singleton_lock_;

const uint32_t Tpm::kLockboxIndex = cryptohome::kLockboxIndex;

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
  if ((tpm_ != tpm) || (handle_ != handle)) {
    if ((tpm_ != nullptr) && (handle_ != kInvalidKeyHandle)) {
      tpm_->CloseHandle(handle_);
    }
    tpm_ = tpm;
    handle_ = handle;
  }
}

int Tpm::TpmVersionInfo::GetFingerprint() const {
  // The exact encoding doesn't matter as long as its unambiguous, stable and
  // contains all information present in the version fields.
  std::string encoded_parameters =
      base::StringPrintf("%08" PRIx32 "%016" PRIx64 "%08" PRIx32 "%08" PRIx32
                         "%016" PRIx64 "%016zx",
                         family, spec_level, manufacturer, tpm_model,
                         firmware_version, vendor_specific.size());
  encoded_parameters.append(vendor_specific);
  brillo::SecureBlob hash = CryptoLib::Sha256(
      brillo::SecureBlob(encoded_parameters.begin(), encoded_parameters.end()));

  // Return the first 31 bits from |hash|.
  int result = hash[0] | hash[1] << 8 | hash[2] << 16 | hash[3] << 24;
  return result & 0x7fffffff;
}

Tpm* Tpm::GetSingleton() {
  // TODO(fes): Replace with a better atomic operation
  singleton_lock_.Acquire();
  if (!singleton_) {
#if USE_TPM2
    singleton_ = new Tpm2Impl();
#else
    singleton_ = new TpmNewImpl();
#endif
  }
  singleton_lock_.Release();
  return singleton_;
}

}  // namespace cryptohome
