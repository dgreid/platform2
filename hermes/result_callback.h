// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_RESULT_CALLBACK_H_
#define HERMES_RESULT_CALLBACK_H_

#include <memory>
#include <utility>

namespace hermes {

template <typename... T>
class ResultCallback {
 public:
  using DBusResponse = brillo::dbus_utils::DBusMethodResponse<T...>;
  explicit ResultCallback(std::unique_ptr<DBusResponse> response)
      : response_(std::move(response)) {}
  ResultCallback(const ResultCallback&) = default;
  ResultCallback(ResultCallback&&) = default;
  ResultCallback& operator=(const ResultCallback&) = default;
  ResultCallback& operator=(ResultCallback&&) = default;

  void Success(const T&... object) const { response_->Return(object...); }
  void Error(const brillo::ErrorPtr& decoded_error) const {
    response_->ReplyWithError(decoded_error.get());
  }

 private:
  std::shared_ptr<DBusResponse> response_;
};

}  // namespace hermes

#endif  // HERMES_RESULT_CALLBACK_H_
