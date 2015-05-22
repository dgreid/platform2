// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRIVETD_PRIVET_TYPES_H_
#define PRIVETD_PRIVET_TYPES_H_

#include <string>

#include <chromeos/errors/error.h>

namespace privetd {

class ConnectionState final {
 public:
  enum Status {
    kDisabled,
    kUnconfigured,
    kConnecting,
    kOnline,
    kOffline,
  };

  explicit ConnectionState(Status status) : status_(status) {}
  explicit ConnectionState(chromeos::ErrorPtr error)
      : status_(kOffline), error_(std::move(error)) {}

  Status status() const {
    CHECK(!error_);
    return status_;
  }

  bool IsStatusEqual(Status status) const {
    if (error_)
      return false;
    return status_ == status;
  }

  const chromeos::Error* error() const { return error_.get(); }

 private:
  Status status_;
  chromeos::ErrorPtr error_;
};

class SetupState final {
 public:
  enum Status {
    kNone,
    kInProgress,
    kSuccess,
  };

  explicit SetupState(Status status) : status_(status) {}
  explicit SetupState(chromeos::ErrorPtr error)
      : status_(kNone), error_(std::move(error)) {}

  Status status() const {
    CHECK(!error_);
    return status_;
  }

  bool IsStatusEqual(Status status) const {
    if (error_)
      return false;
    return status_ == status;
  }

  const chromeos::Error* error() const { return error_.get(); }

 private:
  Status status_;
  chromeos::ErrorPtr error_;
};

}  // namespace privetd

#endif  // PRIVETD_PRIVET_TYPES_H_
