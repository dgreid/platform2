// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/common/property.h"

#include <memory>
#include <utility>

#include <base/stl_util.h>

namespace bluetooth {

void PropertySet::RegisterProperty(
    const std::string& property_name,
    std::unique_ptr<dbus::PropertyBase> property_base) {
  CHECK(!base::Contains(properties_, property_name))
      << "Property " << property_name << " already registered";

  dbus::PropertySet::RegisterProperty(property_name, property_base.get());
  properties_.emplace(property_name, std::move(property_base));
}

dbus::PropertyBase* PropertySet::GetProperty(const std::string& property_name) {
  auto iter = properties_.find(property_name);
  CHECK(iter != properties_.end())
      << "Property " << property_name << " doesn't exist";

  return iter->second.get();
}

}  // namespace bluetooth
