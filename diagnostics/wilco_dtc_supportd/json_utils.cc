// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/wilco_dtc_supportd/json_utils.h"

#include <base/json/json_reader.h>
#include <base/logging.h>
#include <base/values.h>

namespace diagnostics {

bool IsJsonValid(base::StringPiece json, std::string* json_error_message) {
  DCHECK(json_error_message);
  auto result = base::JSONReader::ReadAndReturnValueWithError(
      json, base::JSONParserOptions::JSON_ALLOW_TRAILING_COMMAS);
  *json_error_message = result.error_message;
  return result.value.has_value();
}

}  // namespace diagnostics
