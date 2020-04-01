// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_CLIENT_H_
#define LORGNETTE_SANE_CLIENT_H_

#include <map>
#include <memory>
#include <string>

#include <brillo/errors/error.h>
#include <sane/sane.h>

namespace lorgnette {

// This class represents a connection to the scanner library SANE.  Once
// created, it will initialize a connection to SANE, and it will disconnect
// when destroyed.
// At most 1 connection to SANE is allowed to be active per process, so the
// user must be careful to ensure that is the case.
class SaneClient {
 public:
  typedef std::map<std::string, std::map<std::string, std::string>> ScannerInfo;

  virtual ~SaneClient() {}

  virtual bool ListDevices(brillo::ErrorPtr* error, ScannerInfo* info_out) = 0;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CLIENT_H_
