// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef CRASH_REPORTER_CRASH_SENDER_BASE_H_
#define CRASH_REPORTER_CRASH_SENDER_BASE_H_

#include <memory>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/files/file.h>
#include <base/files/file_path.h>
#include <base/time/clock.h>
#include <base/time/time.h>
#include <brillo/key_value_store.h>
#include <brillo/osrelease_reader.h>
#include <session_manager/dbus-proxies.h>

namespace util {

// Maximum time to wait for ensuring a meta file is complete.
constexpr base::TimeDelta kMaxHoldOffTime = base::TimeDelta::FromSeconds(30);

// Crash information obtained in ChooseAction().
struct CrashInfo {
  brillo::KeyValueStore metadata;
  base::FilePath payload_file;
  std::string payload_kind;
  // Last modification time of the associated .meta file
  base::Time last_modified;
};

// Testing hook. Set to true to force IsMock() to always return true. Easier
// than creating the mock file in internal tests (such as fuzz tests).
extern bool g_force_is_mock;

// Gets the base name of the path pointed by |key| in the given metadata.
// Returns an empty path if the key is not found.
base::FilePath GetBaseNameFromMetadata(const brillo::KeyValueStore& metadata,
                                       const std::string& key);

// Returns which kind of report from the given payload path. Returns an empty
// string if the kind is unknown.
std::string GetKindFromPayloadPath(const base::FilePath& payload_path);

// Parses |raw_metadata| into |metadata|. Keys in metadata are validated (keys
// should consist of expected characters). Returns true on success.
// The original contents of |metadata| will be lost.
bool ParseMetadata(const std::string& raw_metadata,
                   brillo::KeyValueStore* metadata);

// Returns true if the metadata is complete.
bool IsCompleteMetadata(const brillo::KeyValueStore& metadata);

// Records that the crash sending is done.
void RecordCrashDone();

// Returns true if mock is enabled.
bool IsMock();

// Computes a sleep time needed before attempting to send a new crash report.
// On success, returns true and stores the result in |sleep_time|. On error,
// returns false.
bool GetSleepTime(const base::FilePath& meta_file,
                  const base::TimeDelta& max_spread_time,
                  const base::TimeDelta& hold_off_time,
                  base::TimeDelta* sleep_time);

// Gets the client ID if it exists, otherwise it generates it, saves it and
// returns that new ID. If it is unable to create the directory for storage, the
// empty string is returned.
std::string GetClientId();

// This class assists us in recovering from crashes while processing crashes.
// When it is constructed, it attempts to create a ".processing" file for the
// given metadata file, and when it is destructed it removes it.
// If crash_sender crashes, or otherwise exits without running the destructor,
// the .processing file will still exist. ChooseAction uses the existence of
// this file to determine that the crash may be malformed and avoid processing
// it again.
class ScopedProcessingFile {
 public:
  explicit ScopedProcessingFile(const base::FilePath& meta_file);

  // Disallow copy and assign (and implicitly, move).
  ScopedProcessingFile(const ScopedProcessingFile& other) = delete;
  ScopedProcessingFile& operator=(const ScopedProcessingFile& other) = delete;

  ~ScopedProcessingFile();

 private:
  const base::FilePath processing_file_;
};

// Base class for crash reading functionality. Used by both crash sender and
// crash serializer.
class SenderBase {
 public:
  struct Options {
    // Session manager client for locating the user-specific crash directories.
    org::chromium::SessionManagerInterfaceProxyInterface*
        session_manager_proxy = nullptr;

    // Do not send the crash report until the meta file is at least this old.
    // This avoids problems with crash reports being sent out while they are
    // still being written.
    base::TimeDelta hold_off_time = kMaxHoldOffTime;

    // Alternate sleep function for unit testing.
    base::Callback<void(base::TimeDelta)> sleep_function;
  };

  SenderBase(std::unique_ptr<base::Clock> clock, const Options& options);

  virtual ~SenderBase() = default;

  // Actions returned by ChooseAction().
  enum Action {
    kRemove,  // Should remove the crash report.
    kIgnore,  // Should ignore (keep) the crash report.
    kSend,    // Should send the crash report.
  };

  // These values are persisted to logs. Entries should not be renumbered and
  // numeric values should never be reused
  enum CrashRemoveReason {
    kTotalRemoval = 0,
    kNotOfficialImage = 1,
    kNoMetricsConsent = 2,
    kProcessingFileExists = 3,
    kLargeMetaFile = 4,
    kUnparseableMetaFile = 5,
    kPayloadUnspecified = 6,
    kPayloadAbsolute = 7,
    kPayloadNonexistent = 8,
    kPayloadKindUnknown = 9,
    kOSVersionTooOld = 10,
    kOldIncompleteMeta = 11,
    kFinishedUploading = 12,
    kAlreadyUploaded = 13,
    // Keep kSendReasonCount one larger than any other enum value.
    kSendReasonCount = 14,
  };

  // Lock the lock file so no concurrently running process can access the
  // disk files. Dies if lock file cannot be acquired after a delay.
  //
  // Returns the File object holding the lock.
  base::File AcquireLockFileOrDie();

  // Get a list of all directories that might hold user-specific crashes.
  std::vector<base::FilePath> GetUserCrashDirectories();

  // For tests only, crash while sending crashes.
  void SetCrashDuringSendForTesting(bool crash) {
    crash_during_testing_ = crash;
  }

 protected:
  // Looks through |keys| in the os-release data using brillo::OsReleaseReader.
  // Keys are searched in order until a value is found. Returns the value in
  // the Optional if found, otherwise the Optional is empty.
  base::Optional<std::string> GetOsReleaseValue(
      const std::vector<std::string>& keys);

  // Do a minimal evaluation of the given meta file, only performing basic
  // validation (e.g. that it's fully written, that the payload field is valid,
  // etc).
  // In particular, this does _not_ check metrics consent, guest mode, or
  // whether the crash is already uploaded.
  // Arguments:
  //  |meta_file| - The path to the metadata file to process.
  //  |allow_old_os_timestamps| - True iff we should return kSend for metadata
  //                              files created on old (>6 mo) OS versions
  //  |reason| - output parameter. Human-readable description of the reason for
  //             the given action. useful for logs.
  //  |info| - output parameter. CrashInfo struct created while evaluating meta
  //           file.
  //  |processing_file| - optional output parameter. If non-null, a
  //                      ScopedProcessingFile will be placed into it.
  //                      This file should remain in scope during all
  //                      additional processing of the meta file.
  Action EvaluateMetaFileMinimal(
      const base::FilePath& meta_file,
      bool allow_old_os_timestamps,
      std::string* reason,
      CrashInfo* info,
      std::unique_ptr<ScopedProcessingFile>* processing_file);

  // Record the reason for removing a crash.
  virtual void RecordCrashRemoveReason(CrashRemoveReason reason) = 0;

  // Makes sure we have the DBus object initialized and connected.
  void EnsureDBusIsReady();

  // These are accessed by child classes.
  base::Callback<void(base::TimeDelta)> sleep_function_;
  scoped_refptr<dbus::Bus> bus_;
  bool crash_during_testing_ = false;
  const base::TimeDelta hold_off_time_;

 private:
  std::unique_ptr<base::Clock> clock_;
  std::unique_ptr<org::chromium::SessionManagerInterfaceProxyInterface>
      session_manager_proxy_;
  std::unique_ptr<brillo::OsReleaseReader> os_release_reader_;
};

}  // namespace util

#endif  // CRASH_REPORTER_CRASH_SENDER_BASE_H_
