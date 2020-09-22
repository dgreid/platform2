// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/smdp.h"

#include <string>
#include <utility>

#include <base/base64.h>
#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/json/json_reader.h>
#include <base/json/json_writer.h>
#include <base/values.h>

namespace hermes {

SmdpFactory::SmdpFactory(Logger* logger, Executor* executor)
    : logger_(logger), executor_(executor) {}

std::unique_ptr<lpa::smdp::SmdpClient> SmdpFactory::NewSmdpClient(
    std::string tls_certs_dir,
    std::string smdp_addr,
    const lpa::proto::EuiccSpecVersion& card_verison) {
  return std::make_unique<Smdp>(std::move(smdp_addr), std::move(tls_certs_dir),
                                logger_, executor_);
}

Smdp::Smdp(std::string server_addr,
           const std::string& certs_dir,
           Logger* logger,
           Executor* executor)
    : server_transport_(brillo::http::Transport::CreateDefault()),
      logger_(logger),
      executor_(executor),
      weak_factory_(this) {
  if (certs_dir.find("/test/") != std::string::npos) {
    LOG(INFO) << "Using SSL certificates for GSMA test servers";
    server_transport_->UseCustomCertificate(
        brillo::http::Transport::Certificate::kHermesTest);
  } else {
    LOG(INFO) << "Using SSL certificates for GSMA production servers";
    server_transport_->UseCustomCertificate(
        brillo::http::Transport::Certificate::kHermesProd);
  }
  smdp_addr_ = std::move(server_addr);
  // Ensure |smdp_addr_| does not begin with a scheme (e.g. "https://"), as this
  // variable will be used for the smdpAddress field in SM-DP+ communications.
  size_t found = smdp_addr_.find("://");
  if (found != std::string::npos) {
    smdp_addr_.erase(0, found + 3);
  }
}

lpa::util::EuiccLog* Smdp::logger() {
  return logger_;
}

lpa::util::Executor* Smdp::executor() {
  return executor_;
}

void Smdp::SendHttps(const std::string& path,
                     const std::string& request,
                     LpaCallback cb) {
  brillo::ErrorPtr error = nullptr;
  std::string url = "https://" + smdp_addr_ + path;
  VLOG(1) << __func__ << ": sending data to " << url << ": " << request;
  brillo::http::Request http_request(url, brillo::http::request_type::kPost,
                                     server_transport_);
  http_request.SetContentType("application/json");
  http_request.SetUserAgent("gsma-rsp-lpad");
  http_request.AddHeader("X-Admin-Protocol", "gsma/rsp/v2.0.0");
  http_request.AddRequestBody(&request[0], request.size(), &error);
  CHECK(!error);
  http_request.GetResponse(
      base::Bind(&Smdp::OnHttpsResponse, weak_factory_.GetWeakPtr(), cb),
      base::Bind(&Smdp::OnHttpsError, weak_factory_.GetWeakPtr(), cb));
}

void Smdp::OnHttpsResponse(LpaCallback cb,
                           brillo::http::RequestID request_id,
                           std::unique_ptr<brillo::http::Response> response) {
  std::string raw_data;
  if (!response) {
    cb(0, raw_data, lpa::smdp::SmdpClient::kMalformedResponse);
    return;
  }

  raw_data = response->ExtractDataAsString();
  VLOG(1) << __func__ << ": Response raw_data : " << raw_data;

  cb(response->GetStatusCode(), raw_data, lpa::smdp::SmdpClient::kNoError);
}

void Smdp::OnHttpsError(LpaCallback cb,
                        brillo::http::RequestID request_id,
                        const brillo::Error* error) {
  LOG(WARNING) << "HTTPS request failed (brillo error code " << error->GetCode()
               << "): " << error->GetMessage();
  std::string empty;
  cb(0, empty, lpa::smdp::SmdpClient::kSendHttpsError);
}

}  // namespace hermes
