// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/pinweaver_le_credential_backend.h"

#include <string>

#include <brillo/secure_blob.h>
#include <gtest/gtest.h>
#include <trunks/pinweaver.pb.h>
#include <trunks/tpm_pinweaver.h>

#include "cryptohome/tpm2_impl.h"

namespace cryptohome {

namespace {
// see pinweaver.h for struct leaf_sensitive_data_t
constexpr uint32_t kLeafSensitiveDataSize = 3 * PW_SECRET_SIZE;
}  // namespace

// If these change MetadataBuilder need to be updated
static_assert(56 == offsetof(unimported_leaf_data_t, payload),
              "Changed unimported_leaf_data_t layout");
static_assert(220 == sizeof(leaf_public_data_t),
              "Changed leaf_public_data_t size");

// Can build cred_metadata for versions 0.0 and 0.1
class MetadataBuilder {
 public:
  MetadataBuilder(uint16_t major, uint16_t minor) {
    this->version.major = major;
    this->version.minor = minor;
    memset(&data, 0, sizeof(data));
  }

  MetadataBuilder& SetAttemptCount(uint32_t value) {
    data.attempt_count.v = value;
    return *this;
  }

  MetadataBuilder& SetValidPCRValueBitmask(uint8_t index,
                                           uint8_t b0,
                                           uint8_t b1) {
    EXPECT_LT(index, PW_MAX_PCR_CRITERIA_COUNT);
    data.valid_pcr_criteria[index].bitmask[0] = b0;
    data.valid_pcr_criteria[index].bitmask[1] = b1;
    return *this;
  }

  std::vector<uint8_t> write() {
    EXPECT_TRUE((version.major == 0 && version.minor == 1) ||
                (version.major == 0 && version.minor == 0));

    std::vector<uint8_t> ret(sizeof(unimported_leaf_data_t) +
                                 sizeof(leaf_public_data_t) +
                                 kLeafSensitiveDataSize,
                             0);

    unimported_leaf_data_t header;
    memset(&header, 0, sizeof(header));

    header.head.leaf_version = version;
    header.head.pub_len = sizeof(struct leaf_public_data_t);
    header.head.sec_len = kLeafSensitiveDataSize;

    // Remove field added in version 0.1
    if (version.major == 0 && version.minor == 0)
      header.head.pub_len -=
          sizeof(struct valid_pcr_value_t[PW_MAX_PCR_CRITERIA_COUNT]);

    uint8_t* target = ret.data();

    memcpy(target, &header, sizeof(header));
    memcpy(target + offsetof(unimported_leaf_data_t, payload), &data,
           header.head.pub_len);
    memset(target + offsetof(unimported_leaf_data_t, payload) +
               header.head.pub_len,
           0, header.head.sec_len);

    return ret;
  }

 private:
  struct leaf_version_t version;
  struct leaf_public_data_t data;
};

// Using MetadataBuilder we test the behaviour of functions for all
// possible metadata versions

// CHANGES
// 0.0 Initial version
// 0.1 valid_pcr_value_t field is added to leaf_public_data_t

TEST(PinweaverLECredentialBackend, NeedsPCRBinding) {
  // The field valid_pcr_value_t was added in 0.1
  // Version 0.0 should always return true

  Tpm2Impl tpm;
  PinweaverLECredentialBackend backend(&tpm);

  {
    // Metadata too short
    std::vector<uint8_t> s(10);

    EXPECT_TRUE(backend.NeedsPCRBinding(s));
  }

  {
    std::vector<uint8_t> v_0_0 =
        MetadataBuilder(0, 0).SetValidPCRValueBitmask(0, 0, 0).write();

    EXPECT_TRUE(backend.NeedsPCRBinding(v_0_0));

    v_0_0 = MetadataBuilder(0, 0)
                .SetValidPCRValueBitmask(0, 1, 1)  // Value should be ignored
                .write();

    EXPECT_TRUE(backend.NeedsPCRBinding(v_0_0));
  }

  {
    std::vector<uint8_t> v_0_1 =
        MetadataBuilder(0, 1).SetValidPCRValueBitmask(0, 0, 0).write();

    EXPECT_TRUE(backend.NeedsPCRBinding(v_0_1));

    v_0_1 =
        MetadataBuilder(0, 1)
            .SetValidPCRValueBitmask(0, 0, 1)  // Value should not be ignored
            .write();

    EXPECT_FALSE(backend.NeedsPCRBinding(v_0_1));

    v_0_1 =
        MetadataBuilder(0, 1)
            .SetValidPCRValueBitmask(0, 1, 0)  // Value should not be ignored
            .write();

    EXPECT_FALSE(backend.NeedsPCRBinding(v_0_1));
  }
}

TEST(PinweaverLECredentialBackend, GetWrongAuthAttempts) {
  // No changes should affect GetWrongAuthAttempts

  Tpm2Impl tpm;
  PinweaverLECredentialBackend backend(&tpm);

  {
    // Metadata too short
    std::vector<uint8_t> s(10);

    EXPECT_EQ(-1, backend.GetWrongAuthAttempts(s));
  }

  {
    std::vector<uint8_t> v_0_0 =
        MetadataBuilder(0, 0).SetAttemptCount(5).write();

    EXPECT_EQ(5, backend.GetWrongAuthAttempts(v_0_0));
  }

  {
    std::vector<uint8_t> v_0_1 =
        MetadataBuilder(0, 1).SetAttemptCount(7).write();

    EXPECT_EQ(7, backend.GetWrongAuthAttempts(v_0_1));
  }
}

}  // namespace cryptohome
