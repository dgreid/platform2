// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/cryptohome_metrics.h"

#include <string>

#include <base/logging.h>
#include <base/macros.h>
#include <base/stl_util.h>
#include <metrics/metrics_library.h>
#include <metrics/timer.h>

#include "cryptohome/tpm_metrics.h"

namespace {

struct TimerHistogramParams {
  const char* metric_name;
  int min_sample;
  int max_sample;
  int num_buckets;
};

constexpr char kEvkkEncryptionTypeHistogram[] = "Cryptohome.EvkkEncryptionType";
constexpr char kCryptohomeErrorHistogram[] = "Cryptohome.Errors";
constexpr char kDictionaryAttackResetStatusHistogram[] =
    "Platform.TPM.DictionaryAttackResetStatus";
constexpr char kDictionaryAttackCounterHistogram[] =
    "Platform.TPM.DictionaryAttackCounter";
constexpr int kDictionaryAttackCounterNumBuckets = 100;
constexpr char kChecksumStatusHistogram[] = "Cryptohome.ChecksumStatus";
constexpr char kCryptohomeTpmResultsHistogram[] = "Cryptohome.TpmResults";
constexpr char kCryptohomeDeletedUserProfilesHistogram[] =
    "Cryptohome.DeletedUserProfiles";
constexpr char kCryptohomeGCacheFreedDiskSpaceInMbHistogram[] =
    "Cryptohome.GCache.FreedDiskSpaceInMb";
constexpr char kCryptohomeFreeDiskSpaceTotalTimeHistogram[] =
    "Cryptohome.FreeDiskSpaceTotalTime2";
constexpr char kCryptohomeFreeDiskSpaceTotalFreedInMbHistogram[] =
    "Cryptohome.FreeDiskSpaceTotalFreedInMb";
constexpr char kCryptohomeTimeBetweenFreeDiskSpaceHistogram[] =
    "Cryptohome.TimeBetweenFreeDiskSpace";
constexpr char kCryptohomeDircryptoMigrationStartStatusHistogram[] =
    "Cryptohome.DircryptoMigrationStartStatus";
constexpr char kCryptohomeDircryptoMigrationEndStatusHistogram[] =
    "Cryptohome.DircryptoMigrationEndStatus";
constexpr char kCryptohomeDircryptoMinimalMigrationStartStatusHistogram[] =
    "Cryptohome.DircryptoMinimalMigrationStartStatus";
constexpr char kCryptohomeDircryptoMinimalMigrationEndStatusHistogram[] =
    "Cryptohome.DircryptoMinimalMigrationEndStatus";
constexpr char kCryptohomeDircryptoMigrationFailedErrorCodeHistogram[] =
    "Cryptohome.DircryptoMigrationFailedErrorCode";
constexpr char kCryptohomeDircryptoMigrationFailedOperationTypeHistogram[] =
    "Cryptohome.DircryptoMigrationFailedOperationType";
constexpr char kCryptohomeDircryptoMigrationFailedPathTypeHistogram[] =
    "Cryptohome.DircryptoMigrationFailedPathType";
constexpr char kCryptohomeDircryptoMigrationTotalByteCountInMbHistogram[] =
    "Cryptohome.DircryptoMigrationTotalByteCountInMb";
constexpr char kCryptohomeDircryptoMigrationTotalFileCountHistogram[] =
    "Cryptohome.DircryptoMigrationTotalFileCount";
constexpr char kCryptohomeDiskCleanupProgressHistogram[] =
    "Cryptohome.DiskCleanupProgress";
constexpr char kCryptohomeLEResultHistogramPrefix[] = "Cryptohome.LECredential";
constexpr char kCryptohomeAsyncDBusRequestsPrefix[] =
    "Cryptohome.AsyncDBusRequest.";
constexpr char kCryptohomeAsyncDBusRequestsInqueueTimePrefix[] =
    "Cryptohome.AsyncDBusRequest.Inqueue.";
constexpr char kCryptohomeParallelTasksPrefix[] = "Cryptohome.ParallelTasks";
constexpr char kCryptohomeLESyncOutcomeHistogramSuffix[] = ".SyncOutcome";
constexpr char kHomedirEncryptionTypeHistogram[] =
    "Cryptohome.HomedirEncryptionType";
constexpr char kDircryptoMigrationNoSpaceFailureFreeSpaceInMbHistogram[] =
    "Cryptohome.DircryptoMigrationNoSpaceFailureFreeSpaceInMb";
constexpr char kDircryptoMigrationInitialFreeSpaceInMbHistogram[] =
    "Cryptohome.DircryptoMigrationInitialFreeSpaceInMb";
constexpr char kDircryptoMigrationNoSpaceXattrSizeInBytesHistogram[] =
    "Cryptohome.DircryptoMigrationNoSpaceXattrSizeInBytes";
constexpr char kTpmAlertsHistogram[] = "Platform.TPM.HardwareAlerts";
constexpr char kOOPMountOperationResultHistogram[] =
    "Cryptohome.OOPMountOperationResult";
constexpr char kOOPMountCleanupResultHistogram[] =
    "Cryptohome.OOPMountCleanupResult";
constexpr char kInvalidateDirCryptoKeyResultHistogram[] =
    "Cryptohome.InvalidateDirCryptoKeyResult";

// Histogram parameters. This should match the order of 'TimerType'.
// Min and max samples are in milliseconds.
const TimerHistogramParams kTimerHistogramParams[] = {
    {"Cryptohome.TimeToMountAsync", 0, 4000, 50},
    {"Cryptohome.TimeToMountSync", 0, 4000, 50},
    {"Cryptohome.TimeToMountGuestAsync", 0, 4000, 50},
    {"Cryptohome.TimeToMountGuestSync", 0, 4000, 50},
    {"Cryptohome.TimeToTakeTpmOwnership", 0, 100000, 50},
    // A note on the PKCS#11 initialization time:
    // Max sample for PKCS#11 initialization time is 100s; we are interested
    // in recording the very first PKCS#11 initialization time, which may be a
    // lengthy one. Subsequent initializations are fast (under 1s) because they
    // just check if PKCS#11 was previously initialized, returning immediately.
    // These will all fall into the first histogram bucket.
    {"Cryptohome.TimeToInitPkcs11", 1000, 100000, 50},
    {"Cryptohome.TimeToMountEx", 0, 4000, 50},
    // Ext4 crypto migration is expected to takes few minutes in a fast case,
    // and with many tens of thousands of files it may take hours.
    {"Cryptohome.TimeToCompleteDircryptoMigration", 1000, 10 * 60 * 60 * 1000,
     50},
    // Minimal migration is expected to take few seconds in a fast case,
    // and minutes in the worst case if we forgot to blocklist files.
    {"Cryptohome.TimeToCompleteDircryptoMinimalMigration", 200, 2 * 60 * 1000,
     50},
    // The out-of-process mount operation will time out after 3 seconds.
    {"Cryptohome.TimeToPerformOOPMountOperation", 0, 3000, 50},
    // The out-of-process cleanup operation includes a call to waitpid(2) with
    // a 1-second timeout, so make the max sample a bit higher than that.
    {"Cryptohome.TimeToPerformOOPMountCleanup", 0, 1100, 50},
    // Latency of the LegacyUserSession::Verify operation that gets invoked on
    // session unlock.
    {"Cryptohome.TimeSessionUnlock", 0, 4000, 50},
    {"Cryptohome.TimeToMountGuestEx", 0, 4000, 50},
    // This is only being reported from the out-of-process helper so it's
    // covered by the same 3-second timeout.
    {"Cryptohome.TimeToPerformEphemeralMount", 0, 3000, 50},
    // Non-ephemeral mounts are currently mounted in-process but it makes sense
    // to keep the same scale for them as ephemeral mounts.
    {"Cryptohome.TimeToPerformMount", 0, 3000, 50},
};

static_assert(base::size(kTimerHistogramParams) == cryptohome::kNumTimerTypes,
              "kTimerHistogramParams out of sync with enum TimerType");

constexpr char kCryptohomeDeprecatedApiHistogramName[] =
    "Cryptohome.DeprecatedApiCalled";

constexpr char kAttestationStatusHistogramPrefix[] = "Hwsec.Attestation.Status";

MetricsLibraryInterface* g_metrics = NULL;
chromeos_metrics::TimerReporter* g_timers[cryptohome::kNumTimerTypes] = {NULL};

chromeos_metrics::TimerReporter* GetTimer(cryptohome::TimerType timer_type) {
  if (!g_timers[timer_type]) {
    g_timers[timer_type] = new chromeos_metrics::TimerReporter(
        kTimerHistogramParams[timer_type].metric_name,
        kTimerHistogramParams[timer_type].min_sample,
        kTimerHistogramParams[timer_type].max_sample,
        kTimerHistogramParams[timer_type].num_buckets);
  }
  return g_timers[timer_type];
}

}  // namespace

namespace cryptohome {

void InitializeMetrics() {
  g_metrics = new MetricsLibrary();
  chromeos_metrics::TimerReporter::set_metrics_lib(g_metrics);
}

void TearDownMetrics() {
  if (g_metrics) {
    chromeos_metrics::TimerReporter::set_metrics_lib(NULL);
    delete g_metrics;
    g_metrics = NULL;
  }
  for (int i = 0; i < kNumTimerTypes; ++i) {
    if (g_timers[i]) {
      delete g_timers[i];
    }
  }
}

void OverrideMetricsLibraryForTesting(MetricsLibraryInterface* lib) {
  g_metrics = lib;
}

void ClearMetricsLibraryForTesting() {
  g_metrics = nullptr;
}

void ReportEvkkEncryptionType(EvkkEncryptionType encryption_type) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kEvkkEncryptionTypeHistogram, encryption_type,
                           kEvkkEncryptionTypeNumBuckets);
}

void ReportCryptohomeError(CryptohomeError error) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeErrorHistogram, error,
                           kCryptohomeErrorNumBuckets);
}

void ReportTpmResult(TpmResult result) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeTpmResultsHistogram, result,
                           kTpmResultNumberOfBuckets);
}

void ReportCrosEvent(const char* event) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendCrosEventToUMA(event);
}

void ReportTimerStart(TimerType timer_type) {
  if (!g_metrics) {
    return;
  }
  chromeos_metrics::TimerReporter* timer = GetTimer(timer_type);
  if (!timer) {
    return;
  }
  timer->Start();
}

void ReportTimerStop(TimerType timer_type) {
  if (!g_metrics) {
    return;
  }
  chromeos_metrics::TimerReporter* timer = GetTimer(timer_type);
  bool success = (timer && timer->HasStarted() && timer->Stop() &&
                  timer->ReportMilliseconds());
  if (!success) {
    LOG(WARNING) << "Timer " << kTimerHistogramParams[timer_type].metric_name
                 << " failed to report.";
  }
}

void ReportDictionaryAttackResetStatus(DictionaryAttackResetStatus status) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kDictionaryAttackResetStatusHistogram, status,
                           kDictionaryAttackResetStatusNumBuckets);
}

void ReportDictionaryAttackCounter(int counter) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kDictionaryAttackCounterHistogram, counter,
                           kDictionaryAttackCounterNumBuckets);
}

void ReportChecksum(ChecksumStatus status) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kChecksumStatusHistogram, status,
                           kChecksumStatusNumBuckets);
}

void ReportFreedGCacheDiskSpaceInMb(int mb) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendToUMA(kCryptohomeGCacheFreedDiskSpaceInMbHistogram, mb,
                       10 /* 10 MiB minimum */, 1024 * 10 /* 10 GiB maximum */,
                       50 /* number of buckets */);
}

void ReportDeletedUserProfiles(int user_profile_count) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendToUMA(kCryptohomeDeletedUserProfilesHistogram,
                       user_profile_count, 1 /* minimum */, 100 /* maximum */,
                       20 /* number of buckets */);
}

void ReportFreeDiskSpaceTotalTime(int ms) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendToUMA(kCryptohomeFreeDiskSpaceTotalTimeHistogram, ms, 1,
                       60 * 1000, 50);
}

void ReportFreeDiskSpaceTotalFreedInMb(int mb) {
  if (!g_metrics) {
    return;
  }
  constexpr int kMin = 1, kMax = 1024 * 10, /* 10 GiB maximum */
      kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeFreeDiskSpaceTotalFreedInMbHistogram, mb,
                       kMin, kMax, kNumBuckets);
}

void ReportTimeBetweenFreeDiskSpace(int s) {
  if (!g_metrics) {
    return;
  }

  constexpr int kMin = 1, kMax = 86400, /* seconds in a day */
      kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeTimeBetweenFreeDiskSpaceHistogram, s, kMin,
                       kMax, kNumBuckets);
}

void ReportDircryptoMigrationStartStatus(MigrationType migration_type,
                                         DircryptoMigrationStartStatus status) {
  if (!g_metrics) {
    return;
  }
  const char* metric =
      migration_type == MigrationType::FULL
          ? kCryptohomeDircryptoMigrationStartStatusHistogram
          : kCryptohomeDircryptoMinimalMigrationStartStatusHistogram;
  g_metrics->SendEnumToUMA(metric, status, kMigrationStartStatusNumBuckets);
}

void ReportDircryptoMigrationEndStatus(MigrationType migration_type,
                                       DircryptoMigrationEndStatus status) {
  if (!g_metrics) {
    return;
  }
  const char* metric =
      migration_type == MigrationType::FULL
          ? kCryptohomeDircryptoMigrationEndStatusHistogram
          : kCryptohomeDircryptoMinimalMigrationEndStatusHistogram;
  g_metrics->SendEnumToUMA(metric, status, kMigrationEndStatusNumBuckets);
}

void ReportDircryptoMigrationFailedErrorCode(base::File::Error error_code) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(
      kCryptohomeDircryptoMigrationFailedErrorCodeHistogram, -error_code,
      -base::File::FILE_ERROR_MAX);
}

void ReportDircryptoMigrationFailedOperationType(
    DircryptoMigrationFailedOperationType type) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(
      kCryptohomeDircryptoMigrationFailedOperationTypeHistogram, type,
      kMigrationFailedOperationTypeNumBuckets);
}

void ReportAlertsData(const Tpm::AlertsData& alerts) {
  if (!g_metrics) {
    return;
  }

  for (int i = 0; i < base::size(alerts.counters); i++) {
    uint16_t counter = alerts.counters[i];
    if (counter) {
      LOG(INFO) << "TPM alert of type " << i << " reported " << counter
                << " time(s)";
    }
    for (int c = 0; c < counter; c++) {
      g_metrics->SendEnumToUMA(kTpmAlertsHistogram, i,
                               base::size(alerts.counters));
    }
  }
}

void ReportDircryptoMigrationFailedPathType(
    DircryptoMigrationFailedPathType type) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeDircryptoMigrationFailedPathTypeHistogram,
                           type, kMigrationFailedPathTypeNumBuckets);
}

void ReportDircryptoMigrationTotalByteCountInMb(int total_byte_count_mb) {
  if (!g_metrics) {
    return;
  }
  constexpr int kMin = 1, kMax = 1024 * 1024, kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeDircryptoMigrationTotalByteCountInMbHistogram,
                       total_byte_count_mb, kMin, kMax, kNumBuckets);
}

void ReportDircryptoMigrationTotalFileCount(int total_file_count) {
  if (!g_metrics) {
    return;
  }
  constexpr int kMin = 1, kMax = 100000000, kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeDircryptoMigrationTotalFileCountHistogram,
                       total_file_count, kMin, kMax, kNumBuckets);
}

void ReportDiskCleanupProgress(DiskCleanupProgress progress) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(kCryptohomeDiskCleanupProgressHistogram,
                           static_cast<int>(progress),
                           static_cast<int>(DiskCleanupProgress::kNumBuckets));
}

void ReportHomedirEncryptionType(HomedirEncryptionType type) {
  if (!g_metrics) {
    return;
  }
  g_metrics->SendEnumToUMA(
      kHomedirEncryptionTypeHistogram, static_cast<int>(type),
      static_cast<int>(
          HomedirEncryptionType::kHomedirEncryptionTypeNumBuckets));
}

void ReportLEResult(const char* type, const char* action, LECredError result) {
  if (!g_metrics) {
    return;
  }

  std::string hist_str = std::string(kCryptohomeLEResultHistogramPrefix)
                             .append(type)
                             .append(action);

  g_metrics->SendEnumToUMA(hist_str, result, LE_CRED_ERROR_MAX);
}

void ReportLESyncOutcome(LECredError result) {
  if (!g_metrics) {
    return;
  }

  std::string hist_str = std::string(kCryptohomeLEResultHistogramPrefix)
                             .append(kCryptohomeLESyncOutcomeHistogramSuffix);

  g_metrics->SendEnumToUMA(hist_str, result, LE_CRED_ERROR_MAX);
}

void ReportDircryptoMigrationFailedNoSpace(int initial_migration_free_space_mb,
                                           int failure_free_space_mb) {
  if (!g_metrics) {
    return;
  }
  constexpr int kMin = 1, kMax = 1024 * 1024, kNumBuckets = 50;
  g_metrics->SendToUMA(kDircryptoMigrationInitialFreeSpaceInMbHistogram,
                       initial_migration_free_space_mb, kMin, kMax,
                       kNumBuckets);
  g_metrics->SendToUMA(kDircryptoMigrationNoSpaceFailureFreeSpaceInMbHistogram,
                       failure_free_space_mb, kMin, kMax, kNumBuckets);
}

void ReportDircryptoMigrationFailedNoSpaceXattrSizeInBytes(
    int total_xattr_size_bytes) {
  if (!g_metrics) {
    return;
  }
  constexpr int kMin = 1, kMax = 1024 * 1024, kNumBuckets = 50;
  g_metrics->SendToUMA(kDircryptoMigrationNoSpaceXattrSizeInBytesHistogram,
                       total_xattr_size_bytes, kMin, kMax, kNumBuckets);
}

void ReportParallelTasks(int amount_of_task) {
  if (!g_metrics) {
    return;
  }

  constexpr int kMin = 1, kMax = 50, kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeParallelTasksPrefix, amount_of_task, kMin,
                       kMax, kNumBuckets);
}

void ReportAsyncDbusRequestTotalTime(std::string task_name,
                                     base::TimeDelta running_time) {
  if (!g_metrics) {
    return;
  }

  // 3 mins as maximum
  constexpr int kMin = 1, kMax = 3 * 60 * 1000, kNumBuckets = 50;
  g_metrics->SendToUMA(kCryptohomeAsyncDBusRequestsPrefix + task_name,
                       running_time.InMilliseconds(), kMin, kMax, kNumBuckets);
}

void ReportAsyncDbusRequestInqueueTime(std::string task_name,
                                       base::TimeDelta running_time) {
  if (!g_metrics) {
    return;
  }

  // 3 mins as maximum, 3 secs of interval
  constexpr int kMin = 1, kMax = 3 * 60 * 1000, kNumBuckets = 3 * 20;
  g_metrics->SendToUMA(
      kCryptohomeAsyncDBusRequestsInqueueTimePrefix + task_name,
      running_time.InMilliseconds(), kMin, kMax, kNumBuckets);
}

void ReportDeprecatedApiCalled(DeprecatedApiEvent event) {
  if (!g_metrics) {
    return;
  }

  constexpr auto max_event = static_cast<int>(DeprecatedApiEvent::kMaxValue);
  g_metrics->SendEnumToUMA(kCryptohomeDeprecatedApiHistogramName,
                           static_cast<int>(event),
                           static_cast<int>(max_event));
}

void ReportOOPMountOperationResult(OOPMountOperationResult result) {
  if (!g_metrics) {
    return;
  }

  constexpr auto max_event =
      static_cast<int>(OOPMountOperationResult::kMaxValue);
  g_metrics->SendEnumToUMA(kOOPMountOperationResultHistogram,
                           static_cast<int>(result),
                           static_cast<int>(max_event));
}

void ReportOOPMountCleanupResult(OOPMountCleanupResult result) {
  if (!g_metrics) {
    return;
  }

  constexpr auto max_event = static_cast<int>(OOPMountCleanupResult::kMaxValue);
  g_metrics->SendEnumToUMA(kOOPMountCleanupResultHistogram,
                           static_cast<int>(result),
                           static_cast<int>(max_event));
}

void ReportAttestationOpsStatus(const std::string& operation,
                                AttestationOpsStatus status) {
  if (!g_metrics) {
    return;
  }

  const std::string histogram =
      std::string(kAttestationStatusHistogramPrefix) + "." + operation;
  g_metrics->SendEnumToUMA(histogram, static_cast<int>(status),
                           static_cast<int>(AttestationOpsStatus::kMaxValue));
}

void ReportInvalidateDirCryptoKeyResult(bool result) {
  if (!g_metrics) {
    return;
  }

  g_metrics->SendBoolToUMA(kInvalidateDirCryptoKeyResultHistogram, result);
}

}  // namespace cryptohome
