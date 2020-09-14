// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mock_tpm.h"

#include <string>

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::SetArgPointee;

namespace cryptohome {

MockTpm::MockTpm() {
  ON_CALL(*this, GetVersion()).WillByDefault(Return(TpmVersion::TPM_UNKNOWN));
  ON_CALL(*this, EncryptBlob(_, _, _, _))
      .WillByDefault(Invoke(this, &MockTpm::Xor));
  ON_CALL(*this, DecryptBlob(_, _, _, _, _))
      .WillByDefault(Invoke(this, &MockTpm::XorDecrypt));
  ON_CALL(*this, GetPublicKeyHash(_, _)).WillByDefault(Return(kTpmRetryNone));
  ON_CALL(*this, GetEndorsementPublicKey(_))
      .WillByDefault(Return(Tpm::kTpmRetryNone));
  ON_CALL(*this, GetEndorsementPublicKeyWithDelegate(_, _, _))
      .WillByDefault(Return(Tpm::kTpmRetryNone));
  ON_CALL(*this, GetEndorsementCredential(_))
      .WillByDefault(
          DoAll(SetArgPointee<0>(brillo::SecureBlob("test")), Return(true)));
  ON_CALL(*this, MakeIdentity(_, _, _, _, _, _, _, _, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, ActivateIdentity(_, _, _, _, _, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, QuotePCR(_, _, _, _, _, _, _))
      .WillByDefault(Return(Tpm::QuotePcrResult::kSuccess));
  ON_CALL(*this, SealToPCR0(_, _)).WillByDefault(Return(true));
  ON_CALL(*this, Unseal(_, _)).WillByDefault(Return(true));
  ON_CALL(*this, GetRandomDataBlob(_, _))
      .WillByDefault(Invoke(this, &MockTpm::FakeGetRandomDataBlob));
  ON_CALL(*this, GetRandomDataSecureBlob(_, _))
      .WillByDefault(Invoke(this, &MockTpm::FakeGetRandomDataSecureBlob));
  ON_CALL(*this, GetAlertsData(_)).WillByDefault(Return(true));
  ON_CALL(*this, CreateDelegate(_, _, _, _, _)).WillByDefault(Return(true));
  ON_CALL(*this, CreateCertifiedKey(_, _, _, _, _, _, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, Sign(_, _, _, _)).WillByDefault(Return(true));
  ON_CALL(*this, CreatePCRBoundKey(_, _, _, _, _)).WillByDefault(Return(true));
  ON_CALL(*this, VerifyPCRBoundKey(_, _, _)).WillByDefault(Return(true));
  ON_CALL(*this, ExtendPCR(_, _))
      .WillByDefault(Invoke(this, &MockTpm::FakeExtendPCR));
  ON_CALL(*this, ReadPCR(_, _))
      .WillByDefault(Invoke(this, &MockTpm::FakeReadPCR));
  ON_CALL(*this, SetUserType(_)).WillByDefault(Return(true));
  ON_CALL(*this, GetRsuDeviceId(_)).WillByDefault(Return(true));
  ON_CALL(*this, GetLECredentialBackend()).WillByDefault(Return(nullptr));
  ON_CALL(*this, GetDelegate(_, _, _)).WillByDefault(Return(true));
  ON_CALL(*this, DoesUseTpmManager()).WillByDefault(Return(true));
  ON_CALL(*this, UnsealWithAuthorization(_, _, _, _, _))
      .WillByDefault(Return(Tpm::kTpmRetryNone));
  ON_CALL(*this, IsCurrentPCR0ValueValid()).WillByDefault(Return(true));
}

MockTpm::~MockTpm() {}

}  // namespace cryptohome
