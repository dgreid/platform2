// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for TpmImpl.

#include "cryptohome/tpm_impl.h"

#include <map>
#include <string>

#include <gtest/gtest.h>
#include <openssl/sha.h>

#include <base/macros.h>
#include <base/stl_util.h>

namespace cryptohome {

TEST(TpmImplTest, GetPcrMapNotExtended) {
  TpmImpl tpm;
  std::string obfuscated_username = "OBFUSCATED_USER";
  std::map<uint32_t, std::string> result =
      tpm.GetPcrMap(obfuscated_username, /*use_extended_pcr=*/false);

  EXPECT_EQ(1, result.size());
  const std::string& result_str = result[kTpmSingleUserPCR];

  std::string expected_result(SHA_DIGEST_LENGTH, 0);
  EXPECT_EQ(expected_result, result_str);
}

TEST(TpmImplTest, GetPcrMapExtended) {
  TpmImpl tpm;
  std::string obfuscated_username = "OBFUSCATED_USER";
  std::map<uint32_t, std::string> result =
      tpm.GetPcrMap(obfuscated_username, /*use_extended_pcr=*/true);

  EXPECT_EQ(1, result.size());
  const std::string& result_str = result[kTpmSingleUserPCR];

  // Pre-calculated expected result.
  unsigned char expected_result_bytes[] = {
      0x94, 0xce, 0x1b, 0x97, 0x40, 0xfd, 0x5b, 0x1e, 0x8c, 0x64,
      0xb0, 0xd5, 0x38, 0xac, 0x88, 0xb5, 0xb4, 0x52, 0x4f, 0x67};
  std::string expected_result(reinterpret_cast<char*>(expected_result_bytes),
                              base::size(expected_result_bytes));
  EXPECT_EQ(expected_result, result_str);
}

}  // namespace cryptohome
