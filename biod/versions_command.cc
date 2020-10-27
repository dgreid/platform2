// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/versions_command.h"

namespace biod {

EcCmdVersionSupportStatus VersionsCommand::IsVersionSupported(
    uint32_t version) {
  if (Result() == kEcCommandUninitializedResult) {
    // Running EC_CMD_GET_CMD_VERSIONS itself failed (e.g. due to timeout).
    return EcCmdVersionSupportStatus::UNKNOWN;
  }

  if (Result() != EC_RES_SUCCESS) {
    // Command not found on EC.
    return EcCmdVersionSupportStatus::UNSUPPORTED;
  }

  if ((Resp()->version_mask & EC_VER_MASK(version)) == 0) {
    // Command found but version not supported.
    return EcCmdVersionSupportStatus::UNSUPPORTED;
  }

  return EcCmdVersionSupportStatus::SUPPORTED;
}

}  // namespace biod
