// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "trunks/trunks_metrics.h"

#include <string>

#include <base/logging.h>
#include <base/time/time.h>

#include "trunks/error_codes.h"
#include "trunks/tpm_generated.h"

extern "C" {
#include <sys/sysinfo.h>
}

namespace trunks {

namespace {

constexpr char kFirstTimeoutWritingCommand[] =
    "Platform.Trunks.FirstTimeoutWritingCommand";
constexpr char kFirstTimeoutWritingTime[] =
    "Platform.Trunks.FirstTimeoutWritingTime";

constexpr char kFirstTimeoutReadingCommand[] =
    "Platform.Trunks.FirstTimeoutReadingCommand";
constexpr char kFirstTimeoutReadingTime[] =
    "Platform.Trunks.FirstTimeoutReadingTime";

TPM_CC GetCommandCode(const std::string& command) {
  std::string buffer = command;
  TPM_ST tag;
  UINT32 command_size;
  TPM_CC command_code = 0;
  // Parse the header to get the command code
  TPM_RC rc = Parse_TPM_ST(&buffer, &tag, nullptr);
  DCHECK_EQ(rc, TPM_RC_SUCCESS);
  rc = Parse_UINT32(&buffer, &command_size, nullptr);
  DCHECK_EQ(rc, TPM_RC_SUCCESS);
  rc = Parse_TPM_CC(&buffer, &command_code, nullptr);
  DCHECK_EQ(rc, TPM_RC_SUCCESS);
  return command_code;
}

}  // namespace

bool TrunksMetrics::ReportTpmHandleTimeoutCommandAndTime(
    int error_result, const std::string& command) {
  std::string command_metrics, time_metrics;
  switch (error_result) {
    case TRUNKS_RC_WRITE_ERROR:
      command_metrics = kFirstTimeoutWritingCommand;
      time_metrics = kFirstTimeoutWritingTime;
      break;
    case TRUNKS_RC_READ_ERROR:
      command_metrics = kFirstTimeoutReadingCommand;
      time_metrics = kFirstTimeoutReadingTime;
      break;
    default:
      LOG(INFO) << "Reporting unexpected error: " << error_result;
      return false;
  }

  TPM_CC command_code = GetCommandCode(command);
  metrics_library_.SendSparseToUMA(command_metrics,
                                   static_cast<int>(command_code));
  struct sysinfo info;
  if (sysinfo(&info) == 0) {
    constexpr int kMinUptimeInSeconds = 1;
    constexpr int kMaxUptimeInSeconds = 7 * 24 * 60 * 60;  // 1 week
    constexpr int kNumUptimeBuckets = 50;

    metrics_library_.SendToUMA(time_metrics, info.uptime, kMinUptimeInSeconds,
                               kMaxUptimeInSeconds, kNumUptimeBuckets);
  } else {
    PLOG(WARNING) << "Error getting system uptime";
  }
  return true;
}

}  // namespace trunks
