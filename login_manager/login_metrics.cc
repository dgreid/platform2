// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file handles the details of reporting user metrics related to login.

#include "login_manager/login_metrics.h"

#include <string>

#include <base/files/file_util.h>
#include <base/sys_info.h>
#include <base/time/default_clock.h>
#include <base/time/default_tick_clock.h>
#include <brillo/make_unique_ptr.h>
#include <metrics/bootstat.h>
#include <metrics/metrics_library.h>

#include "login_manager/cumulative_use_time_metric.h"

namespace login_manager {
namespace {
// Uptime stats file created when session_manager executes Chrome.
// For any case of reload after crash no stats are recorded.
// For any signout stats are recorded.
const char kChromeUptimeFile[] = "/tmp/uptime-chrome-exec";

const char kLoginConsumerAllowsNewUsersMetric[] =
    "Login.ConsumerNewUsersAllowed";
const char kLoginPolicyFilesMetric[] = "Login.PolicyFilesStatePerBoot";
const char kLoginUserTypeMetric[] = "Login.UserType";
const char kLoginStateKeyGenerationStatus[] = "Login.StateKeyGenerationStatus";
const int kMaxPolicyFilesValue = 64;
const char kLoginMetricsFlagFile[] = "per_boot_flag";
const char kMetricsDir[] = "/var/lib/metrics";

const char kArcCumulativeUseTimeMetric[] = "Arc.CumulativeUseTime";

}  // namespace

// static
int LoginMetrics::PolicyFilesStatusCode(const PolicyFilesStatus& status) {
  return (status.owner_key_file_state * 16      /* 4^2 */ +
          status.policy_file_state * 4          /* 4^1 */ +
          status.defunct_prefs_file_state * 1   /* 4^0 */);
}

LoginMetrics::LoginMetrics(const base::FilePath& per_boot_flag_dir)
    : per_boot_flag_file_(per_boot_flag_dir.Append(kLoginMetricsFlagFile)) {
  metrics_lib_.Init();

  if (metrics_lib_.AreMetricsEnabled()) {
    arc_cumulative_use_time_.reset(new CumulativeUseTimeMetric(
        kArcCumulativeUseTimeMetric,
        &metrics_lib_,
        base::FilePath(kMetricsDir),
        brillo::make_unique_ptr(new base::DefaultClock()),
        brillo::make_unique_ptr(new base::DefaultTickClock())));
    std::string version;
    base::SysInfo::GetLsbReleaseValue("CHROMEOS_RELEASE_VERSION", &version);
    arc_cumulative_use_time_->Init(version);
  }
}

LoginMetrics::~LoginMetrics() {}

void LoginMetrics::SendConsumerAllowsNewUsers(bool allowed) {
  int uma_code = allowed ? ANY_USER_ALLOWED : ONLY_WHITELISTED_ALLOWED;
  metrics_lib_.SendEnumToUMA(kLoginConsumerAllowsNewUsersMetric, uma_code, 1);
}

void LoginMetrics::SendLoginUserType(bool dev_mode, bool incognito,
                                     bool owner) {
  int uma_code = LoginUserTypeCode(dev_mode, incognito, owner);
  metrics_lib_.SendEnumToUMA(kLoginUserTypeMetric, uma_code, NUM_TYPES - 1);
}

bool LoginMetrics::SendPolicyFilesStatus(const PolicyFilesStatus& status) {
  if (!base::PathExists(per_boot_flag_file_)) {
    metrics_lib_.SendEnumToUMA(kLoginPolicyFilesMetric,
                               LoginMetrics::PolicyFilesStatusCode(status),
                               kMaxPolicyFilesValue);
    bool created = base::WriteFile(per_boot_flag_file_, "", 0) == 0;
    PLOG_IF(WARNING, !created) << "Can't touch " << per_boot_flag_file_.value();
    return true;
  }
  return false;
}

void LoginMetrics::SendStateKeyGenerationStatus(
    StateKeyGenerationStatus status) {
  metrics_lib_.SendEnumToUMA(kLoginStateKeyGenerationStatus, status,
                             STATE_KEY_STATUS_COUNT);
}

void LoginMetrics::RecordStats(const char* tag) {
  bootstat_log(tag);
}

bool LoginMetrics::HasRecordedChromeExec() {
  return base::PathExists(base::FilePath(kChromeUptimeFile));
}

void LoginMetrics::StartTrackingArcUseTime() {
  if (arc_cumulative_use_time_)
    arc_cumulative_use_time_->Start();
}

void LoginMetrics::StopTrackingArcUseTime() {
  if (arc_cumulative_use_time_)
    arc_cumulative_use_time_->Stop();
}

// static
// Code for incognito, owner and any other user are 0, 1 and 2
// respectively in normal mode. In developer mode they are 3, 4 and 5.
int LoginMetrics::LoginUserTypeCode(bool dev_mode, bool guest, bool owner) {
  if (!dev_mode) {
    if (guest)
      return GUEST;
    if (owner)
      return OWNER;
    return OTHER;
  }
  // If we get here, we're in dev mode.
  if (guest)
    return DEV_GUEST;
  if (owner)
    return DEV_OWNER;
  return DEV_OTHER;
}

}  // namespace login_manager
