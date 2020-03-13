// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_ERROR_H_
#define DLCSERVICE_ERROR_H_

#include <string>

#include <brillo/errors/error.h>

namespace dlcservice {

class Error {
 public:
  // Returns the D-Bus error object with error code and error message set.
  static brillo::ErrorPtr Create(const std::string& code,
                                 const std::string& msg);

  // Returns a string representation of D-Bus error object used to help logging.
  static std::string ToString(const brillo::ErrorPtr& err);
};

}  // namespace dlcservice

#endif  // DLCSERVICE_ERROR_H_
