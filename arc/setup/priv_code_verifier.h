// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_SETUP_PRIV_CODE_VERIFIER_H_
#define ARC_SETUP_PRIV_CODE_VERIFIER_H_

#include <memory>
#include <string>

#include <base/macros.h>

#include "arc/setup/boot_lockbox_client.h"

namespace base {
class FilePath;
}  // namespace base

namespace arc {

// TODO(xzhou): This class is NOT production-ready yet. arc-setup.cc does not
// call into this at all. Talk to xzhou/yusukes before using it.

// A class that verifies the code integrity in /data/dalvik-cache.
// TODO(xzhou): Write unit test.
class PrivCodeVerifier {
 public:
  explicit PrivCodeVerifier(
      std::unique_ptr<BootLockboxClient> boot_lockbox_client);
  PrivCodeVerifier(const PrivCodeVerifier&) = delete;
  PrivCodeVerifier& operator=(const PrivCodeVerifier&) = delete;

  ~PrivCodeVerifier();

  // Waits for cryptohomed to be ready before calling to cryptohomed.
  bool WaitForCryptohomed();

  // Checks code integrity using TPM keys. The function does not check
  // whether TPM is ready or not, call IsTpmReady() before this function.
  bool CheckCodeIntegrity(const base::FilePath& dalvik_cache_directory);

  // Verifies the signatures of compiled code.
  bool Verify(const base::FilePath& directory, const std::string& isa);

  // Verifies the integrity of code in |android_data_directory|.
  bool IsCodeValid(const base::FilePath& android_data_directory);

  // Signs the compiled code in |directory|.
  bool Sign(const base::FilePath& directory);

  // Starts patchoat container and relocate boot image.
  bool PatchImage();

  // Checks if TPM is enabled on device.
  bool IsTpmReady();

 private:
  std::unique_ptr<BootLockboxClient> boot_lockbox_client_;
};

std::unique_ptr<PrivCodeVerifier> CreatePrivCodeVerifier();

}  // namespace arc

#endif  // ARC_SETUP_PRIV_CODE_VERIFIER_H_
