// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port.h"

#include <base/logging.h>
#include <re2/re2.h>

namespace {

constexpr char kPortNumRegex[] = R"(port(\d+))";

}  // namespace

namespace typecd {

// static
std::unique_ptr<Port> Port::CreatePort(const base::FilePath& syspath) {
  int port_num;
  if (!RE2::FullMatch(syspath.BaseName().value(), kPortNumRegex, &port_num)) {
    LOG(ERROR) << "Couldn't extract port num from syspath.";
    return nullptr;
  }

  return std::make_unique<Port>(syspath, port_num);
}

Port::Port(const base::FilePath& syspath, int port_num)
    : syspath_(syspath), port_num_(port_num) {
  LOG(INFO) << "Port " << port_num_ << " enumerated.";
}

void Port::AddPartner(const base::FilePath& path) {
  partner_ = std::make_unique<Partner>(path);
}

void Port::RemovePartner() {
  partner_.reset();
}

}  // namespace typecd
