// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/mock_tpm.h"

#include <string>

using testing::_;
using testing::DoAll;
using testing::SetArgumentPointee;
using testing::Return;

namespace cryptohome {

MockTpm::MockTpm() {
  ON_CALL(*this, EncryptBlob(_, _, _, _))
      .WillByDefault(Invoke(this, &MockTpm::Xor));
  ON_CALL(*this, DecryptBlob(_, _, _, _))
      .WillByDefault(Invoke(this, &MockTpm::Xor));
  ON_CALL(*this, GetPublicKeyHash(_, _))
      .WillByDefault(Return(kTpmRetryNone));
  ON_CALL(*this, GetEndorsementPublicKey(_))
      .WillByDefault(Return(true));
  ON_CALL(*this, GetEndorsementCredential(_))
      .WillByDefault(DoAll(SetArgumentPointee<0>(chromeos::SecureBlob("test")),
                           Return(true)));
  ON_CALL(*this, MakeIdentity(_, _, _, _, _, _, _, _, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, ActivateIdentity(_, _, _, _, _, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, QuotePCR(_, _, _, _, _, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, SealToPCR0(_, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, Unseal(_, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, GetRandomData(_, _))
      .WillByDefault(Invoke(this, &MockTpm::FakeGetRandomData));
  ON_CALL(*this, CreateDelegate(_, _, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, CreateCertifiedKey(_, _, _, _, _, _, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, Sign(_, _, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, CreatePCRBoundKey(_, _, _, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, VerifyPCRBoundKey(_, _, _))
      .WillByDefault(Return(true));
  ON_CALL(*this, ExtendPCR(_, _))
      .WillByDefault(Invoke(this, &MockTpm::FakeExtendPCR));
  ON_CALL(*this, ReadPCR(_, _))
      .WillByDefault(Invoke(this, &MockTpm::FakeReadPCR));
}

MockTpm::~MockTpm() {}

}  // namespace cryptohome
