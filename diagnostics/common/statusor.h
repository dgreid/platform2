// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_COMMON_STATUSOR_H_
#define DIAGNOSTICS_COMMON_STATUSOR_H_

#include <string>
#include <utility>

#include <base/strings/stringprintf.h>

namespace diagnostics {

// Canonical error codes, copied from absl.
enum class StatusCode : int {
  kOk = 0,
  kInvalidArgument = 3,
  kInternal = 13,
  kUnavailable = 14,
};

// TODO(chromium:1102627, dlunev): use absl status instead when the dependency
// is available.
class Status {
 public:
  static Status OkStatus() { return Status(StatusCode::kOk, ""); }

  Status(StatusCode code, const std::string& message)
      : code_(code), message_(message) {}
  Status(const Status&) = default;
  Status(Status&&) = default;
  Status& operator=(const Status&) = default;
  Status& operator=(Status&&) = default;

  StatusCode code() const { return code_; }
  const std::string& message() const { return message_; }

  const std::string ToString() const {
    return base::StringPrintf("%s : %d", message_.c_str(), code_);
  }

  bool ok() const { return code_ == StatusCode::kOk; }

 private:
  StatusCode code_;
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
  StatusOr(valueType&& value)  // NOLINT(runtime/explicit)
      : status_(Status::OkStatus()), value_(std::move(value)) {}

  StatusOr(const StatusOr<valueType>&) = default;
  StatusOr(StatusOr<valueType>&&) = default;
  StatusOr& operator=(const StatusOr<valueType>&) = default;
  StatusOr& operator=(StatusOr<valueType>&&) = default;

  bool ok() const { return status_.ok(); }

  const Status& status() const { return status_; }

  const valueType& value() const& {
    DCHECK(ok());
    return value_;
  }

  valueType& value() & {
    DCHECK(ok());
    return value_;
  }

  const valueType&& value() const&& {
    DCHECK(ok());
    return std::move(value_);
  }

  valueType&& value() && {
    DCHECK(ok());
    return std::move(value_);
  }

 private:
  Status status_;
  valueType value_;
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_COMMON_STATUSOR_H_
