// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/system_property_impl.h"

#include <string>

#include <vboot/crossystem.h>

bool SystemPropertyImpl::GetInt(const std::string& key, int* value_out) {
  int value = ::VbGetSystemPropertyInt(key.c_str());
  if (value == -1)
    return false;
  *value_out = value;
  return true;
}

bool SystemPropertyImpl::SetInt(const std::string& key, int value) {
  return 0 == ::VbSetSystemPropertyInt(key.c_str(), value);
}

bool SystemPropertyImpl::GetString(const std::string& key,
                                   std::string* value_out) {
  char buf[VB_MAX_STRING_PROPERTY];
  if (::VbGetSystemPropertyString(key.c_str(), buf, sizeof(buf)) == nullptr)
    return false;
  *value_out = std::string(buf);
  return true;
}

bool SystemPropertyImpl::SetString(const std::string& key,
                                   const std::string& value) {
  return 0 == ::VbSetSystemPropertyString(key.c_str(), value.c_str());
}
