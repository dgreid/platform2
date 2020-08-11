// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/strings/string_util.h>

#include "lorgnette/guess_source.h"

base::Optional<lorgnette::SourceType> GuessSourceType(const std::string& name) {
  std::string lowercase = base::ToLowerASCII(name);

  if (lowercase == "fb" || lowercase == "flatbed")
    return lorgnette::SOURCE_PLATEN;

  if (lowercase == "adf" || lowercase == "adf front" ||
      lowercase == "automatic document feeder")
    return lorgnette::SOURCE_ADF_SIMPLEX;

  if (lowercase == "adf duplex")
    return lorgnette::SOURCE_ADF_DUPLEX;

  return base::nullopt;
}
