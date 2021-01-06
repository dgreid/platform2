// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMASTER_CONTEXT_CHAPS_CRYPTO_OPERATION_H_
#define ARC_KEYMASTER_CONTEXT_CHAPS_CRYPTO_OPERATION_H_

#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>

#include "arc/keymaster/context/crypto_operation.h"

namespace arc {
namespace keymaster {
namespace context {

class ChapsClient;
class ContextAdaptor;

extern const MechanismDescription kCkmRsaPkcsSign;
extern const MechanismDescription kCkmMd5RsaPkcsSign;
extern const MechanismDescription kCkmSha1RsaPkcsSign;
extern const MechanismDescription kCkmSha256RsaPkcsSign;
extern const MechanismDescription kCkmSha384RsaPkcsSign;
extern const MechanismDescription kCkmSha512RsaPkcsSign;

// Implement operations by forwarding them to Chaps via |ChapsClient|.
class ChapsCryptoOperation : public CryptoOperation {
 public:
  ChapsCryptoOperation(base::WeakPtr<ContextAdaptor> context_adaptor,
                       const std::string& label,
                       const brillo::Blob& id);
  ~ChapsCryptoOperation() override;
  // Not copyable nor assignable.
  ChapsCryptoOperation(const ChapsCryptoOperation&) = delete;
  ChapsCryptoOperation& operator=(const ChapsCryptoOperation&) = delete;

  // CryptoOperation overrides:
  base::Optional<uint64_t> Begin(MechanismDescription description) override;
  base::Optional<brillo::Blob> Update(const brillo::Blob& input) override;
  base::Optional<brillo::Blob> Finish() override;
  bool Abort() override;

  bool IsSupportedMechanism(MechanismDescription description) const override;

 private:
  const base::WeakPtr<ContextAdaptor> context_adaptor_;

  // Key label and ID in Chaps, correspond to PKCS#11 CKA_LABEL and CKA_ID.
  const std::string label_;
  const brillo::Blob id_;

  // Chaps client is allocated in Begin, and released in Finish/Abort.
  std::unique_ptr<ChapsClient> chaps_;
};

}  // namespace context
}  // namespace keymaster
}  // namespace arc

#endif  // ARC_KEYMASTER_CONTEXT_CHAPS_CRYPTO_OPERATION_H_
