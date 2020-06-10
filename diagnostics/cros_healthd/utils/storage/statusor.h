// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_STATUSOR_H_
#define DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_STATUSOR_H_

#include <string>

#include <base/strings/stringprintf.h>

namespace diagnostics {

class Status {
 public:
  Status(int code, const std::string& message)
      : code_(code), message_(message) {}
  Status(const Status&) = default;
  Status(Status&&) = default;
  Status& operator=(const Status&) = default;
  Status& operator=(Status&&) = default;

  int code() const { return code_; }
  const std::string& message() const { return message_; }

  const std::string ToString() const {
    return base::StringPrintf("%s : %d", message_.c_str(), code_);
  }

  bool ok() const { return code_ == 0; }

  static Status OkStatus() { return Status(0, ""); }

 private:
  int code_;
  std::string message_;
};

template <class valueType>
class StatusOr {
 public:
  // Implicit conversion to StatusOr to allow transparent "return"s.
  StatusOr(const Status& status)  // NOLINT(runtime/explicit)
      : status_(status) {}
  StatusOr(const valueType& value)  // NOLINT(runtime/explicit)
      : status_(Status::OkStatus()), value_(value) {}

  StatusOr(const StatusOr<valueType>&) = default;
  StatusOr(StatusOr<valueType>&&) = default;
  StatusOr& operator=(const StatusOr<valueType>&) = default;
  StatusOr& operator=(StatusOr<valueType>&&) = default;

  bool ok() const { return status_.ok(); }

  const Status& status() const { return status_; }
  const valueType& value() const {
    DCHECK(ok());
    return value_;
  }

 private:
  Status status_;
  valueType value_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_UTILS_STORAGE_STATUSOR_H_
