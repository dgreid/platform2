// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_SYSTEM_PROPERTY_IMPL_H_
#define RUNTIME_PROBE_SYSTEM_PROPERTY_IMPL_H_

#include <string>

#include "runtime_probe/system_property.h"

// System property operation implemented with vboot crossystem.
class SystemPropertyImpl : public SystemProperty {
 public:
  bool GetInt(const std::string& key, int* value_out);
  bool SetInt(const std::string& key, int value);
  bool GetString(const std::string& key, std::string* value_out);
  bool SetString(const std::string& key, const std::string& value);
};

#endif  // RUNTIME_PROBE_SYSTEM_PROPERTY_IMPL_H_
