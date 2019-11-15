// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef U2FD_WEBAUTHN_HANDLER_H_
#define U2FD_WEBAUTHN_HANDLER_H_

#include <functional>
#include <memory>

#include <u2f/proto_bindings/u2f_interface.pb.h>

#include "u2fd/tpm_vendor_cmd.h"
#include "u2fd/user_state.h"

namespace u2f {

// Implementation of the WebAuthn DBus API.
// More detailed documentation is available in u2f_interface.proto
class WebAuthnHandler {
 public:
  WebAuthnHandler();

  // Initializes WebAuthnHandler.
  // |tpm_proxy| - proxy to send commands to TPM. Owned by U2fDaemon and should
  // outlive WebAuthnHandler.
  // |user_state| - pointer to a UserState instance, for requesting user secret.
  // Owned by U2fDaemon and should outlive WebAuthnHandler.
  // |request_presence| - callback for performing other platform tasks when
  // expecting the user to press the power button.
  void Initialize(TpmVendorCommandProxy* tpm_proxy,
                  UserState* user_state,
                  std::function<void()> request_presence);

  // Generates a new credential.
  MakeCredentialResponse MakeCredential(const MakeCredentialRequest& request);

  // Signs a challenge from the relaying party.
  GetAssertionResponse GetAssertion(const GetAssertionRequest& request);

  // Tests validity and/or presence of specified credentials.
  HasCredentialsResponse HasCredentials(const HasCredentialsRequest& request);

 private:
  bool Initialized();

  TpmVendorCommandProxy* tpm_proxy_;
  UserState* user_state_;
  std::function<void()> request_presence_;
};

}  // namespace u2f

#endif  // U2FD_WEBAUTHN_HANDLER_H_
