// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/tpm_simulator_handle.h"

#include <unistd.h>

#if defined(USE_SIMULATOR)
extern "C" {
#include <tpm2/_TPM_Init_fp.h>
#include <tpm2/BaseTypes.h>
#include <tpm2/ExecCommand_fp.h>
#include <tpm2/tpm_manufacture.h>
#include <tpm2/TpmBuildSwitches.h>
#include <tpm2/Manufacture_fp.h>  // NOLINT(build/include_alpha) - needs TpmBuildSwitches.h
#include <tpm2/Platform.h>
}  // extern "C"
#endif  // USE_SIMULATOR

#include <base/callback.h>
#include <base/logging.h>
#include <base/stl_util.h>

#include "trunks/error_codes.h"

namespace {

const char kSimulatorStateDirectory[] = "/var/lib/trunks";

}  // namespace

namespace trunks {

TpmSimulatorHandle::TpmSimulatorHandle() {}

TpmSimulatorHandle::~TpmSimulatorHandle() {}

bool TpmSimulatorHandle::Init() {
  CHECK_EQ(chdir(kSimulatorStateDirectory), 0);
  if (!init_) {
    InitializeSimulator();
    init_ = true;
  }
  return true;
}

void TpmSimulatorHandle::InitializeSimulator() {
#if defined(USE_SIMULATOR)
  // Initialize TPM.
  _plat__Signal_PowerOn();
  /*
   * Make sure NV RAM metadata is initialized, needed to check
   * manufactured status. This is a speculative call which will have to
   * be repeated in case the TPM has not been through the manufacturing
   * sequence yet. No harm in calling it twice in that case.
   */
  _TPM_Init();
  _plat__SetNvAvail();

  if (!tpm_manufactured()) {
    TPM_Manufacture(TRUE);
    // TODO(b/132145000): Verify if the second call to _TPM_Init() is necessary.
    _TPM_Init();
    if (!tpm_endorse())
      LOG(ERROR) << __func__ << " Failed to endorse TPM with a fixed key.";
  }

  // Send TPM2_Startup(TPM_SU_CLEAR), ignore the result. This is normally done
  // by firmware. Without TPM2_Startup, TpmUtility::CheckState() fails,
  // ResourceManager aborts initialization, and trunks daemon dies.
  unsigned int response_size;
  unsigned char* response;
  unsigned char startup_cmd[] = {
    0x80, 0x01, /* TPM_ST_NO_SESSIONS */
    0x00, 0x00, 0x00, 0x0c, /* commandSize = 12 */
    0x00, 0x00, 0x01, 0x44, /* TPM_CC_Startup */
    0x00, 0x00 /* TPM_SU_CLEAR */
  };
  ExecuteCommand(sizeof(startup_cmd), startup_cmd, &response_size, &response);
  LOG(INFO) << "TPM2_Startup(TPM_SU_CLEAR) sent.";
#else
  LOG(FATAL) << "Simulator not configured.";
#endif
}

void TpmSimulatorHandle::SendCommand(const std::string& command,
                                     const ResponseCallback& callback) {
  callback.Run(SendCommandAndWait(command));
}

std::string TpmSimulatorHandle::SendCommandAndWait(const std::string& command) {
  if (!init_) {
    InitializeSimulator();
    init_ = true;
  }
#if defined(USE_SIMULATOR)
  unsigned int response_size;
  unsigned char* response;
  std::string mutable_command(command);
  ExecuteCommand(command.size(),
                 reinterpret_cast<unsigned char*>(base::data(mutable_command)),
                 &response_size, &response);
  return std::string(reinterpret_cast<char*>(response), response_size);
#else
  return CreateErrorResponse(TCTI_RC_GENERAL_FAILURE);
#endif
}

}  // namespace trunks
