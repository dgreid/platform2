// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_TPM_UTILITY_MOCK_H_
#define CHAPS_TPM_UTILITY_MOCK_H_

#include <string>

#include "chaps/tpm_utility.h"

#include <base/macros.h>
#include <gmock/gmock.h>

namespace chaps {

class TPMUtilityMock : public TPMUtility {
 public:
  TPMUtilityMock();
  TPMUtilityMock(const TPMUtilityMock&) = delete;
  TPMUtilityMock& operator=(const TPMUtilityMock&) = delete;

  ~TPMUtilityMock() override;

  MOCK_METHOD0(MinRSAKeyBits, size_t());
  MOCK_METHOD0(MaxRSAKeyBits, size_t());
  MOCK_METHOD0(Init, bool());
  MOCK_METHOD0(IsTPMAvailable, bool());
  MOCK_METHOD0(GetTPMVersion, TPMVersion());
  MOCK_METHOD5(Authenticate,
               bool(int,
                    const brillo::SecureBlob&,
                    const std::string&,
                    const std::string&,
                    brillo::SecureBlob*));
  MOCK_METHOD5(ChangeAuthData,
               bool(int,
                    const brillo::SecureBlob&,
                    const brillo::SecureBlob&,
                    const std::string&,
                    std::string*));
  MOCK_METHOD2(GenerateRandom, bool(int, std::string*));
  MOCK_METHOD1(StirRandom, bool(const std::string&));
  MOCK_METHOD6(GenerateRSAKey,
               bool(int,
                    int,
                    const std::string&,
                    const brillo::SecureBlob&,
                    std::string*,
                    int*));
  MOCK_METHOD3(GetRSAPublicKey, bool(int, std::string*, std::string*));
  MOCK_METHOD1(IsECCurveSupported, bool(int));
  MOCK_METHOD5(GenerateECCKey,
               bool(int, int, const brillo::SecureBlob&, std::string*, int*));
  MOCK_METHOD2(GetECCPublicKey, bool(int, std::string*));
  MOCK_METHOD7(WrapRSAKey,
               bool(int,
                    const std::string&,
                    const std::string&,
                    const std::string&,
                    const brillo::SecureBlob&,
                    std::string*,
                    int*));
  MOCK_METHOD8(WrapECCKey,
               bool(int,
                    int,
                    const std::string&,
                    const std::string&,
                    const std::string&,
                    const brillo::SecureBlob&,
                    std::string*,
                    int*));
  MOCK_METHOD4(LoadKey,
               bool(int, const std::string&, const brillo::SecureBlob&, int*));
  MOCK_METHOD5(
      LoadKeyWithParent,
      bool(int, const std::string&, const brillo::SecureBlob&, int, int*));
  MOCK_METHOD1(UnloadKeysForSlot, void(int));
  MOCK_METHOD3(Bind, bool(int, const std::string&, std::string*));
  MOCK_METHOD3(Unbind, bool(int, const std::string&, std::string*));
  MOCK_METHOD5(Sign,
               bool(int,
                    CK_MECHANISM_TYPE,
                    const std::string&,
                    const std::string&,
                    std::string*));
  MOCK_METHOD0(IsSRKReady, bool());
};

}  // namespace chaps

#endif  // CHAPS_TPM_UTILITY_MOCK_H_
