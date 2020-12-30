// Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_options.h"

#include <algorithm>

#include <base/containers/adapters.h>
#include <base/stl_util.h>
#include <base/strings/string_util.h>
#include <base/strings/strcat.h>

#include "cros-disks/quote.h"

namespace cros_disks {

namespace {
bool FindLastElementStartingWith(const std::vector<std::string>& container,
                                 base::StringPiece prefix,
                                 std::string* result) {
  for (const auto& element : base::Reversed(container)) {
    if (base::StartsWith(element, prefix, base::CompareCase::SENSITIVE)) {
      *result = element;
      return true;
    }
  }
  return false;
}
}  // namespace

bool IsReadOnlyMount(const std::vector<std::string>& options) {
  for (const auto& option : base::Reversed(options)) {
    if (option == "ro")
      return true;
    if (option == "rw")
      return false;
  }
  return false;
}

bool GetParamValue(const std::vector<std::string>& params,
                   base::StringPiece name,
                   std::string* value) {
  if (!FindLastElementStartingWith(params, base::StrCat({name, "="}), value)) {
    return false;
  }
  *value = value->substr(name.length() + 1);
  return true;
}

void SetParamValue(std::vector<std::string>* params,
                   base::StringPiece name,
                   base::StringPiece value) {
  params->emplace_back(base::StrCat({name, "=", value}));
}

}  // namespace cros_disks
