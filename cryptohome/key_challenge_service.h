// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_KEY_CHALLENGE_SERVICE_H_
#define CRYPTOHOME_KEY_CHALLENGE_SERVICE_H_

#include <memory>

#include <base/callback.h>

#include "cryptohome/rpc.pb.h"

namespace cryptohome {

// Interface for making challenge requests against the specified cryptographic
// key.
//
// In real use cases, the implementation of this interface will make IPC
// requests to the service that talks to the cryptographic token with the
// challenged key.
//
// This class is intended to be used only on a single thread / task runner only.
// Response callbacks will also be run on the same thread / task runner.
class KeyChallengeService {
 public:
  virtual ~KeyChallengeService() = default;

  // This callback is called with a response for a challenge request made via
  // ChallengeKey().
  //
  // In case of error, |response| will be null; otherwise, it will contain the
  // challenge response data.
  using ResponseCallback =
      base::OnceCallback<void(std::unique_ptr<KeyChallengeResponse> response)>;

  // Starts a challenge request against the specified cryptographic key.
  //
  // The challenge data is passed via |key_challenge_request|, and |account_id|
  // specifies the account whom the vault keyset in question belongs. The result
  // is reported via |response_callback|.
  virtual void ChallengeKey(const AccountIdentifier& account_id,
                            const KeyChallengeRequest& key_challenge_request,
                            ResponseCallback response_callback) = 0;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEY_CHALLENGE_SERVICE_H_
