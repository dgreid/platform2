// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ATTESTATION_PCA_AGENT_SERVER_PCA_RESPONSE_HANDLER_H_
#define ATTESTATION_PCA_AGENT_SERVER_PCA_RESPONSE_HANDLER_H_

#include "attestation/pca_agent/server/pca_agent_service.h"

#include <memory>
#include <string>
#include <utility>

#include <attestation/proto_bindings/interface.pb.h>
#include <base/bind.h>
#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/http/http_request.h>

namespace attestation {
namespace pca_agent {

// A class that is designed for handling the pca response. To achieve its
// purpose, this class implements 2 functions, |OnError| and |OnSuccess|, to
// handle the situations as their names suggest respetively.
// Note that this class is |base::RefCounted| so the caller can bind them into 2
// callbacks at the same time.
template <typename ReplyType>
class PcaResponseHandler final
    : public base::RefCounted<PcaResponseHandler<ReplyType>> {
  using DBusResponseType = brillo::dbus_utils::DBusMethodResponse<ReplyType>;

 public:
  // Constructs a new handler with |name| as its name, and |response| as the
  // dbus response callback. It is intended that this constructor takes
  // ownership of what |response| has.
  PcaResponseHandler(const std::string& name,
                     std::unique_ptr<DBusResponseType> response);
  // Designed to be called when errors occur during sending HTTP request.
  void OnError(brillo::http::RequestID /*not used*/, const brillo::Error* err);
  // Designed to be called when sending HTTP request successfully.
  void OnSuccess(brillo::http::RequestID,
                 std::unique_ptr<brillo::http::Response> pca_response);

 private:
  // The name of the response it is handling; used for logging.
  std::string name_;
  // The dbus response callback, which is called when either |OnError| or
  // |OnSuccess| is called.
  std::unique_ptr<DBusResponseType> response_;

  DISALLOW_COPY_AND_ASSIGN(PcaResponseHandler);
};

}  // namespace pca_agent
}  // namespace attestation

#endif  // ATTESTATION_PCA_AGENT_SERVER_PCA_RESPONSE_HANDLER_H_
