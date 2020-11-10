// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TYPECD_CABLE_H_
#define TYPECD_CABLE_H_

#include <map>
#include <memory>
#include <utility>

#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>

#include "typecd/peripheral.h"

namespace typecd {

// A cable represents a cord/connector which is used to connect a Partner
// to a Host. This class is used to maintain the state associated with the
// cable.
class Cable : public Peripheral {
 public:
  explicit Cable(const base::FilePath& syspath) : Peripheral(syspath) {}
  Cable(const Cable&) = delete;
  Cable& operator=(const Cable&) = delete;

  // Check whether the cable supports Thunderbolt3 speed requirements.
  bool TBT3PDIdentityCheck();
};

}  // namespace typecd

#endif  // TYPECD_CABLE_H_
