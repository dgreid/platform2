// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libwebserv/request_utils.h>

#include <utility>

#include <base/bind.h>
#include <brillo/streams/memory_stream.h>
#include <brillo/streams/stream_utils.h>
#include <libwebserv/request.h>
#include <libwebserv/response.h>

namespace libwebserv {

namespace {

struct RequestDataContainer {
  std::unique_ptr<Request> request;
  std::unique_ptr<Response> response;
  GetRequestDataSuccessCallback success_callback;
  GetRequestDataErrorCallback error_callback;
  std::vector<uint8_t> data;
};

void OnCopySuccess(std::shared_ptr<RequestDataContainer> container,
                   brillo::StreamPtr /* in_stream */,
                   brillo::StreamPtr out_stream,
                   uint64_t /* size_copied */) {
  // Close/release the memory stream so we can work with underlying data buffer.
  out_stream->CloseBlocking(nullptr);
  out_stream.reset();
  container->success_callback.Run(std::move(container->request),
                                  std::move(container->response),
                                  std::move(container->data));
}

void OnCopyError(std::shared_ptr<RequestDataContainer> container,
                 brillo::StreamPtr /* in_stream */,
                 brillo::StreamPtr /* out_stream */,
                 const brillo::Error* error) {
  container->error_callback.Run(std::move(container->request),
                                std::move(container->response), error);
}

}  // anonymous namespace

void GetRequestData(std::unique_ptr<Request> request,
                    std::unique_ptr<Response> response,
                    const GetRequestDataSuccessCallback& success_callback,
                    const GetRequestDataErrorCallback& error_callback) {
  auto container = std::make_shared<RequestDataContainer>();
  auto in_stream = request->GetDataStream();
  auto out_stream = brillo::MemoryStream::CreateRef(&container->data, nullptr);
  container->request = std::move(request);
  container->response = std::move(response);
  container->success_callback = success_callback;
  container->error_callback = error_callback;
  brillo::stream_utils::CopyData(std::move(in_stream), std::move(out_stream),
                                 base::Bind(&OnCopySuccess, container),
                                 base::Bind(&OnCopyError, container));
}

}  // namespace libwebserv
