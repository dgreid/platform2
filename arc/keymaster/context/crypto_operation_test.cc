// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymaster/context/crypto_operation.h"

#include <base/containers/flat_set.h>
#include <gtest/gtest.h>
#include "base/optional.h"

namespace arc {
namespace keymaster {
namespace context {

namespace {

const auto kMechanismA = MechanismDescription(OperationType::kSign,
                                              Algorithm::kRsa,
                                              Digest::kSha256,
                                              Padding::kPkcs7,
                                              BlockMode::kNone);

const auto kMechanismB = MechanismDescription(OperationType::kUnsupported,
                                              Algorithm::kRsa,
                                              Digest::kSha256,
                                              Padding::kPkcs7,
                                              BlockMode::kNone);

const auto kMechanismC = MechanismDescription(OperationType::kSign,
                                              Algorithm::kUnsupported,
                                              Digest::kSha256,
                                              Padding::kPkcs7,
                                              BlockMode::kNone);

const base::flat_set<MechanismDescription> kTestOperations = {kMechanismA,
                                                              kMechanismB};

// Concrete implementation of |CryptoOperation| for tests.
class TestOperation : public CryptoOperation {
 public:
  TestOperation() = default;
  ~TestOperation() = default;
  // Not copyable nor assignable.
  TestOperation(const TestOperation&) = delete;
  TestOperation& operator=(const TestOperation&) = delete;

  base::Optional<uint64_t> Begin(MechanismDescription description) {
    return base::nullopt;
  }

  base::Optional<brillo::Blob> Update(const brillo::Blob& input) {
    return base::nullopt;
  }

  base::Optional<brillo::Blob> Finish() { return base::nullopt; }

  bool Abort() { return false; }

  const base::flat_set<MechanismDescription>& SupportedOperations() {
    return kTestOperations;
  }
};

}  // anonymous namespace

TEST(CryptoOperation, IsSupported) {
  TestOperation operation;
  operation.set_description(kMechanismA);
  EXPECT_TRUE(operation.IsSupported());

  operation.set_description(kMechanismB);
  EXPECT_TRUE(operation.IsSupported());

  operation.set_description(kMechanismC);
  EXPECT_FALSE(operation.IsSupported());
}

TEST(MechanismDescription, EqualsOperator) {
  EXPECT_EQ(kMechanismA, kMechanismA);

  MechanismDescription copyOfA(kMechanismA);
  EXPECT_EQ(kMechanismA, copyOfA);

  EXPECT_FALSE(kMechanismA == kMechanismC);
  EXPECT_FALSE(kMechanismB == kMechanismC);
  EXPECT_FALSE(copyOfA == kMechanismC);
}

TEST(MechanismDescription, LessOperator) {
  EXPECT_LT(kMechanismA, kMechanismB);
  EXPECT_LT(kMechanismA, kMechanismC);
  EXPECT_LT(kMechanismB, kMechanismC);
}

}  // namespace context
}  // namespace keymaster
}  // namespace arc
