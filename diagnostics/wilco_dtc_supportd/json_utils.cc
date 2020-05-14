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
  int json_error_code = base::JSONReader::JSON_NO_ERROR;
  // TODO(crbug.com/1054279): use base::JSONReader::ReadAndReturnValueWithError
  // after uprev to r680000.
  base::JSONReader::ReadAndReturnErrorDeprecated(
      json, base::JSONParserOptions::JSON_ALLOW_TRAILING_COMMAS,
      &json_error_code, json_error_message, nullptr, nullptr);
  return json_error_code == base::JSONReader::JSON_NO_ERROR;
}

}  // namespace diagnostics
