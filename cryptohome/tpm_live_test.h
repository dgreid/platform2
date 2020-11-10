// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test methods that run on a real TPM.
// Note: the TPM must be owned in order for all tests to work correctly.

#ifndef CRYPTOHOME_TPM_LIVE_TEST_H_
#define CRYPTOHOME_TPM_LIVE_TEST_H_

#include <map>
#include <string>

#include "cryptohome/tpm.h"

#include <base/logging.h>
#include <brillo/secure_blob.h>

namespace cryptohome {

class TpmLiveTest {
 public:
  TpmLiveTest();
  TpmLiveTest(const TpmLiveTest&) = delete;
  TpmLiveTest& operator=(const TpmLiveTest&) = delete;

  ~TpmLiveTest() = default;

  // This method runs all or a subset of all tests, depending on the supplied
  // parameters. On TPM 1.2, some tests are only running when |owner_password|
  // is non-empty. On TPM 2.0, some tests run only when
  // |tpm2_use_system_owner_password| is true.
  bool RunLiveTests(const brillo::SecureBlob& owner_password,
                    bool tpm2_use_system_owner_password);

 private:
  // Helper method to try to sign some data.
  bool SignData(const brillo::SecureBlob& pcr_bound_key,
                const brillo::SecureBlob& public_key_der,
                int index);

  // Helper method to try to encrypt and decrypt some data.
  bool EncryptAndDecryptData(const brillo::SecureBlob& pcr_bound_key,
                             const std::map<uint32_t, std::string>& pcr_map);

  // This test checks if PCRs and PCR bound keys work correctly.
  bool PCRKeyTest();

  // This test checks if PCRs and keys bound to multiple PCR indexes work
  // correctly.
  bool MultiplePCRKeyTest();

  // This test checks if we can create and load an RSA decryption key and use
  // it to encrypt and decrypt.
  bool DecryptionKeyTest();

  // This test checks if we can seal and unseal a blob to a PCR state using
  // some authorization value.
  bool SealToPcrWithAuthorizationTest();

  // This test verifies that the Nvram subsystem of the TPM is working
  // correctly.
  // This test requires the TPM owner password to be provided via
  // |owner_password|.
  bool NvramTest(const brillo::SecureBlob& owner_password);

  // This test checks the signature-sealed secret creation and its unsealing. A
  // random RSA key is used.
  // For TPM 1.2, this test requires the TPM owner password to be provided via
  // |owner_password|; for other implementations, this test may be run with an
  // empty |owner_password|.
  bool SignatureSealedSecretTest(const brillo::SecureBlob& owner_password);

  Tpm* tpm_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_TPM_LIVE_TEST_H_
