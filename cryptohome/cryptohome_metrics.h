// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_CRYPTOHOME_METRICS_H_
#define CRYPTOHOME_CRYPTOHOME_METRICS_H_

#include <string>

#include <base/files/file.h>
#include <base/time/time.h>
#include <metrics/metrics_library.h>

#include "cryptohome/le_credential_manager.h"
#include "cryptohome/migration_type.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_metrics.h"

namespace cryptohome {

// List of all the possible operation types. Used to construct the correct
// histogram while logging to UMA.
enum LECredOperationType {
  LE_CRED_OP_RESET_TREE = 0,
  LE_CRED_OP_INSERT,
  LE_CRED_OP_CHECK,
  LE_CRED_OP_RESET,
  LE_CRED_OP_REMOVE,
  LE_CRED_OP_SYNC,
  LE_CRED_OP_MAX,
};

// List of all possible actions taken within an LE Credential operation.
// Used to construct the correct histogram while logging to UMA.
enum LECredActionType {
  LE_CRED_ACTION_LOAD_FROM_DISK = 0,
  LE_CRED_ACTION_BACKEND,
  LE_CRED_ACTION_SAVE_TO_DISK,
  LE_CRED_ACTION_BACKEND_GET_LOG,
  LE_CRED_ACTION_BACKEND_REPLAY_LOG,
  LE_CRED_ACTION_MAX,
};

// Indication of whether EVKK's encryption is TPM backed.
// Entries should not be renumbered and numeric values should never be reused.
enum EvkkEncryptionType {
  kScryptBackedEncryption,
  kTpmBackedEncryption,
  kEvkkEncryptionTypeNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum CryptohomeError {
  kTpmFail = 1,
  kTcsKeyLoadFailed = 2,
  kTpmDefendLockRunning = 3,
  kDecryptAttemptButTpmKeyMissing = 4,
  kDecryptAttemptButTpmNotOwned = 5,
  kDecryptAttemptButTpmNotAvailable = 6,
  kDecryptAttemptButTpmKeyMismatch = 7,
  kDecryptAttemptWithTpmKeyFailed = 8,
  kCannotLoadTpmSrk = 9,
  kCannotReadTpmSrkPublic = 10,
  kCannotLoadTpmKey = 11,
  kCannotReadTpmPublicKey = 12,
  kTpmBadKeyProperty = 13,
  kLoadPkcs11TokenFailed = 14,
  kEncryptWithTpmFailed = 15,
  kTssCommunicationFailure = 16,
  kTssInvalidHandle = 17,
  kBothTpmAndScryptWrappedKeyset = 18,
  kEphemeralCleanUpFailed = 19,
  kTpmOutOfMemory = 20,
  kCryptohomeErrorNumBuckets,  // Must be the last entry.
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum TimerType {
  kAsyncMountTimer,       // Unused.
  kSyncMountTimer,        // Unused.
  kAsyncGuestMountTimer,  // Unused.
  kSyncGuestMountTimer,   // Unused.
  kTpmTakeOwnershipTimer,
  kPkcs11InitTimer,
  kMountExTimer,
  kDircryptoMigrationTimer,
  kDircryptoMinimalMigrationTimer,
  kOOPMountOperationTimer,
  kOOPMountCleanupTimer,
  kSessionUnlockTimer,
  kMountGuestExTimer,
  kPerformEphemeralMountTimer,
  kPerformMountTimer,
  kNumTimerTypes  // For the number of timer types.
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DictionaryAttackResetStatus {
  kResetNotNecessary,
  kResetAttemptSucceeded,
  kResetAttemptFailed,
  kDelegateNotAllowed,
  kDelegateNotAvailable,
  kCounterQueryFailed,
  kInvalidPcr0State,
  kDictionaryAttackResetStatusNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum ChecksumStatus {
  kChecksumOK,
  kChecksumDoesNotExist,
  kChecksumReadError,
  kChecksumMismatch,
  kChecksumOutOfSync,
  kChecksumStatusNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DircryptoMigrationStartStatus {
  kMigrationStarted = 1,
  kMigrationResumed = 2,
  kMigrationStartStatusNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DircryptoMigrationEndStatus {
  kNewMigrationFailedGeneric = 1,
  kNewMigrationFinished = 2,
  kResumedMigrationFailedGeneric = 3,
  kResumedMigrationFinished = 4,
  kNewMigrationFailedLowDiskSpace = 5,
  kResumedMigrationFailedLowDiskSpace = 6,
  // The detail of the "FileError" failures (the failed file operation,
  // error code, and the rough classification of the failed path) will be
  // reported in separate metrics, too. Since there's no good way to relate the
  // multi-dimensional metric however, we treat some combinations as special
  // cases and distinguish them here as well.
  kNewMigrationFailedFileError = 7,
  kResumedMigrationFailedFileError = 8,
  kNewMigrationFailedFileErrorOpenEIO = 9,
  kResumedMigrationFailedFileErrorOpenEIO = 10,
  kNewMigrationCancelled = 11,
  kResumedMigrationCancelled = 12,
  kMigrationEndStatusNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DircryptoMigrationFailedOperationType {
  kMigrationFailedAtOtherOperation = 1,
  kMigrationFailedAtOpenSourceFile = 2,
  kMigrationFailedAtOpenDestinationFile = 3,
  kMigrationFailedAtCreateLink = 4,
  kMigrationFailedAtDelete = 5,
  kMigrationFailedAtGetAttribute = 6,
  kMigrationFailedAtMkdir = 7,
  kMigrationFailedAtReadLink = 8,
  kMigrationFailedAtSeek = 9,
  kMigrationFailedAtSendfile = 10,
  kMigrationFailedAtSetAttribute = 11,
  kMigrationFailedAtStat = 12,
  kMigrationFailedAtSync = 13,
  kMigrationFailedAtTruncate = 14,
  kMigrationFailedAtOpenSourceFileNonFatal = 15,
  kMigrationFailedAtRemoveAttribute = 16,
  kMigrationFailedOperationTypeNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum DircryptoMigrationFailedPathType {
  kMigrationFailedUnderOther = 1,
  kMigrationFailedUnderAndroidOther = 2,
  kMigrationFailedUnderAndroidCache = 3,
  kMigrationFailedUnderDownloads = 4,
  kMigrationFailedUnderCache = 5,
  kMigrationFailedUnderGcache = 6,
  kMigrationFailedPathTypeNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class HomedirEncryptionType {
  kEcryptfs = 1,
  kDircrypto = 2,
  kHomedirEncryptionTypeNumBuckets
};

// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
enum class DiskCleanupProgress {
  kEphemeralUserProfilesCleaned = 1,
  kBrowserCacheCleanedAboveTarget = 2,
  kGoogleDriveCacheCleanedAboveTarget = 3,
  kGoogleDriveCacheCleanedAboveMinimum = 4,
  kAndroidCacheCleanedAboveTarget = 5,
  kAndroidCacheCleanedAboveMinimum = 6,
  kWholeUserProfilesCleanedAboveTarget = 7,
  kWholeUserProfilesCleaned = 8,
  kNoUnmountedCryptohomes = 9,
  kNumBuckets
};

// Add new deprecated function event here.
// These values are persisted to logs. Entries should not be renumbered and
// numeric values should never be reused.
// Note: All updates here must also update Chrome's enums.xml database.
// Please see this document for more details:
// https://chromium.googlesource.com/chromium/src/+/master/tools/metrics/histograms/
//
// You can view them live here:
// https://uma.googleplex.com/histograms/?histograms=Platform.Cryptohome.DeprecatedApiCalled
enum class DeprecatedApiEvent {
  kInitializeCastKey = 0,
  kGetBootAttribute,            // 1
  kSetBootAttribute,            // 2
  kFlushAndSignBootAttributes,  // 3
  kSignBootLockbox,             // 4
  kVerifyBootLockbox,           // 5
  kFinalizeBootLockbox,         // 6
  kTpmIsBeingOwned,             // 7
  kMaxValue,                    // 8
};

// List of the possible results of attempting a mount operation using the
// out-of-process mount helper.
enum class OOPMountOperationResult {
  kSuccess = 0,
  kFailedToStart = 1,
  kFailedToWriteRequestProtobuf = 2,
  kHelperProcessTimedOut = 3,
  kFailedToReadResponseProtobuf = 4,
  kMaxValue = kFailedToReadResponseProtobuf
};

// List of the possible results of attempting an unmount/mount clean-up
// using the out-of-process mount helper.
enum class OOPMountCleanupResult {
  kSuccess = 0,
  kFailedToPoke = 1,
  kFailedToWait = 2,
  kFailedToKill = 3,
  kMaxValue = kFailedToKill
};

// List of generic results of attestation-related operations. These entries
// should not be renumbered and numeric values should never be reused.
enum class AttestationOpsStatus {
  kSuccess = 0,
  kFailure = 1,
  kInvalidPcr0Value = 2,
  kMaxValue,
};

// Just to make sure I count correctly.
static_assert(static_cast<int>(DeprecatedApiEvent::kMaxValue) == 8,
              "DeprecatedApiEvent Enum miscounted");

// Cros events emitted by cryptohome.
const char kAttestationOriginSpecificIdentifiersExhausted[] =
    "Attestation.OriginSpecificExhausted";
const char kCryptohomeDoubleMount[] = "Cryptohome.DoubleMountRequest";

// Constants related to LE Credential UMA logging.
constexpr char kLEOpResetTree[] = ".ResetTree";
constexpr char kLEOpInsert[] = ".Insert";
constexpr char kLEOpCheck[] = ".Check";
constexpr char kLEOpReset[] = ".Reset";
constexpr char kLEOpRemove[] = ".Remove";
constexpr char kLEOpSync[] = ".Sync";
constexpr char kLEActionLoadFromDisk[] = ".LoadFromDisk";
constexpr char kLEActionBackend[] = ".Backend";
constexpr char kLEActionSaveToDisk[] = ".SaveToDisk";
constexpr char kLEActionBackendGetLog[] = ".BackendGetLog";
constexpr char kLEActionBackendReplayLog[] = ".BackendReplayLog";

// Attestation-related operations. Those are suffixes of the histogram
// kAttestationStatusHistogramPrefix defined in the .cc file.
constexpr char kAttestationDecryptDatabase[] = "DecryptDatabase";
constexpr char kAttestationMigrateDatabase[] = "MigrateDatabase";
constexpr char kAttestationPrepareForEnrollment[] = "PrepareForEnrollment";

// Initializes cryptohome metrics. If this is not called, all calls to Report*
// will have no effect.
void InitializeMetrics();

// Cleans up and returns cryptohome metrics to an uninitialized state.
void TearDownMetrics();

// Override the internally used MetricsLibrary for testing purpose.
void OverrideMetricsLibraryForTesting(MetricsLibraryInterface* lib);

// Reset the internally used MetricsLibrary for testing purpose. This is usually
// used with OverrideMetricsLibraryForTesting().
void ClearMetricsLibraryForTesting();

// The |encryption_type| value is reported to the
// "Cryptohome.EvkkEncryptionType" enum histogram.
void ReportEvkkEncryptionType(EvkkEncryptionType encryption_type);

// The |error| value is reported to the "Cryptohome.Errors" enum histogram.
void ReportCryptohomeError(CryptohomeError error);

// The |result| value is reported to the "Cryptohome.TpmResults" enum histogram.
void ReportTpmResult(TpmResult result);

// Cros events are translated to an enum and reported to the generic
// "Platform.CrOSEvent" enum histogram. The |event| string must be registered in
// metrics/metrics_library.cc:kCrosEventNames.
void ReportCrosEvent(const char* event);

// Starts a timer for the given |timer_type|.
void ReportTimerStart(TimerType timer_type);

// Stops a timer and reports in milliseconds. Timers are reported to the
// "Cryptohome.TimeTo*" histograms.
void ReportTimerStop(TimerType timer_type);

// Reports a status value on the "Platform.TPM.DictionaryAttackResetStatus"
// histogram.
void ReportDictionaryAttackResetStatus(DictionaryAttackResetStatus status);

// Reports a dictionary attack counter value to the
// "Platform.TPM.DictionaryAttackCounter" histogram.
void ReportDictionaryAttackCounter(int counter);

void ReportChecksum(ChecksumStatus status);

// Reports number of deleted user profiles to the
// "Cryptohome.DeletedUserProfiles" histogram.
void ReportDeletedUserProfiles(int user_profile_count);

// Reports total time taken by HomeDirs::FreeDiskSpace cleanup (milliseconds) to
// the "Cryptohome.FreeDiskSpaceTotalTime" histogram.
void ReportFreeDiskSpaceTotalTime(int ms);

// Reports total space freed by HomeDirs::FreeDiskSpace (in MiB) to
// the "Cryptohome.FreeDiskSpaceTotalTime" histogram.
void ReportFreeDiskSpaceTotalFreedInMb(int mb);

// Reports the time between HomeDirs::FreeDiskSpace cleanup calls (seconds) to
// the "Cryptohome.TimeBetweenFreeDiskSpace" histogram.
void ReportTimeBetweenFreeDiskSpace(int s);

// Reports removed GCache size by cryptohome to the
// "Cryptohome.FreedGCacheDiskSpaceInMb" histogram.
void ReportFreedGCacheDiskSpaceInMb(int mb);

// The |status| value is reported to the
// "Cryptohome.DircryptoMigrationStartStatus" (full migration)
// or the "Cryptohome.DircryptoMinimalMigrationStartStatus" (minimal migration)
// enum histogram.
void ReportDircryptoMigrationStartStatus(MigrationType migration_type,
                                         DircryptoMigrationStartStatus status);

// The |status| value is reported to the
// "Cryptohome.DircryptoMigrationEndStatus" (full migration)
// or the "Cryptohome.DircryptoMinimalMigrationEndStatus" (minimal migration)
// enum histogram.
void ReportDircryptoMigrationEndStatus(MigrationType migration_type,
                                       DircryptoMigrationEndStatus status);

// The |error_code| value is reported to the
// "Cryptohome.DircryptoMigrationFailedErrorCode"
// enum histogram.
void ReportDircryptoMigrationFailedErrorCode(base::File::Error error_code);

// The |type| value is reported to the
// "Cryptohome.DircryptoMigrationFailedOperationType"
// enum histogram.
void ReportDircryptoMigrationFailedOperationType(
    DircryptoMigrationFailedOperationType type);

// The |alerts| data set is reported to the
// "Platform.TPM.HardwareAlerts" enum histogram.
void ReportAlertsData(const Tpm::AlertsData& alerts);

// The |type| value is reported to the
// "Cryptohome.DircryptoMigrationFailedPathType"
// enum histogram.
void ReportDircryptoMigrationFailedPathType(
    DircryptoMigrationFailedPathType type);

// Reports the total byte count in MB to migrate to the
// "Cryptohome.DircryptoMigrationTotalByteCountInMb" histogram.
void ReportDircryptoMigrationTotalByteCountInMb(int total_byte_count_mb);

// Reports the total file count to migrate to the
// "Cryptohome.DircryptoMigrationTotalFileCount" histogram.
void ReportDircryptoMigrationTotalFileCount(int total_file_count);

// Reports which topmost priority was reached to fulfill a cleanup request
// to the "Cryptohome.DiskCleanupProgress" enum histogram.
void ReportDiskCleanupProgress(DiskCleanupProgress progress);

// The |type| value is reported to the "Cryptohome.HomedirEncryptionType" enum
// histogram.
void ReportHomedirEncryptionType(HomedirEncryptionType type);

// Reports the result of a Low Entropy (LE) Credential operation to the relevant
// LE Credential histogram.
void ReportLEResult(const char* type, const char* action, LECredError result);

// Reports the overall outcome of a Low Entropy (LE) Credential Sync operation
// to the "Cryptohome.LECredential.SyncOutcome" enum histogram.
void ReportLESyncOutcome(LECredError result);

// Reports the free space in MB when the migration fails and what the free space
// was initially when the migration was started.
void ReportDircryptoMigrationFailedNoSpace(int initial_migration_free_space_mb,
                                           int failure_free_space_mb);

// Reports the total size in bytes of the current xattrs already set on a file
// and the xattr that caused the setxattr call to fail.
void ReportDircryptoMigrationFailedNoSpaceXattrSizeInBytes(
    int total_xattr_size_bytes);

// Reports the total running time of a dbus request.
void ReportAsyncDbusRequestTotalTime(std::string task_name,
                                     base::TimeDelta running_time);

// Reports the total in-queue time of mount thread of a dbus request
void ReportAsyncDbusRequestInqueueTime(std::string task_name,
                                       base::TimeDelta running_time);

// Reports the amount of total tasks waiting in the queue of mount thread.
void ReportParallelTasks(int amount_of_task);

// Reports when a deprecated function that is exposed on the DBus is called.
// This is used to determine which deprecated function is truly dead code,
// and removing it will not trigger side effects.
void ReportDeprecatedApiCalled(DeprecatedApiEvent event);

// Reports the result of an out-of-process mount operation.
void ReportOOPMountOperationResult(OOPMountOperationResult result);

// Reports the result of an out-of-process cleanup operation.
void ReportOOPMountCleanupResult(OOPMountCleanupResult result);

// Reports the result of attestation-related operations. |operation| should be
// one of the suffixes of the histogram kAttestationStatusHistogramPrefix listed
// above.
void ReportAttestationOpsStatus(const std::string& operation,
                                AttestationOpsStatus status);

// Reports the result of an InvalidateDirCryptoKey operation.
void ReportInvalidateDirCryptoKeyResult(bool result);

// Initialization helper.
class ScopedMetricsInitializer {
 public:
  ScopedMetricsInitializer() { InitializeMetrics(); }
  ~ScopedMetricsInitializer() { TearDownMetrics(); }
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_CRYPTOHOME_METRICS_H_
