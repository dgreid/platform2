// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/webauthn_handler.h"

#include <memory>
#include <utility>

#include <u2f/proto_bindings/u2f_interface.pb.h>

namespace u2f {

WebAuthnHandler::WebAuthnHandler()
    : tpm_proxy_(nullptr), user_state_(nullptr) {}

void WebAuthnHandler::Initialize(TpmVendorCommandProxy* tpm_proxy,
                                 UserState* user_state,
                                 std::function<void()> request_presence) {
  tpm_proxy_ = tpm_proxy;
  user_state_ = user_state;
  request_presence_ = request_presence;
}

bool WebAuthnHandler::Initialized() {
  return tpm_proxy_ != nullptr && user_state_ != nullptr;
}

MakeCredentialResponse WebAuthnHandler::MakeCredential(
    const MakeCredentialRequest& request) {
  // TODO(louiscollard): Implement.
  return MakeCredentialResponse();
}

GetAssertionResponse WebAuthnHandler::GetAssertion(
    const GetAssertionRequest& request) {
  // TODO(louiscollard): Implement.
  return GetAssertionResponse();
}

HasCredentialsResponse WebAuthnHandler::HasCredentials(
    const HasCredentialsRequest& request) {
  // TODO(louiscollard): Implement.
  return HasCredentialsResponse();
}

}  // namespace u2f
