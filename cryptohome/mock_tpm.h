// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_TPM_H_
#define CRYPTOHOME_MOCK_TPM_H_

#include "cryptohome/tpm.h"

#include <set>

#include <base/logging.h>
#include <chromeos/secure_blob.h>
#include <gmock/gmock.h>

namespace cryptohome {
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

class Crypto;

class MockTpm : public Tpm {
 public:
  MockTpm();
  ~MockTpm();
  MOCK_METHOD4(EncryptBlob, TpmRetryAction(TpmKeyHandle,
                                           const chromeos::SecureBlob&,
                                           const chromeos::SecureBlob&,
                                           chromeos::SecureBlob*));
  MOCK_METHOD4(DecryptBlob, TpmRetryAction(TpmKeyHandle,
                                           const chromeos::SecureBlob&,
                                           const chromeos::SecureBlob&,
                                           chromeos::SecureBlob*));
  MOCK_METHOD2(GetPublicKeyHash, TpmRetryAction(TpmKeyHandle,
                                                chromeos::SecureBlob*));
  MOCK_METHOD1(GetOwnerPassword, bool(chromeos::Blob*));
  MOCK_CONST_METHOD0(IsEnabled, bool());
  MOCK_METHOD1(SetIsEnabled, void(bool));
  MOCK_CONST_METHOD0(IsOwned, bool());
  MOCK_METHOD1(SetIsOwned, void(bool));
  MOCK_METHOD2(PerformEnabledOwnedCheck, bool(bool*, bool*));
  MOCK_CONST_METHOD0(IsInitialized, bool());
  MOCK_METHOD1(SetIsInitialized, void(bool));
  MOCK_CONST_METHOD0(IsBeingOwned, bool());
  MOCK_METHOD1(SetIsBeingOwned, void(bool));
  MOCK_METHOD2(GetRandomData, bool(size_t, chromeos::Blob*));
  MOCK_METHOD2(DefineLockOnceNvram, bool(uint32_t, size_t));
  MOCK_METHOD2(WriteNvram, bool(uint32_t, const chromeos::SecureBlob&));
  MOCK_METHOD2(ReadNvram, bool(uint32_t, chromeos::SecureBlob*));
  MOCK_METHOD1(DestroyNvram, bool(uint32_t));
  MOCK_METHOD1(IsNvramDefined, bool(uint32_t));
  MOCK_METHOD1(IsNvramLocked, bool(uint32_t));
  MOCK_METHOD1(GetNvramSize, unsigned int(uint32_t));
  MOCK_METHOD1(GetEndorsementPublicKey, bool(chromeos::SecureBlob*));
  MOCK_METHOD1(GetEndorsementCredential, bool(chromeos::SecureBlob*));
  MOCK_METHOD9(MakeIdentity, bool(chromeos::SecureBlob*,
                                  chromeos::SecureBlob*,
                                  chromeos::SecureBlob*,
                                  chromeos::SecureBlob*,
                                  chromeos::SecureBlob*,
                                  chromeos::SecureBlob*,
                                  chromeos::SecureBlob*,
                                  chromeos::SecureBlob*,
                                  chromeos::SecureBlob*));
  MOCK_METHOD6(QuotePCR, bool(int,
                              const chromeos::SecureBlob&,
                              const chromeos::SecureBlob&,
                              chromeos::SecureBlob*,
                              chromeos::SecureBlob*,
                              chromeos::SecureBlob*));
  MOCK_METHOD2(SealToPCR0, bool(const chromeos::Blob&, chromeos::Blob*));
  MOCK_METHOD2(Unseal, bool(const chromeos::Blob&, chromeos::Blob*));
  MOCK_METHOD7(CreateCertifiedKey, bool(const chromeos::SecureBlob&,
                                        const chromeos::SecureBlob&,
                                        chromeos::SecureBlob*,
                                        chromeos::SecureBlob*,
                                        chromeos::SecureBlob*,
                                        chromeos::SecureBlob*,
                                        chromeos::SecureBlob*));
  MOCK_METHOD3(CreateDelegate, bool(const chromeos::SecureBlob&,
                                    chromeos::SecureBlob*,
                                    chromeos::SecureBlob*));
  MOCK_METHOD6(ActivateIdentity, bool(const chromeos::SecureBlob&,
                                      const chromeos::SecureBlob&,
                                      const chromeos::SecureBlob&,
                                      const chromeos::SecureBlob&,
                                      const chromeos::SecureBlob&,
                                      chromeos::SecureBlob*));
  MOCK_METHOD3(Sign, bool(const chromeos::SecureBlob&,
                          const chromeos::SecureBlob&,
                          chromeos::SecureBlob*));
  MOCK_METHOD4(CreatePCRBoundKey, bool(int,
                                       const chromeos::SecureBlob&,
                                       chromeos::SecureBlob*,
                                       chromeos::SecureBlob*));
  MOCK_METHOD3(VerifyPCRBoundKey, bool(int,
                                       const chromeos::SecureBlob&,
                                       const chromeos::SecureBlob&));
  MOCK_METHOD2(ExtendPCR, bool(int, const chromeos::SecureBlob&));
  MOCK_METHOD2(ReadPCR, bool(int, chromeos::SecureBlob*));
  MOCK_METHOD0(IsEndorsementKeyAvailable, bool());
  MOCK_METHOD0(CreateEndorsementKey, bool());
  MOCK_METHOD2(TakeOwnership, bool(int, const chromeos::SecureBlob&));
  MOCK_METHOD1(InitializeSrk, bool(const chromeos::SecureBlob&));
  MOCK_METHOD2(ChangeOwnerPassword, bool(const chromeos::SecureBlob&,
                                         const chromeos::SecureBlob&));
  MOCK_METHOD1(TestTpmAuth, bool(const chromeos::SecureBlob&));
  MOCK_METHOD1(SetOwnerPassword, void(const chromeos::SecureBlob&));
  MOCK_METHOD1(IsTransient, bool(TpmRetryAction));
  MOCK_METHOD1(CreateWrappedRsaKey, bool(chromeos::SecureBlob*));
  MOCK_METHOD2(LoadWrappedKey, TpmRetryAction(const chromeos::SecureBlob&,
                                              ScopedKeyHandle*));
  MOCK_METHOD2(LegacyLoadCryptohomeKey, bool(ScopedKeyHandle*,
                                             chromeos::SecureBlob*));
  MOCK_METHOD1(CloseHandle, void(TpmKeyHandle));
  MOCK_METHOD2(GetStatus, void(TpmKeyHandle, TpmStatusInfo*));
  MOCK_METHOD4(GetDictionaryAttackInfo, bool(int*, int*, bool*, int*));
  MOCK_METHOD2(ResetDictionaryAttackMitigation,
               bool(const chromeos::SecureBlob&, const chromeos::SecureBlob&));

 private:
  TpmRetryAction Xor(TpmKeyHandle _key,
                     const chromeos::SecureBlob& plaintext,
                     const chromeos::SecureBlob& key,
                     chromeos::SecureBlob* ciphertext) {
    chromeos::SecureBlob local_data_out(plaintext.size());
    for (unsigned int i = 0; i < local_data_out.size(); i++) {
      local_data_out[i] = plaintext[i] ^ 0x1e;
    }
    ciphertext->swap(local_data_out);
    return kTpmRetryNone;
  }

  bool FakeGetRandomData(size_t num_bytes, chromeos::Blob* blob) {
    blob->resize(num_bytes);
    return true;
  }

  bool FakeExtendPCR(int index, const chromeos::SecureBlob& value) {
    extended_pcrs_.insert(index);
    return true;
  }

  bool FakeReadPCR(int index, chromeos::SecureBlob* value) {
    value->assign(20, extended_pcrs_.count(index) ? 0xAA : 0);
    return true;
  }

  std::set<int> extended_pcrs_;
};
}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_TPM_H_
