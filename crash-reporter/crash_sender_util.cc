// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_sender_util.h"

#include <inttypes.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/guid.h>
#include <base/json/json_writer.h>
#include <base/rand_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/threading/platform_thread.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>
#include <brillo/http/http_proxy.h>
#include <brillo/http/http_transport.h>
#include <brillo/http/http_utils.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>

#include "crash-reporter/crash_sender.pb.h"
#include "crash-reporter/crash_sender_paths.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/util.h"

namespace util {

bool g_force_is_mock = false;
bool g_force_is_mock_successful = false;

namespace {

// URL to send official build crash reports to.
constexpr char kReportUploadProdUrl[] = "https://clients2.google.com/cr/report";
// URL to send test/dev build crash reports to.
constexpr char kReportUploadStagingUrl[] =
    "https://clients2.google.com/cr/staging_report";
constexpr char kUndefined[] = "undefined";
constexpr char kChromeOsProduct[] = "ChromeOS";
constexpr char kUploadVarPrefix[] = "upload_var_";
constexpr char kUploadTextPrefix[] = "upload_text_";
constexpr char kUploadFilePrefix[] = "upload_file_";
constexpr char kOsTimestamp[] = "os_millis";
constexpr char kAlreadyUploadedExt[] = ".alreadyuploaded";

// Keys used in uploads.log file. (All timestamps are measured in seconds.)
constexpr char kJsonLogKeyUploadId[] = "upload_id";
constexpr char kJsonLogKeyUploadTime[] = "upload_time";
constexpr char kJsonLogKeyLocalId[] = "local_id";
constexpr char kJsonLogKeyCaptureTime[] = "capture_time";
constexpr char kJsonLogKeyState[] = "state";
constexpr char kJsonLogKeySource[] = "source";

// Keys used in CrashDetails::metadata.
constexpr char kMetadataKeyCaptureTimeMillis[] = "upload_var_reportTimeMillis";
constexpr char kMetadataKeySource[] = "exec_name";

// Values used for kJsonLogKeySource.
constexpr char kMetadataValueRedacted[] = "REDACTED";

// Length of the client ID. This is a standard GUID which has the dashes
// removed.
constexpr size_t kClientIdLength = 32U;

// Buffer size for reading a meta file into memory, in bytes.
constexpr size_t kMaxMetaFileSize = 1024 * 1024;

// Must match testModeSuccessful in the tast-test chrome_crash_loop.go.
constexpr char kTestModeSuccessful[] =
    "Test Mode: Logging success and exiting instead of actually uploading";

// Returns true if the given report kind is known.
// TODO(satorux): Move collector constants to a common file.
bool IsKnownKind(const std::string& kind) {
  return (kind == "minidump" || kind == "kcrash" || kind == "log" ||
          kind == "devcore" || kind == "eccrash" || kind == "bertdump");
}

// Returns true if the given key is valid for crash metadata.
bool IsValidKey(const std::string& key) {
  if (key.empty())
    return false;

  for (const char c : key) {
    if (!(base::IsAsciiAlpha(c) || base::IsAsciiDigit(c) || c == '_' ||
          c == '-' || c == '.')) {
      return false;
    }
  }

  return true;
}

// Converts metadata into CrashInfo.
void MetadataToCrashInfo(const brillo::KeyValueStore& metadata,
                         CrashInfo* info) {
  info->payload_file = GetBaseNameFromMetadata(metadata, "payload");
  info->payload_kind = GetKindFromPayloadPath(info->payload_file);
}

}  // namespace

void ParseCommandLine(int argc,
                      const char* const* argv,
                      CommandLineFlags* flags) {
  DEFINE_int32(max_spread_time, kMaxSpreadTimeInSeconds,
               "Max time in secs to sleep before sending (0 to send now)");
  DEFINE_string(crash_directory, "",
                "If set, upload only crashes in this directory.");
  const std::string ignore_rate_limits_description = base::StringPrintf(
      "Ignore normal limit of %d crash uploads per day", kMaxCrashRate);
  DEFINE_bool(ignore_rate_limits, false,
              ignore_rate_limits_description.c_str());
  const std::string ignore_hold_off_time_description = base::StringPrintf(
      "Assume all crash reports are completely written to disk. Do not "
      "wait %" PRId64 " seconds after meta file is written to start sending.",
      kMaxHoldOffTime.InSeconds());
  DEFINE_bool(ignore_hold_off_time, false,
              ignore_hold_off_time_description.c_str());
  DEFINE_bool(dev, false,
              "Send crash reports regardless of image/build type "
              "and upload them to the staging server instead.");
  DEFINE_bool(ignore_pause_file, false,
              "Ignore the existence of the pause file and run anyways");
  DEFINE_bool(test_mode, false,
              "Do not upload crashes; instead, log a special message if the "
              "crash is valid. Used by tast test ChromeCrashLoop.");
  DEFINE_bool(delete_crashes, true,
              "Instead of removing crashes after uploading them, create a new "
              "file indicating that they have been uploaded. Used by "
              "integration testing frameworks to provide data to the crash "
              "server while also capturing crashes in a framework-specific "
              "way.");
  brillo::FlagHelper::Init(argc, argv, "Chromium OS Crash Sender");
  if (FLAGS_max_spread_time < 0) {
    LOG(ERROR) << "Invalid value for max spread time: "
               << FLAGS_max_spread_time;
    exit(EXIT_FAILURE);
  }
  flags->max_spread_time = base::TimeDelta::FromSeconds(FLAGS_max_spread_time);
  flags->crash_directory = FLAGS_crash_directory;
  flags->ignore_rate_limits = FLAGS_ignore_rate_limits;
  flags->ignore_hold_off_time = FLAGS_ignore_hold_off_time;
  flags->allow_dev_sending = FLAGS_dev;
  flags->ignore_pause_file = FLAGS_ignore_pause_file;
  flags->test_mode = FLAGS_test_mode;
  flags->delete_crashes = FLAGS_delete_crashes;
  if (flags->test_mode) {
    // The pause file is intended to pause the cronjob crash_sender during
    // tests, not the crash_sender invoked by the test code.
    flags->ignore_pause_file = true;
  }
}

void RecordCrashDone() {
  if (IsMock()) {
    // For testing purposes, emit a message to log so that we
    // know when the test has received all the messages from this run.
    // The string is referenced in
    // third_party/autotest/files/client/cros/crash/crash_test.py and
    // platform/tast-tests/src/chromiumos/tast/local/crash/sender.go
    LOG(INFO) << "crash_sender done. (mock)";
  }
}

bool IsMock() {
  if (g_force_is_mock) {
    return true;
  }
  return base::PathExists(
      paths::GetAt(paths::kSystemRunStateDirectory, paths::kMockCrashSending));
}

bool IsMockSuccessful() {
  if (g_force_is_mock_successful) {
    return true;
  }
  int64_t file_size;
  return base::GetFileSize(paths::GetAt(paths::kSystemRunStateDirectory,
                                        paths::kMockCrashSending),
                           &file_size) &&
         !file_size;
}

bool DoesPauseFileExist() {
  return base::PathExists(paths::Get(paths::kPauseCrashSending));
}

std::string GetImageType() {
  if (util::IsTestImage())
    return "test";
  else if (util::IsDeveloperImage())
    return "dev";
  else if (IsMock() && !IsMockSuccessful())
    return "mock-fail";
  else
    return "";
}

base::FilePath GetBasePartOfCrashFile(const base::FilePath& file_name) {
  std::vector<std::string> components;
  file_name.GetComponents(&components);

  std::vector<std::string> parts = base::SplitString(
      components.back(), ".", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  if (parts.size() < 4) {
    LOG(ERROR) << "Unexpected file name format: " << file_name.value();
    return file_name;
  }

  parts.resize(4);
  const std::string base_name = base::JoinString(parts, ".");

  if (components.size() == 1)
    return base::FilePath(base_name);
  return file_name.DirName().Append(base_name);
}

void RemoveOrphanedCrashFiles(const base::FilePath& crash_dir) {
  base::FileEnumerator iter(crash_dir, true /* recursive */,
                            base::FileEnumerator::FILES, "*");
  for (base::FilePath file = iter.Next(); !file.empty(); file = iter.Next()) {
    // Get the meta data file path.
    const base::FilePath meta_file =
        base::FilePath(GetBasePartOfCrashFile(file).value() + ".meta");

    // Check how old the file is.
    base::File::Info info;
    if (!base::GetFileInfo(file, &info)) {
      PLOG(WARNING) << "Failed to get file info: " << file.value();
      continue;
    }
    base::TimeDelta delta = base::Time::Now() - info.last_modified;

    if (!base::PathExists(meta_file) && delta.InHours() >= 24) {
      LOG(INFO) << "Removing old orphaned file: " << file.value();
      if (!base::DeleteFile(file, false /* recursive */))
        PLOG(WARNING) << "Failed to remove " << file.value();
    }
  }
}

void SortReports(std::vector<MetaFile>* reports) {
  std::sort(reports->begin(), reports->end(),
            [](const MetaFile& m1, const MetaFile& m2) {
              // Send older reports first to avoid starvation if there is a
              // constant stream of crashes (that is, if thing A is producing
              // crash reports constantly, and thing B produces one crash
              // report, make sure thing B's crash report gets sent eventually.)
              return m1.second.last_modified < m2.second.last_modified;
            });
}

void RemoveReportFiles(const base::FilePath& meta_file, bool delete_crashes) {
  if (meta_file.Extension() != ".meta") {
    LOG(ERROR) << "Not a meta file: " << meta_file.value();
    return;
  }

  const std::string pattern =
      meta_file.BaseName().RemoveExtension().value() + ".*";

  bool add_uploaded_file = !delete_crashes;
  if (delete_crashes) {
    base::FileEnumerator iter(meta_file.DirName(), false /* recursive */,
                              base::FileEnumerator::FILES, pattern);
    for (base::FilePath file = iter.Next(); !file.empty(); file = iter.Next()) {
      if (!base::DeleteFile(file, false /* recursive */)) {
        PLOG(WARNING) << "Failed to remove " << file.value();
        // We may have failed to remove the file due to incorrect selinux config
        // on the directory. However, we may still be able to add files to it,
        // so mark the crash as uploaded to prevent uploading it again.
        // See https://crbug.com/1060019.
        if (file.Extension() == ".meta") {
          add_uploaded_file = true;
        }
      }
    }
  }
  if (add_uploaded_file) {
    base::File f(meta_file.ReplaceExtension(kAlreadyUploadedExt),
                 base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    if (!f.IsValid()) {
      LOG(ERROR) << "Failed to mark crash as uploaded";
    }
  }
}

std::vector<base::FilePath> GetMetaFiles(const base::FilePath& crash_dir) {
  std::vector<base::FilePath> meta_files;
  if (!base::DirectoryExists(crash_dir)) {
    // Directory not existing is not an error.
    return meta_files;
  }

  base::FileEnumerator iter(crash_dir, false /* recursive */,
                            base::FileEnumerator::FILES, "*.meta");
  std::vector<std::pair<base::Time, base::FilePath>> time_meta_pairs;
  for (base::FilePath file = iter.Next(); !file.empty(); file = iter.Next()) {
    base::File::Info info;
    if (!base::GetFileInfo(file, &info)) {
      PLOG(WARNING) << "Failed to get file info: " << file.value();
      continue;
    }
    time_meta_pairs.push_back(std::make_pair(info.last_modified, file));
  }
  std::sort(time_meta_pairs.begin(), time_meta_pairs.end());

  for (const auto& pair : time_meta_pairs)
    meta_files.push_back(pair.second);
  return meta_files;
}

base::FilePath GetBaseNameFromMetadata(const brillo::KeyValueStore& metadata,
                                       const std::string& key) {
  std::string value;
  if (!metadata.GetString(key, &value))
    return base::FilePath();

  return base::FilePath(value).BaseName();
}

std::string GetKindFromPayloadPath(const base::FilePath& payload_path) {
  std::vector<std::string> parts =
      base::SplitString(payload_path.BaseName().value(), ".",
                        base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
  // Suppress "gz".
  if (parts.size() >= 2 && parts.back() == "gz")
    parts.pop_back();

  if (parts.size() <= 1)
    return "";

  std::string extension = parts.back();
  if (extension == "dmp")
    return "minidump";

  return extension;
}

bool ParseMetadata(const std::string& raw_metadata,
                   brillo::KeyValueStore* metadata) {
  metadata->Clear();
  if (!metadata->LoadFromString(raw_metadata))
    return false;

  for (const auto& key : metadata->GetKeys()) {
    if (!IsValidKey(key))
      return false;
  }

  return true;
}

bool IsCompleteMetadata(const brillo::KeyValueStore& metadata) {
  // *.meta files always end with done=1 so we can tell if they are complete.
  std::string value;
  if (!metadata.GetString("done", &value))
    return false;
  return value == "1";
}

bool IsAlreadyUploaded(const base::FilePath& meta_file) {
  return base::PathExists(meta_file.ReplaceExtension(kAlreadyUploadedExt));
}

bool IsTimestampNewEnough(const base::FilePath& timestamp_file) {
  const base::Time threshold =
      base::Time::Now() - base::TimeDelta::FromHours(24);

  base::File::Info info;
  if (!base::GetFileInfo(timestamp_file, &info)) {
    PLOG(ERROR) << "Failed to get file info: " << timestamp_file.value();
    return false;
  }

  return threshold < info.last_modified;
}

bool IsBelowRate(const base::FilePath& timestamps_dir,
                 int max_crash_rate,
                 int max_crash_bytes) {
  // If we can't get a size for one of our uploads, use this as a size. It's
  // an overestimate, but it ensures that when a user upgrades from a previous
  // version of the code to this version, we don't send a huge batch of reports
  // because the previous version didn't write out sizes.
  const int kGuesstimateBytes = util::kDefaultMaxUploadBytes;

  // Count the number of timestamp files, that were written in the past 24
  // hours. Remove files that are older. Each file that exists should contain
  // a SendRecord protobuf giving the number of bytes used for that send; add
  // them up.
  int current_rate = 0;
  int current_bytes = 0;
  base::FileEnumerator iter(timestamps_dir, false /* recursive */,
                            base::FileEnumerator::FILES, "*");
  for (base::FilePath file = iter.Next(); !file.empty(); file = iter.Next()) {
    if (IsTimestampNewEnough(file)) {
      ++current_rate;
      std::string serialized;
      if (!base::ReadFileToString(file, &serialized)) {
        PLOG(WARNING) << "Unable to read timestamp file at " << file.value();
        // Keep going without reading the file; what else can we do? If we
        // really get a file with bad permissions, we don't want to stop ever
        // sending crashes from this computer, so we shouldn't return false.
        // But do add something to current_bytes to avoid uploading an unlimited
        // number of reports if this happens to all our files.
        current_bytes += kGuesstimateBytes;
        continue;
      }

      crash::SendRecord previous_send;
      if (!previous_send.ParseFromString(serialized)) {
        LOG(WARNING) << "Could not parse " << file.value();
        current_bytes += kGuesstimateBytes;
        continue;
      }

      if (previous_send.size() <= 0) {
        // Zero is not a realistic size for the upload, so don't believe it.
        // Probably from a previous version of the code that didn't write out
        // the sizes. proto3 will read an empty file as "all fields are zero".
        LOG(WARNING) << "Previous upload size was " << previous_send.size()
                     << "; ignoring and guessing " << kGuesstimateBytes;
        current_bytes += kGuesstimateBytes;
        continue;
      }
      current_bytes += previous_send.size();
    } else {
      if (!base::DeleteFile(file, false /* recursive */))
        PLOG(WARNING) << "Failed to remove " << file.value();
    }
  }
  LOG(INFO) << "Current send rate: " << current_rate << " sends and "
            << current_bytes << " bytes/24hrs";

  // We allow either condition independently; see comments around
  // kMaxCrashBytes. Therefore, we use || instead of the more common &&.
  return current_rate < max_crash_rate || current_bytes < max_crash_bytes;
}

void RecordSendAttempt(const base::FilePath& timestamps_dir, int bytes) {
  if (!base::CreateDirectory(timestamps_dir)) {
    PLOG(ERROR) << "Failed to create a timestamps directory: "
                << timestamps_dir.value();
    return;
  }

  base::FilePath temp_file_path;
  base::ScopedFILE temp_file(
      base::CreateAndOpenTemporaryFileInDir(timestamps_dir, &temp_file_path));
  if (temp_file == nullptr) {
    PLOG(ERROR) << "Failed to create a file in " << timestamps_dir.value();
  } else {
    crash::SendRecord record;
    record.set_size(bytes);
    std::string serialized;
    record.SerializeToString(&serialized);
    fwrite(serialized.c_str(), 1, serialized.size(), temp_file.get());
  }
}

bool GetSleepTime(const base::FilePath& meta_file,
                  const base::TimeDelta& max_spread_time,
                  const base::TimeDelta& hold_off_time,
                  base::TimeDelta* sleep_time) {
  base::File::Info info;
  if (!base::GetFileInfo(meta_file, &info)) {
    PLOG(ERROR) << "Failed to get file info: " << meta_file.value();
    return false;
  }

  // The meta file should be written *after* all to-be-uploaded files that it
  // references.  Nevertheless, as a safeguard, a hold-off time after writing
  // the meta file is ensured.  Also, sending of crash reports is spread out
  // randomly by up to |max_spread_time|. Thus, for the sleep call the greater
  // of the two delays is used. Use max() to ensure that holdoff_time is not
  // negative.
  const base::TimeDelta hold_off_time_remaining =
      std::max(info.last_modified + hold_off_time - base::Time::Now(),
               base::TimeDelta());

  const int seconds = (max_spread_time.InSeconds() <= 0
                           ? 0
                           : base::RandInt(0, max_spread_time.InSeconds()));
  const base::TimeDelta spread_time = base::TimeDelta::FromSeconds(seconds);

  *sleep_time = std::max(spread_time, hold_off_time_remaining);

  return true;
}

std::string GetClientId() {
  std::string client_id;
  base::FilePath client_id_dir = paths::Get(paths::kCrashSenderStateDirectory);
  if (!base::CreateDirectory(client_id_dir)) {
    PLOG(ERROR) << "Failed to create directory: " << client_id_dir.value();
    return "";
  }
  base::FilePath client_id_file = client_id_dir.Append(paths::kClientId);
  if (base::PathExists(client_id_file)) {
    if (!base::ReadFileToString(client_id_file, &client_id)) {
      PLOG(ERROR) << "Error reading client ID file: " << client_id_file.value();
    } else if (client_id.length() != kClientIdLength) {
      // Don't log what this is, otherwise we may need to scrub it.
      LOG(ERROR) << "Client ID has wrong format, regenerate it";
    } else {
      return client_id;
    }
  }
  client_id = base::GenerateGUID();
  // Strip out the dashes, we don't want those.
  base::RemoveChars(client_id, "-", &client_id);

  if (base::WriteFile(client_id_file, client_id.c_str(), client_id.length()) !=
      client_id.length()) {
    PLOG(ERROR) << "Error writing out client ID to file: "
                << client_id_file.value();
  }

  return client_id;
}

Sender::Sender(std::unique_ptr<MetricsLibraryInterface> metrics_lib,
               std::unique_ptr<base::Clock> clock,
               const Sender::Options& options)
    : metrics_lib_(std::move(metrics_lib)),
      session_manager_proxy_(options.session_manager_proxy),
      shill_proxy_(options.shill_proxy),
      form_data_boundary_(options.form_data_boundary),
      always_write_uploads_log_(options.always_write_uploads_log),
      max_crash_rate_(options.max_crash_rate),
      max_crash_bytes_(options.max_crash_bytes),
      max_spread_time_(options.max_spread_time),
      hold_off_time_(options.hold_off_time),
      sleep_function_(options.sleep_function),
      allow_dev_sending_(options.allow_dev_sending),
      test_mode_(options.test_mode),
      delete_crashes_(options.delete_crashes),
      clock_(std::move(clock)) {}

bool Sender::Init() {
  if (!scoped_temp_dir_.CreateUniqueTempDir()) {
    PLOG(ERROR) << "Failed to create a temporary directory";
    return false;
  }

  return true;
}

base::File Sender::AcquireLockFileOrDie() {
  base::FilePath lock_file_path = paths::Get(paths::kCrashSenderLockFile);
  base::File lock_file(lock_file_path, base::File::FLAG_OPEN_ALWAYS |
                                           base::File::FLAG_READ |
                                           base::File::FLAG_WRITE);
  if (!lock_file.IsValid()) {
    LOG(FATAL) << "Error opening " << lock_file_path.value() << ": "
               << base::File::ErrorToString(lock_file.error_details());
  }

  base::TimeDelta wait_for_lock_file = base::TimeDelta::FromMinutes(5);

  if (IsCrashTestInProgress()) {
    // When running crash.SenderLock test, don't wait a full 5 minutes before
    // completing the test.
    wait_for_lock_file = base::TimeDelta::FromSeconds(1);
  }

  base::Time stop_time = clock_->Now() + wait_for_lock_file;
  while (clock_->Now() < stop_time) {
    if (lock_file.Lock() == base::File::FILE_OK) {
      return lock_file;
    }
    const base::TimeDelta kSleepTime = base::TimeDelta::FromSeconds(1);
    if (sleep_function_.is_null()) {
      base::PlatformThread::Sleep(kSleepTime);
    } else {
      sleep_function_.Run(kSleepTime);
    }
  }

  // Last try. Exit if this one doesn't succeed.
  auto result = lock_file.Lock();
  if (result != base::File::FILE_OK) {
    // Note: If another process is holding the lock, this will just say
    // something unhelpful like "FILE_ERROR_FAILED"; File::Lock doesn't have a
    // separate return code corresponding to EWOULDBLOCK.
    LOG(ERROR) << "Failed to acquire a lock: "
               << base::File::ErrorToString(result);
    RecordCrashDone();
    exit(EXIT_FAILURE);
  }

  return lock_file;
}

bool Sender::HasCrashUploadingConsent() {
  if (util::HasMockConsent()) {
    return true;
  }

  return metrics_lib_->AreMetricsEnabled();
}

Sender::Action Sender::ChooseAction(const base::FilePath& meta_file,
                                    std::string* reason,
                                    CrashInfo* info) {
  if (!IsMock() && !IsOfficialImage() && !allow_dev_sending_ && !test_mode_) {
    *reason = "Not an official OS version";
    return kRemove;
  }

  // HasCrashUploadingConsent() returns false in guest mode, thus IsGuestMode()
  // should be checked first (otherwise, all crash files are deleted in guest
  // mode).
  //
  // Note that this check is slightly racey, but should be rare enough for us
  // not to care:
  //
  // - crash_sender checks IsGuestMode() and it returns false
  // - User logs in to guest mode
  // - crash_sender checks HasCrashUploadingConsent() and it's now false
  // - Reports are deleted
  if (metrics_lib_->IsGuestMode()) {
    *reason = "Crash sending delayed due to guest mode";
    return kIgnore;
  }
  if (!HasCrashUploadingConsent()) {
    *reason = "Crash reporting is disabled";
    return kRemove;
  }

  std::string raw_metadata;
  if (!base::ReadFileToStringWithMaxSize(meta_file, &raw_metadata,
                                         kMaxMetaFileSize)) {
    if (raw_metadata.empty()) {
      *reason = "Metadata file is inaccessible: " + meta_file.value();
      return kIgnore;
    }

    *reason = "Metadata file is unusually large: " + meta_file.value();
    return kRemove;
  }

  if (!ParseMetadata(raw_metadata, &info->metadata)) {
    *reason = "Corrupted metadata: " + raw_metadata;
    return kRemove;
  }

  MetadataToCrashInfo(info->metadata, info);

  if (info->payload_file.empty()) {
    *reason = "Payload is not found in the meta data: " + raw_metadata;
    return kRemove;
  }

  // Check for absolute path, or Append will CHECK-fail.
  if (info->payload_file.IsAbsolute()) {
    *reason =
        "Corrupt meta: payload path is absolute: " + info->payload_file.value();
    return kRemove;
  }

  // Make it an absolute path.
  info->payload_file = meta_file.DirName().Append(info->payload_file);

  if (!base::PathExists(info->payload_file)) {
    *reason = "Missing payload: " + info->payload_file.value();
    return kRemove;
  }

  if (!IsKnownKind(info->payload_kind)) {
    *reason = "Unknown kind: " + info->payload_kind;
    return kRemove;
  }

  // If we have an OS timestamp in the metadata and it's too old to upload then
  // remove the report. We wouldn't have gotten here if the current OS version
  // is too old, so this is an old report from before an OS update.
  std::string os_timestamp_str;
  int64_t os_millis;
  if (!allow_dev_sending_ && !test_mode_ &&
      info->metadata.GetString(kOsTimestamp, &os_timestamp_str) &&
      base::StringToInt64(os_timestamp_str, &os_millis) &&
      util::IsOsTimestampTooOldForUploads(
          base::Time::UnixEpoch() +
              base::TimeDelta::FromMilliseconds(os_millis),
          clock_.get())) {
    *reason = "Old OS version";
    return kRemove;
  }

  base::File::Info file_info;
  if (!base::GetFileInfo(meta_file, &file_info)) {
    // Should not happen since it succeeded to read the file.
    *reason = "Failed to get file info";
    return kIgnore;
  }

  info->last_modified = file_info.last_modified;
  if (!IsCompleteMetadata(info->metadata)) {
    const base::TimeDelta delta = clock_->Now() - file_info.last_modified;
    if (delta.InHours() >= 24) {
      *reason = "Removing old incomplete metadata";
      return kRemove;
    } else {
      *reason = "Recent incomplete metadata";
      return kIgnore;
    }
  }

  if (IsAlreadyUploaded(meta_file)) {
    *reason = "Not uploading already-uploaded crash";
    return kIgnore;
  }

  if (info->payload_kind == "devcore" && !IsDeviceCoredumpUploadAllowed()) {
    *reason = "Device coredump upload not allowed";
    return kIgnore;
  }

  return kSend;
}

void Sender::RemoveAndPickCrashFiles(const base::FilePath& crash_dir,
                                     std::vector<MetaFile>* to_send) {
  std::vector<base::FilePath> meta_files = GetMetaFiles(crash_dir);

  for (const auto& meta_file : meta_files) {
    LOG(INFO) << "Checking metadata: " << meta_file.value();

    std::string reason;
    CrashInfo info;
    switch (ChooseAction(meta_file, &reason, &info)) {
      case kRemove:
        LOG(INFO) << "Removing: " << reason;
        RemoveReportFiles(meta_file, delete_crashes_);
        break;
      case kIgnore:
        LOG(INFO) << "Ignoring: " << reason;
        break;
      case kSend:
        to_send->push_back(std::make_pair(meta_file, std::move(info)));
        break;
      default:
        NOTREACHED();
    }
  }
}

void Sender::SendCrashes(const std::vector<MetaFile>& crash_meta_files,
                         base::TimeDelta* total_sleep_time) {
  if (crash_meta_files.empty())
    return;

  std::string client_id = GetClientId();

  if (total_sleep_time) {
    *total_sleep_time = base::TimeDelta();
  }

  base::File lock(AcquireLockFileOrDie());
  for (const auto& pair : crash_meta_files) {
    const base::FilePath& meta_file = pair.first;
    const CrashInfo& info = pair.second;
    LOG(INFO) << "Evaluating crash report: " << meta_file.value();

    base::TimeDelta sleep_time;
    if (!GetSleepTime(meta_file, max_spread_time_, hold_off_time_,
                      &sleep_time)) {
      LOG(WARNING) << "Failed to compute sleep time for " << meta_file.value();
      continue;
    }

    LOG(INFO) << "Scheduled to send in " << sleep_time.InSeconds() << "s";
    lock.Close();  // Don't hold lock during sleep.
    if (!IsMock()) {
      base::PlatformThread::Sleep(sleep_time);
    } else if (!sleep_function_.is_null()) {
      sleep_function_.Run(sleep_time);
    }
    if (total_sleep_time) {
      *total_sleep_time += sleep_time;
    }
    lock = AcquireLockFileOrDie();

    // This should be checked inside of the loop, since the device can disable
    // metrics while sending crash reports with an interval up to
    // max_spread_time_ between sends. We only need to check if metrics are
    // enabled and not guest mode because in guest mode, it always indicates
    // that metrics are disabled.
    if (!HasCrashUploadingConsent()) {
      LOG(INFO) << "Metrics disabled or guest mode entered, delaying crash "
                << "sending";
      return;
    }

    // User-specific crash reports become inaccessible if the user signs out
    // while sleeping, thus we need to check if the metadata is still
    // accessible.
    if (!base::PathExists(meta_file)) {
      LOG(INFO) << "Metadata is no longer accessible: " << meta_file.value();
      continue;
    }

    const base::FilePath timestamps_dir =
        paths::Get(paths::kTimestampsDirectory);
    if (!IsBelowRate(timestamps_dir, max_crash_rate_, max_crash_bytes_)) {
      LOG(WARNING) << "Cannot send more crashes. Sending " << meta_file.value()
                   << " would exceed the max daily rate of " << max_crash_rate_
                   << " crashes and " << max_crash_bytes_ << " bytes";
      return;
    }

    // If we are offline, then don't try to send any crashes.
    if (!IsMock() && !IsNetworkOnline()) {
      LOG(INFO) << "Stopping crash sending; network is offline";
      return;
    }

    const CrashDetails details = {
        .meta_file = meta_file,
        .payload_file = info.payload_file,
        .payload_kind = info.payload_kind,
        .client_id = client_id,
        .metadata = info.metadata,
    };
    if (!RequestToSendCrash(details)) {
      LOG(WARNING) << "Failed to send " << meta_file.value()
                   << ", not removing; will retry later";
      continue;
    }
    LOG(INFO) << "Successfully sent crash " << meta_file.value()
              << " and removing.";
    RemoveReportFiles(meta_file, delete_crashes_);
  }
}

std::vector<base::FilePath> Sender::GetUserCrashDirectories() {
  // Set up the session manager proxy if it's not given from the options.
  if (!session_manager_proxy_) {
    EnsureDBusIsReady();
    session_manager_proxy_.reset(
        new org::chromium::SessionManagerInterfaceProxy(bus_));
  }

  std::vector<base::FilePath> directories;
  util::GetUserCrashDirectories(session_manager_proxy_.get(), &directories);
  util::GetDaemonStoreCrashDirectories(session_manager_proxy_.get(),
                                       &directories);

  return directories;
}

std::unique_ptr<brillo::http::FormData> Sender::CreateCrashFormData(
    const CrashDetails& details, std::string* product_name_out) {
  std::unique_ptr<brillo::http::FormData> form_data =
      std::make_unique<brillo::http::FormData>(form_data_boundary_);

  std::string exec_name;
  if (!details.metadata.GetString("exec_name", &exec_name))
    exec_name = kUndefined;
  form_data->AddTextField("exec_name", exec_name);

  std::string board;
  if (!details.metadata.GetString("board", &board) &&
      !GetCachedKeyValueDefault(base::FilePath(paths::kLsbRelease),
                                "CHROMEOS_RELEASE_BOARD", &board)) {
    board = kUndefined;
  }
  form_data->AddTextField("board", board);

  std::string hwclass = util::GetHardwareClass();
  form_data->AddTextField("hwclass", hwclass);

  // When uploading Chrome reports we need to report the right product and
  // version. If the meta file does not specify it we try to examine os-release
  // content. If not available there product gets assigned default product name
  // and version is derived from CHROMEOS_RELEASE_VERSION in /etc/lsb-release.
  std::string product;
  if (!details.metadata.GetString("upload_var_prod", &product)) {
    product =
        GetOsReleaseValue({"GOOGLE_CRASH_ID", "ID"}).value_or(kChromeOsProduct);
  }
  form_data->AddTextField("prod", product);

  std::string version;
  if (!details.metadata.GetString("upload_var_ver", &version)) {
    if (!details.metadata.GetString("ver", &version)) {
      version = GetOsReleaseValue(
                    {"GOOGLE_CRASH_VERSION_ID", "BUILD_ID", "VERSION_ID"})
                    .value_or(kUndefined);
    }
  }
  form_data->AddTextField("ver", version);

  std::string sig;
  if (details.metadata.GetString("sig", &sig)) {
    form_data->AddTextField("sig", sig);
    form_data->AddTextField("sig2", sig);
  }
  base::FilePath payload_file = details.payload_file;
  if (!payload_file.IsAbsolute()) {
    payload_file = details.meta_file.DirName().Append(payload_file);
  }
  brillo::ErrorPtr file_error;
  if (!form_data->AddFileField("upload_file_" + details.payload_kind,
                               payload_file, {}, &file_error)) {
    LOG(ERROR) << "Failed adding payload file as attachment: "
               << file_error->GetMessage();
    return nullptr;
  }

  for (const auto& key : details.metadata.GetKeys()) {
    if (!base::StartsWith(key, "upload_", base::CompareCase::SENSITIVE) ||
        key == "upload_var_prod" || key == "upload_var_ver" ||
        key == "upload_var_guid") {
      continue;
    }
    std::string value;
    details.metadata.GetString(key, &value);
    bool is_upload_var =
        base::StartsWith(key, kUploadVarPrefix, base::CompareCase::SENSITIVE);
    bool is_upload_text =
        base::StartsWith(key, kUploadTextPrefix, base::CompareCase::SENSITIVE);
    bool is_upload_file =
        base::StartsWith(key, kUploadFilePrefix, base::CompareCase::SENSITIVE);
    if (is_upload_var) {
      form_data->AddTextField(key.substr(sizeof(kUploadVarPrefix) - 1), value);
    } else if (is_upload_text || is_upload_file) {
      base::FilePath value_file(value);
      // Relative paths are relative to the meta data file.
      if (!value_file.IsAbsolute()) {
        value_file = details.meta_file.DirName().Append(value_file);
      }
      if (is_upload_text) {
        std::string value_content;
        if (base::ReadFileToString(value_file, &value_content)) {
          form_data->AddTextField(key.substr(sizeof(kUploadTextPrefix) - 1),
                                  value_content);
        } else {
          LOG(ERROR) << "Failed attaching file contents from "
                     << value_file.value();
        }
      } else {  // not is_upload_text so must be is_upload_file
        brillo::ErrorPtr error;
        if (base::PathExists(value_file) &&
            !form_data->AddFileField(key.substr(sizeof(kUploadFilePrefix) - 1),
                                     value_file, {}, &error)) {
          LOG(ERROR) << "Failed attaching file " << value_file.value()
                     << " of: " << error->GetMessage();
        }
      }
    }
  }

  std::string image_type = GetImageType();
  if (!image_type.empty())
    form_data->AddTextField("image_type", image_type);

  std::string boot_mode = util::GetBootModeString();
  if (!boot_mode.empty())
    form_data->AddTextField("boot_mode", boot_mode);

  std::string error_type;
  if (details.metadata.GetString("error_type", &error_type))
    form_data->AddTextField("error_type", error_type);

  LOG(INFO) << "Sending crash:";
  if (product != kChromeOsProduct)
    LOG(INFO) << "  Sending crash report on behalf of " << product;
  LOG(INFO) << "  Metadata: " << details.meta_file.value() << " ("
            << details.payload_kind << ")";
  LOG(INFO) << "  Payload: " << details.payload_file.value();
  LOG(INFO) << "  Version: " << version;
  if (!image_type.empty())
    LOG(INFO) << "  Image type: " << image_type;
  if (!boot_mode.empty())
    LOG(INFO) << "  Boot mode: " << boot_mode;
  if (IsMock()) {
    LOG(INFO) << "  Product: " << product;
    LOG(INFO) << "  URL: " << kReportUploadProdUrl;
    LOG(INFO) << "  Board: " << board;
    LOG(INFO) << "  HWClass: " << hwclass;
    if (!sig.empty())
      LOG(INFO) << "  sig: " << sig;
  }

  LOG(INFO) << "  Exec name: " << exec_name;
  if (!error_type.empty())
    LOG(INFO) << "  Error type: " << error_type;

  form_data->AddTextField("guid", details.client_id);

  if (product_name_out)
    *product_name_out = product;

  return form_data;
}

std::unique_ptr<base::Value> Sender::CreateJsonEntity(
    const std::string& report_id,
    const std::string& product_name,
    const CrashDetails& details) {
  auto root_dict = std::make_unique<base::Value>(base::Value::Type::DICTIONARY);

  int64_t timestamp = (base::Time::Now() - base::Time::UnixEpoch()).InSeconds();
  root_dict->SetKey(kJsonLogKeyUploadTime,
                    base::Value(std::to_string(timestamp)));

  root_dict->SetKey(kJsonLogKeyUploadId, base::Value(report_id));
  root_dict->SetKey(kJsonLogKeyLocalId, base::Value(product_name));

  // The |capture_timestamp| should be converted from milliseconds to seconds.
  std::string capture_timestamp;
  int64_t capture_timestamp_millis;
  if (details.metadata.GetString(kMetadataKeyCaptureTimeMillis,
                                 &capture_timestamp) &&
      base::StringToInt64(capture_timestamp, &capture_timestamp_millis)) {
    root_dict->SetKey(
        kJsonLogKeyCaptureTime,
        base::Value(std::to_string(capture_timestamp_millis / 1000)));
  }

  // The state value is always same as
  // UploadList::UploadInfo::State::Uploaded.
  root_dict->SetKey(kJsonLogKeyState, base::Value(3));

  std::string source;
  if (details.metadata.GetString(kMetadataKeySource, &source)) {
    // Hide the real source to avoid privacy concern if it is not a system
    // crash.
    if (!paths::Get(paths::kSystemCrashDirectory).IsParent(details.meta_file))
      source = kMetadataValueRedacted;
    root_dict->SetKey(kJsonLogKeySource, base::Value(source));
  }

  return root_dict;
}

bool Sender::RequestToSendCrash(const CrashDetails& details) {
  std::string product_name;
  std::unique_ptr<brillo::http::FormData> form_data =
      CreateCrashFormData(details, &product_name);
  if (!form_data) {
    return false;
  }

  if (test_mode_) {
    LOG(WARNING) << kTestModeSuccessful;
    return true;
  }

  std::string report_id;

  auto stream_data = form_data->ExtractDataStream();
  uint64_t uncompressed_size = stream_data->GetSize();
  // Compress the data before sending it to the server. We compress the entire
  // request body and then specify the Content-Encoding as gzip to achieve this.
  std::vector<unsigned char> compressed_form_data =
      util::GzipStream(std::move(stream_data));

  // Record the send attempt even if it fails. We may still have used up network
  // bandwidth even if we lose the connection at the end.
  const base::FilePath timestamps_dir = paths::Get(paths::kTimestampsDirectory);
  int size = static_cast<int>(compressed_form_data.size());
  if (size == 0) {
    // Compression failed; we'll end up using the uncompressed stream below.
    size = static_cast<int>(uncompressed_size);
  }
  RecordSendAttempt(timestamps_dir, size);

  if (!IsMock()) {
    // Determine the proxy server if it's not given from the options.
    if (proxy_servers_.empty()) {
      EnsureDBusIsReady();
      brillo::http::GetChromeProxyServers(bus_, kReportUploadProdUrl,
                                          &proxy_servers_);
    }

    std::shared_ptr<brillo::http::Transport> transport;
    if (proxy_servers_.empty() || proxy_servers_[0] == "direct://") {
      transport = brillo::http::Transport::CreateDefault();
    } else {
      transport =
          brillo::http::Transport::CreateDefaultWithProxy(proxy_servers_[0]);
    }

    brillo::ErrorPtr upload_error;
    std::unique_ptr<brillo::http::Response> response;
    if (!compressed_form_data.empty()) {
      response = brillo::http::PostBinaryAndBlock(
          allow_dev_sending_ ? kReportUploadStagingUrl : kReportUploadProdUrl,
          compressed_form_data.data(), compressed_form_data.size(),
          form_data->GetContentType(),
          {{brillo::http::request_header::kContentEncoding, "gzip"}}, transport,
          &upload_error);
    } else {
      LOG(ERROR) << "Failed compressing crash data for upload, perform the "
                 << "upload uncompressed";
      // This really should never happen, but it's probably better to try to
      // send this uncompressed even though it requires regenerating all the
      // data since extracting the data stream from the FormData is a
      // potentially destructive operation.
      form_data = CreateCrashFormData(details, &product_name);
      if (!form_data) {
        return false;
      }
      response = brillo::http::PostFormDataAndBlock(
          allow_dev_sending_ ? kReportUploadStagingUrl : kReportUploadProdUrl,
          std::move(form_data), {} /* headers */, transport, &upload_error);
    }

    if (!response) {
      LOG(ERROR) << "Crash sending failed with error: "
                 << upload_error->GetMessage();
      return false;
    }
    if (!response->IsSuccessful()) {
      LOG(ERROR) << "Crash sending failed with HTTP "
                 << response->GetStatusCode() << ": "
                 << response->GetStatusText();
      return false;
    }

    report_id = response->ExtractDataAsString();
  } else {
    if (!IsMockSuccessful()) {
      LOG(INFO) << "Mocking unsuccessful send";
      return false;
    }
    LOG(INFO) << "Mocking successful send";

    if (!always_write_uploads_log_)
      return true;

    if (!details.metadata.GetString("fake_report_id", &report_id))
      report_id = kUndefined;
  }

  if (product_name == "Chrome_ChromeOS")
    product_name = "Chrome";
  if (!util::IsOfficialImage()) {
    base::ReplaceSubstringsAfterOffset(&product_name, 0, "Chrome", "Chromium");
  }
  std::string silent;
  details.metadata.GetString("silent", &silent);
  if (always_write_uploads_log_ || (!USE_CHROMELESS_TTY && silent != "true")) {
    base::FilePath upload_logs_path(paths::Get(paths::kChromeCrashLog));

    // Open the file before we check the normalized path or it will fail if the
    // path doesn't exist.
    base::File upload_logs_file(upload_logs_path, base::File::FLAG_OPEN_ALWAYS |
                                                      base::File::FLAG_APPEND);

    base::FilePath normalized_path;
    if (base::NormalizeFilePath(upload_logs_path, &normalized_path) &&
        upload_logs_path == normalized_path) {
      std::unique_ptr<base::Value> json_entity =
          CreateJsonEntity(report_id, product_name, details);
      std::string upload_log_entry;
      if (!base::JSONWriter::Write(*json_entity, &upload_log_entry)) {
        LOG(WARNING) << "Cannot construct a valid uploads.log entry in JSON "
                        "format, so skip the update.";
        return true;
      }

      upload_log_entry += "\n";
      if (!upload_logs_file.IsValid() ||
          upload_logs_file.WriteAtCurrentPos(upload_log_entry.c_str(),
                                             upload_log_entry.size()) !=
              upload_log_entry.size()) {
        PLOG(ERROR) << "Error writing to Chrome uploads.log file";
      }
    } else {
      LOG(ERROR) << "Did not write to Chrome uploads.log file because the "
                 << "normalized path didn't match the target path, target: "
                 << upload_logs_path.value()
                 << " normalized: " << normalized_path.value();
    }
  }
  LOG(INFO) << "Crash report receipt ID " << report_id;
  return true;
}

void Sender::EnsureDBusIsReady() {
  if (!bus_) {
    dbus::Bus::Options options;
    options.bus_type = dbus::Bus::SYSTEM;
    bus_ = new dbus::Bus(options);
    CHECK(bus_->Connect());
  }
}

base::Optional<std::string> Sender::GetOsReleaseValue(
    const std::vector<std::string>& keys) {
  if (!os_release_reader_) {
    os_release_reader_ = std::make_unique<brillo::OsReleaseReader>();
    os_release_reader_->Load();
  }
  std::string value;
  for (const auto& key : keys) {
    if (os_release_reader_->GetString(key, &value))
      return base::Optional<std::string>(value);
  }
  return base::Optional<std::string>();
}

bool Sender::IsNetworkOnline() {
  if (!shill_proxy_) {
    EnsureDBusIsReady();
    shill_proxy_ =
        std::make_unique<org::chromium::flimflam::ManagerProxy>(bus_);
  }
  brillo::VariantDictionary dict;
  brillo::ErrorPtr err;
  if (!shill_proxy_->GetProperties(&dict, &err)) {
    // If we don't know, then just assume we are connected.
    LOG(WARNING) << "Failed making D-Bus call for network state; attempting "
                 << "upload anyways";
    return true;
  }
  const std::string state = brillo::GetVariantValueOrDefault<std::string>(
      dict, shill::kConnectionStateProperty);
  if (state.empty()) {
    // If we didn't get a valid value back, then assume we are connected.
    LOG(WARNING) << "Received empty ConnectionState property from shill; "
                 << "attempting upload anyways";
    return true;
  }
  // Possible values for this are defined in platform2/shill/service.cc, but the
  // only one that means we have an Internet connection is "online". All of the
  // other values represent some other reduced (or no) level of connectivity or
  // the process of establishing a connection.
  return base::EqualsCaseInsensitiveASCII(state, "online");
}

}  // namespace util
