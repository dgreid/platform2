// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "typecd/port.h"

#include <base/logging.h>
#include <re2/re2.h>

namespace typecd {

Port::Port(const base::FilePath& syspath, int port_num)
    : syspath_(syspath), port_num_(port_num) {
  LOG(INFO) << "Port " << port_num_ << " enumerated.";
}

void Port::AddPartner(const base::FilePath& path) {
  if (partner_) {
    LOG(WARNING) << "Partner already exists for port " << port_num_;
    return;
  }
  partner_ = std::make_unique<Partner>(path);

  LOG(INFO) << "Partner enumerated for port " << port_num_;
}

void Port::RemovePartner() {
  if (!partner_) {
    LOG(WARNING) << "No partner present for port " << port_num_;
    return;
  }
  partner_.reset();

  LOG(INFO) << "Partner removed for port " << port_num_;
}

}  // namespace typecd
