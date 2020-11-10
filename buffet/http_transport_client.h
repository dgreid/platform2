// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_HTTP_TRANSPORT_CLIENT_H_
#define BUFFET_HTTP_TRANSPORT_CLIENT_H_

#include <map>
#include <memory>
#include <string>

#include <base/memory/weak_ptr.h>
#include <brillo/http/http_request.h>
#include <weave/provider/http_client.h>

namespace brillo {
namespace http {
class Transport;
}
}  // namespace brillo

namespace buffet {

class HttpTransportClient : public weave::provider::HttpClient {
 public:
  HttpTransportClient();
  HttpTransportClient(const HttpTransportClient&) = delete;
  HttpTransportClient& operator=(const HttpTransportClient&) = delete;

  ~HttpTransportClient() override;

  void SendRequest(Method method,
                   const std::string& url,
                   const Headers& headers,
                   const std::string& data,
                   const SendRequestCallback& callback) override;

  void OnSuccessCallback(int id,
                         std::unique_ptr<brillo::http::Response> response);
  void OnErrorCallback(int id, const brillo::Error* brillo_error);

  void SetOnline(bool online);

  void SetLocalIpAddress(const std::string& ip_address);

 private:
  std::map<int, HttpClient::SendRequestCallback> callbacks_;

  std::shared_ptr<brillo::http::Transport> transport_;

  base::WeakPtrFactory<HttpTransportClient> weak_ptr_factory_{this};
};

}  // namespace buffet

#endif  // BUFFET_HTTP_TRANSPORT_CLIENT_H_
