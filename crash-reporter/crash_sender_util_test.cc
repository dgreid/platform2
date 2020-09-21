// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_sender_util.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <unistd.h>

#include <memory>
#include <numeric>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/command_line.h>
#include <base/files/file.h>
#include <base/files/file_enumerator.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/json/json_reader.h>
#include <base/macros.h>
#include <base/stl_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/strcat.h>
#include <base/time/time.h>
#include <base/values.h>
#include <brillo/flag_helper.h>
#include <brillo/key_value_store.h>
#include <brillo/process/process.h>
#include <chromeos/dbus/service_constants.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>
#include <shill/dbus-proxy-mocks.h>

#include "crash-reporter/crash_sender_paths.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/test_util.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::ExitedWithCode;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::UnorderedElementsAre;

namespace util {
namespace {

// Enum types for setting the runtime conditions.
enum BuildType { kOfficialBuild, kUnofficialBuild };
enum SessionType { kSignInMode, kGuestMode };
enum MetricsFlag { kMetricsEnabled, kMetricsDisabled };

constexpr char kFakeClientId[] = "00112233445566778899aabbccddeeff";

// This is what the kConnectionState property will get set to for mocked calls
// into shill flimflam manager.
std::string* g_connection_state;

// Simple mock for Clock. We can't use SimpleTestClock in some places because
// we need Now to return different values on different calls, and we don't have
// a hook to gain control in between the Now() calls.
class MockClock : public base::Clock {
 public:
  ~MockClock() override {}
  MOCK_METHOD(base::Time, Now, (), (const override));
};

// Parses the Chrome uploads.log file from Sender to a vector of items per line.
// Example:
//
// {"field1":"foo1","field2":"foo2"}
// {"field1":"bar1","field2":"bar2"}
//
// => [{"field1":"foo1","field2":"foo2"}, {"field1":"bar1","field2":"bar2"}]
//
std::vector<base::Optional<base::Value>> ParseChromeUploadsLog(
    const std::string& contents) {
  std::vector<base::Optional<base::Value>> rows;

  std::vector<std::string> lines = base::SplitString(
      contents, "\n", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto& line : lines) {
    rows.push_back(base::JSONReader::Read(line));
  }

  return rows;
}

// Helper function for calling GetBasePartOfCrashFile() concisely for tests.
std::string GetBasePartHelper(const std::string& file_name) {
  return GetBasePartOfCrashFile(base::FilePath(file_name)).value();
}

// Helper function for calling base::TouchFile() concisely for tests.
bool TouchFileHelper(const base::FilePath& file_name,
                     base::Time modified_time) {
  return base::TouchFile(file_name, modified_time, modified_time);
}

// Creates lsb-release file with information about the build type.
bool CreateLsbReleaseFile(BuildType type) {
  std::string label = "Official build";
  if (type == kUnofficialBuild)
    label = "Test build";

  return test_util::CreateFile(paths::Get("/etc/lsb-release"),
                               "CHROMEOS_RELEASE_DESCRIPTION=" + label + "\n");
}

// Creates a file that indicates uploading of device coredumps is allowed.
bool CreateDeviceCoredumpUploadAllowedFile() {
  return test_util::CreateFile(
      paths::GetAt(paths::kCrashReporterStateDirectory,
                   paths::kDeviceCoredumpUploadAllowed),
      "");
}

// Creates the client ID file and stores the fake client ID in it.
bool CreateClientIdFile() {
  return test_util::CreateFile(
      paths::GetAt(paths::kCrashSenderStateDirectory, paths::kClientId),
      kFakeClientId);
}

// Returns file names found in |directory|.
std::vector<base::FilePath> GetFileNamesIn(const base::FilePath& directory) {
  std::vector<base::FilePath> files;
  base::FileEnumerator iter(directory, false /* recursive */,
                            base::FileEnumerator::FILES, "*");
  for (base::FilePath file = iter.Next(); !file.empty(); file = iter.Next())
    files.push_back(file);
  return files;
}

// Fake sleep function that records the requested sleep time.
void FakeSleep(std::vector<base::TimeDelta>* sleep_times,
               base::TimeDelta duration) {
  sleep_times->push_back(duration);
}

// Set the file flag which indicates we are mocking crash sending, either
// successfully or as a a failure. This also creates the directory where
// uploads.log is written to since Chrome would normally be doing that.
bool SetMockCrashSending(bool success) {
  return test_util::CreateFile(paths::GetAt(paths::kSystemRunStateDirectory,
                                            paths::kMockCrashSending),
                               success ? "" : "0") &&
         base::CreateDirectory(paths::Get(paths::kChromeCrashLog).DirName());
}

// Handles calls for getting the network state.
bool GetShillProperties(
    brillo::VariantDictionary* dict,
    brillo::ErrorPtr* error,
    int timeout_ms = dbus::ObjectProxy::TIMEOUT_USE_DEFAULT) {
  dict->emplace(shill::kConnectionStateProperty, *g_connection_state);
  return true;
}

class CrashSenderUtilTest : public testing::Test {
 protected:
  void SetUp() override {
    // Grab executable path before TearDown() can reset base::CommandLine.
    if (build_directory_ == nullptr) {
      base::FilePath my_executable_path =
          base::CommandLine::ForCurrentProcess()->GetProgram();
      build_directory_ = new base::FilePath(my_executable_path.DirName());
    }
    metrics_lib_ = std::make_unique<MetricsLibraryMock>();
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    test_dir_ = temp_dir_.GetPath();
    paths::SetPrefixForTesting(test_dir_);

    // Make sure the directory for the lock file exists.
    const base::FilePath lock_file_path =
        paths::Get(paths::kCrashSenderLockFile);
    const base::FilePath lock_file_directory = lock_file_path.DirName();
    ASSERT_TRUE(base::CreateDirectory(lock_file_directory));

    // We need to properly init the CommandLine object for the command line
    // parsing tests.
    base::CommandLine::Init(0, nullptr);
  }

  void TearDown() override {
    paths::SetPrefixForTesting(base::FilePath());

    // ParseCommandLine() uses base::CommandLine via
    // brillo::FlagHelper. Reset these here to avoid side effects.
    if (base::CommandLine::InitializedForCurrentProcess())
      base::CommandLine::Reset();
    brillo::FlagHelper::ResetForTesting();
  }

  // Checks to see if a file is locked by AcquireLockFileOrDie().
  bool IsFileLocked(const base::FilePath& file_name) {
    // AcquireLockFileOrDie creates the file when it runs, so count the file
    // not existing as "not locked".
    if (!base::PathExists(file_name)) {
      return false;
    }

    // There's no portable & reliable way for a process to test its own file
    // locks, so we have to spawn another process (which won't inherit the
    // locks) to do the testing.
    CHECK(build_directory_);
    base::FilePath lock_file_tester =
        build_directory_->Append("lock_file_tester");
    std::string command = lock_file_tester.value() + " " + file_name.value();
    int test_result = system(command.c_str());
    if (WIFEXITED(test_result)) {
      if (WEXITSTATUS(test_result) == 0) {
        return true;
      }
      if (WEXITSTATUS(test_result) == 1) {
        return false;
      }
      LOG(FATAL) << "lock_file_tester failed with exit code "
                 << WEXITSTATUS(test_result);
    }
    LOG(FATAL)
        << "lock_file_tester failed before exiting; complete wait status "
        << test_result;
    return false;
  }

  // Lock the indicated file |file_name| using base::File::Lock() so that
  // AcquireLockFileOrDie() will fail to acquire it. File will be created if
  // it doesn't exist. Returns when the file is actually locked. Since locks are
  // per-process, in order to prevent this process from locking the file, we
  // have to spawn a separate process to hold the lock; the process holding the
  // lock is returned. It can be killed to release the lock.
  std::unique_ptr<brillo::Process> LockFile(const base::FilePath& file_name) {
    auto lock_process = std::make_unique<brillo::ProcessImpl>();
    CHECK(build_directory_);
    base::FilePath lock_file_holder =
        build_directory_->Append("hold_lock_file");
    lock_process->AddArg(lock_file_holder.value());
    lock_process->AddArg(file_name.value());
    CHECK(lock_process->Start());

    // Wait for the file to actually be locked. Don't wait forever in case the
    // subprocess fails in some way.
    base::Time stop_time = base::Time::Now() + base::TimeDelta::FromMinutes(1);
    bool success = false;
    while (!success && base::Time::Now() < stop_time) {
      base::File lock_file(file_name, base::File::FLAG_OPEN |
                                          base::File::FLAG_READ |
                                          base::File::FLAG_WRITE);
      if (lock_file.IsValid()) {
        struct flock lock;
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;
        if (fcntl(lock_file.GetPlatformFile(), F_GETLK, &lock) == 0 &&
            lock.l_type == F_WRLCK) {
          success = true;
        }
      }

      if (!success) {
        base::PlatformThread::Sleep(base::TimeDelta::FromSeconds(5));
      }
    }

    CHECK(success) << "Subprocess did not lock " << file_name.value();
    return lock_process;
  }

  // Creates a file at |file_path| with contents |content| and sets its access
  // and modification time to |timestamp|.
  bool CreateFile(const base::FilePath& file_path,
                  base::StringPiece content,
                  base::Time timestamp) {
    if (!test_util::CreateFile(file_path, content))
      return false;

    if (!TouchFileHelper(file_path, timestamp))
      return false;

    return true;
  }

  // Creates test crash files in |crash_directory|. Returns true on success.
  bool CreateTestCrashFiles(const base::FilePath& crash_directory) {
    const base::Time now = test_util::GetDefaultTime();
    const base::TimeDelta hour = base::TimeDelta::FromHours(1);

    // Choose timestamps so that the return value of GetMetaFiles() is sorted
    // per timestamps correctly.
    const base::Time good_meta_time = now - hour * 3;
    const base::Time absolute_meta_time = now - hour * 2;
    const base::Time recent_os_meta_time = now - hour;
    const base::Time devcore_meta_time = now;

    // These should be kept, since the payload is a known kind and exists.
    good_meta_ = crash_directory.Append("good.meta");
    good_log_ = crash_directory.Append("good.log");
    if (!CreateFile(good_meta_, "payload=good.log\ndone=1\n", good_meta_time))
      return false;
    if (!CreateFile(good_log_, "", now))
      return false;

    // These should be kept, the payload path is absolute but should be handled
    // properly.
    absolute_meta_ = crash_directory.Append("absolute.meta");
    absolute_log_ = crash_directory.Append("absolute.log");
    if (!CreateFile(absolute_meta_,
                    "payload=" + absolute_log_.value() + "\n" + "done=1\n",
                    absolute_meta_time))
      return false;
    if (!CreateFile(absolute_log_, "", now))
      return false;

    // These should be removed, since the `alreadyuploaded` file exists.
    uploaded_meta_ = crash_directory.Append("uploaded.meta");
    uploaded_log_ = crash_directory.Append("uploaded.log");
    uploaded_already_ = crash_directory.Append("uploaded.alreadyuploaded");
    if (!CreateFile(uploaded_meta_, "payload=uploaded.log\ndone=1\n",
                    good_meta_time))
      return false;
    if (!CreateFile(uploaded_log_, "", now))
      return false;
    if (!CreateFile(uploaded_already_, "", now))
      return false;

    // This should be ignored as corrupt. Payload can't be /.
    root_payload_meta_ = crash_directory.Append("root_payload.meta");
    if (!test_util::CreateFile(root_payload_meta_,
                               "payload=/\n"
                               "done=1\n"))
      return false;

    // These should be ignored, if uploading of device coredumps is not allowed.
    devcore_meta_ = crash_directory.Append("devcore.meta");
    devcore_devcore_ = crash_directory.Append("devcore.devcore");
    if (!CreateFile(devcore_meta_,
                    "payload=devcore.devcore\n"
                    "done=1\n",
                    devcore_meta_time))
      return false;
    if (!CreateFile(devcore_devcore_, "", now))
      return false;

    // This should be removed, since metadata is corrupted.
    corrupted_meta_ = crash_directory.Append("corrupted.meta");
    if (!CreateFile(corrupted_meta_, "!@#$%^&*\ndone=1\n", now))
      return false;

    // This should be removed, since no payload info is recorded.
    empty_meta_ = crash_directory.Append("empty.meta");
    if (!CreateFile(empty_meta_, "done=1\n", now))
      return false;

    // This should be removed, since the payload file does not exist.
    nonexistent_meta_ = crash_directory.Append("nonexistent.meta");
    if (!CreateFile(nonexistent_meta_,
                    "payload=nonexistent.log\n"
                    "done=1\n",
                    now))
      return false;

    // These should be removed, since the payload is an unknown kind.
    unknown_meta_ = crash_directory.Append("unknown.meta");
    unknown_xxx_ = crash_directory.Append("unknown.xxx");
    if (!CreateFile(unknown_meta_,
                    "payload=unknown.xxx\n"
                    "done=1\n",
                    now))
      return false;
    if (!CreateFile(unknown_xxx_, "", now))
      return false;

    // This should be removed, since the meta file is old.
    old_incomplete_meta_ = crash_directory.Append("old_incomplete.meta");
    if (!CreateFile(old_incomplete_meta_, "payload=good.log\n", now))
      return false;
    if (!TouchFileHelper(old_incomplete_meta_, now - hour * 24))
      return false;

    // This should be ignored, even though the payload doesn't exist,
    // since the meta file is new.
    new_incomplete_meta_ = crash_directory.Append("new_incomplete.meta");
    if (!CreateFile(new_incomplete_meta_, "payload=nonexistent.log\n", now))
      return false;

    // This should be ignored, even though there's no payload, since the meta
    // file is new.
    new_empty_meta_ = crash_directory.Append("new_empty.meta");
    if (!CreateFile(new_empty_meta_, "", now))
      return false;

    // This should be kept since the OS timestamp is recent.
    recent_os_meta_ = crash_directory.Append("recent_os.meta");
    if (!CreateFile(recent_os_meta_,
                    base::StringPrintf(
                        "payload=recent_os.log\n"
                        "os_millis=%" PRId64 "\n"
                        "done=1\n",
                        (now - base::Time::UnixEpoch()).InMilliseconds()),
                    recent_os_meta_time)) {
      return false;
    }
    recent_os_log_ = crash_directory.Append("recent_os.log");
    if (!CreateFile(recent_os_log_, "", now))
      return false;

    // This should be removed since the OS timestamp is old.
    old_os_meta_ = crash_directory.Append("old_os.meta");
    if (!CreateFile(old_os_meta_,
                    base::StringPrintf("payload=good.log\n"
                                       "os_millis=%" PRId64 "\n"
                                       "done=1\n",
                                       ((now - base::Time::UnixEpoch()) -
                                        base::TimeDelta::FromDays(200))
                                           .InMilliseconds()),
                    now)) {
      return false;
    }

    // Create large metadata with the size of 1MiB + 1byte.
    large_meta_ = crash_directory.Append("large.meta");
    if (!CreateFile(large_meta_, std::string(1024 * 1024 + 1, 'x'), now)) {
      return false;
    }

    return true;
  }

  // Sets the runtime conditions that affect behaviors of ChooseAction().
  // Returns true on success.
  bool SetConditions(BuildType build_type,
                     SessionType session_type,
                     MetricsFlag metrics_flag) {
    return SetConditions(build_type, session_type, metrics_flag,
                         metrics_lib_.get());
  }

  // Version of SetConditions useful for tests that need to create a Sender.
  // Sender owns the MetricsLibraryInterface pointer, so metrics_lib_ is
  // usually nullptr in these tests.
  static bool SetConditions(BuildType build_type,
                            SessionType session_type,
                            MetricsFlag metrics_flag,
                            MetricsLibraryMock* metrics_lib) {
    if (!CreateLsbReleaseFile(build_type))
      return false;

    metrics_lib->set_guest_mode(session_type == kGuestMode);
    metrics_lib->set_metrics_enabled(metrics_flag == kMetricsEnabled);

    return true;
  }

  // Directory that the test executable lives in. We reset CommandLine during
  // TearDown, so we must grab this information early.
  static base::FilePath* build_directory_;
  base::AtExitManager at_exit_manager_;

  std::unique_ptr<MetricsLibraryMock> metrics_lib_;
  base::ScopedTempDir temp_dir_;
  base::FilePath test_dir_;

  base::FilePath good_meta_;
  base::FilePath good_log_;
  base::FilePath absolute_meta_;
  base::FilePath absolute_log_;
  base::FilePath uploaded_meta_;
  base::FilePath uploaded_log_;
  base::FilePath uploaded_already_;
  base::FilePath root_payload_meta_;
  base::FilePath devcore_meta_;
  base::FilePath devcore_devcore_;
  base::FilePath empty_meta_;
  base::FilePath corrupted_meta_;
  base::FilePath nonexistent_meta_;
  base::FilePath unknown_meta_;
  base::FilePath unknown_xxx_;
  base::FilePath old_incomplete_meta_;
  base::FilePath new_incomplete_meta_;
  base::FilePath new_empty_meta_;
  base::FilePath recent_os_meta_;
  base::FilePath recent_os_log_;
  base::FilePath old_os_meta_;
  base::FilePath large_meta_;
};

base::FilePath* CrashSenderUtilTest::build_directory_ = nullptr;
using CrashSenderUtilDeathTest = CrashSenderUtilTest;

}  // namespace

TEST_F(CrashSenderUtilTest, ParseCommandLine_NoFlags) {
  const char* argv[] = {"crash_sender"};
  base::CommandLine command_line(base::size(argv), argv);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      &command_line);
  CommandLineFlags flags;
  ParseCommandLine(base::size(argv), argv, &flags);
  EXPECT_EQ(flags.max_spread_time.InSeconds(), kMaxSpreadTimeInSeconds);
  EXPECT_TRUE(flags.crash_directory.empty());
  EXPECT_FALSE(flags.ignore_rate_limits);
  EXPECT_FALSE(flags.ignore_hold_off_time);
  EXPECT_FALSE(flags.allow_dev_sending);
  EXPECT_FALSE(flags.ignore_pause_file);
  EXPECT_FALSE(flags.upload_old_reports);
}

TEST_F(CrashSenderUtilDeathTest, ParseCommandLine_InvalidMaxSpreadTime) {
  const char* argv[] = {"crash_sender", "--max_spread_time=-1"};
  base::CommandLine command_line(base::size(argv), argv);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      &command_line);
  CommandLineFlags flags;
  EXPECT_DEATH(ParseCommandLine(base::size(argv), argv, &flags),
               "Invalid value for max spread time: -1");
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_ValidMaxSpreadTime) {
  const char* argv[] = {"crash_sender", "--max_spread_time=0"};
  base::CommandLine command_line(base::size(argv), argv);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      &command_line);
  CommandLineFlags flags;
  ParseCommandLine(base::size(argv), argv, &flags);
  EXPECT_EQ(base::TimeDelta::FromSeconds(0), flags.max_spread_time);
  EXPECT_TRUE(flags.crash_directory.empty());
  EXPECT_FALSE(flags.ignore_rate_limits);
  EXPECT_FALSE(flags.ignore_hold_off_time);
  EXPECT_FALSE(flags.allow_dev_sending);
  EXPECT_FALSE(flags.ignore_pause_file);
  EXPECT_FALSE(flags.upload_old_reports);
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_IgnoreRateLimits) {
  const char* argv[] = {"crash_sender", "--ignore_rate_limits"};
  base::CommandLine command_line(base::size(argv), argv);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      &command_line);
  CommandLineFlags flags;
  ParseCommandLine(base::size(argv), argv, &flags);
  EXPECT_EQ(flags.max_spread_time.InSeconds(), kMaxSpreadTimeInSeconds);
  EXPECT_TRUE(flags.crash_directory.empty());
  EXPECT_TRUE(flags.ignore_rate_limits);
  EXPECT_FALSE(flags.ignore_hold_off_time);
  EXPECT_FALSE(flags.allow_dev_sending);
  EXPECT_FALSE(flags.ignore_pause_file);
  EXPECT_FALSE(flags.upload_old_reports);
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_IgnoreHoldOffTime) {
  const char* argv[] = {"crash_sender", "--ignore_hold_off_time"};
  base::CommandLine command_line(base::size(argv), argv);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      &command_line);
  CommandLineFlags flags;
  ParseCommandLine(base::size(argv), argv, &flags);
  EXPECT_EQ(flags.max_spread_time.InSeconds(), kMaxSpreadTimeInSeconds);
  EXPECT_TRUE(flags.crash_directory.empty());
  EXPECT_FALSE(flags.ignore_rate_limits);
  EXPECT_TRUE(flags.ignore_hold_off_time);
  EXPECT_FALSE(flags.allow_dev_sending);
  EXPECT_FALSE(flags.ignore_pause_file);
  EXPECT_FALSE(flags.upload_old_reports);
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_CrashDirectory) {
  const char* argv[] = {"crash_sender", "--crash_directory=/tmp"};
  base::CommandLine command_line(base::size(argv), argv);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      &command_line);
  CommandLineFlags flags;
  ParseCommandLine(base::size(argv), argv, &flags);
  EXPECT_EQ(flags.max_spread_time.InSeconds(), kMaxSpreadTimeInSeconds);
  EXPECT_EQ(flags.crash_directory, "/tmp");
  EXPECT_FALSE(flags.ignore_rate_limits);
  EXPECT_FALSE(flags.ignore_hold_off_time);
  EXPECT_FALSE(flags.allow_dev_sending);
  EXPECT_FALSE(flags.ignore_pause_file);
  EXPECT_FALSE(flags.upload_old_reports);
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_Dev) {
  const char* argv[] = {"crash_sender", "--dev"};
  base::CommandLine command_line(base::size(argv), argv);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      &command_line);
  CommandLineFlags flags;
  ParseCommandLine(base::size(argv), argv, &flags);
  EXPECT_EQ(flags.max_spread_time.InSeconds(), kMaxSpreadTimeInSeconds);
  EXPECT_TRUE(flags.crash_directory.empty());
  EXPECT_FALSE(flags.ignore_rate_limits);
  EXPECT_FALSE(flags.ignore_hold_off_time);
  EXPECT_TRUE(flags.allow_dev_sending);
  EXPECT_FALSE(flags.ignore_pause_file);
  EXPECT_FALSE(flags.upload_old_reports);
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_IgnorePauseFile) {
  const char* argv[] = {"crash_sender", "--ignore_pause_file"};
  base::CommandLine command_line(base::size(argv), argv);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      &command_line);
  CommandLineFlags flags;
  ParseCommandLine(base::size(argv), argv, &flags);
  EXPECT_EQ(flags.max_spread_time.InSeconds(), kMaxSpreadTimeInSeconds);
  EXPECT_TRUE(flags.crash_directory.empty());
  EXPECT_FALSE(flags.ignore_rate_limits);
  EXPECT_FALSE(flags.ignore_hold_off_time);
  EXPECT_FALSE(flags.allow_dev_sending);
  EXPECT_TRUE(flags.ignore_pause_file);
  EXPECT_FALSE(flags.upload_old_reports);
}

TEST_F(CrashSenderUtilTest, ParseCommandLine_UploadOldReports) {
  const char* argv[] = {"crash_sender", "--upload_old_reports"};
  base::CommandLine command_line(base::size(argv), argv);
  brillo::FlagHelper::GetInstance()->set_command_line_for_testing(
      &command_line);
  CommandLineFlags flags;
  ParseCommandLine(base::size(argv), argv, &flags);
  EXPECT_EQ(flags.max_spread_time.InSeconds(), kMaxSpreadTimeInSeconds);
  EXPECT_TRUE(flags.crash_directory.empty());
  EXPECT_FALSE(flags.ignore_rate_limits);
  EXPECT_FALSE(flags.ignore_hold_off_time);
  EXPECT_FALSE(flags.allow_dev_sending);
  EXPECT_FALSE(flags.ignore_pause_file);
  EXPECT_TRUE(flags.upload_old_reports);
}

TEST_F(CrashSenderUtilTest, IsMock) {
  EXPECT_FALSE(IsMock());
  ASSERT_TRUE(SetMockCrashSending(false));
  EXPECT_TRUE(IsMock());
  EXPECT_FALSE(IsMockSuccessful());
  ASSERT_TRUE(SetMockCrashSending(true));
  EXPECT_TRUE(IsMock());
  EXPECT_TRUE(IsMockSuccessful());
}

TEST_F(CrashSenderUtilTest, DoesPauseFileExist) {
  EXPECT_FALSE(DoesPauseFileExist());

  ASSERT_TRUE(test_util::CreateFile(paths::Get(paths::kPauseCrashSending), ""));
  EXPECT_TRUE(DoesPauseFileExist());
}

TEST_F(CrashSenderUtilTest, GetImageType) {
  EXPECT_EQ("", GetImageType());
  ASSERT_TRUE(SetMockCrashSending(false));
  EXPECT_EQ("mock-fail", GetImageType());
  ASSERT_TRUE(test_util::CreateFile(paths::Get(paths::kLeaveCoreFile), ""));
  EXPECT_EQ("dev", GetImageType());
  ASSERT_TRUE(test_util::CreateFile(
      paths::GetAt(paths::kEtcDirectory, paths::kLsbRelease),
      "CHROMEOS_RELEASE_TRACK=testimage-channel"));
  EXPECT_EQ("test", GetImageType());
}

TEST_F(CrashSenderUtilTest, GetBasePartOfCrashFile) {
  EXPECT_EQ("1", GetBasePartHelper("1"));
  EXPECT_EQ("1.2", GetBasePartHelper("1.2"));
  EXPECT_EQ("1.2.3", GetBasePartHelper("1.2.3"));
  EXPECT_EQ("1.2.3.4", GetBasePartHelper("1.2.3.4"));
  EXPECT_EQ("1.2.3.4", GetBasePartHelper("1.2.3.4.log"));
  EXPECT_EQ("1.2.3.4", GetBasePartHelper("1.2.3.4.log"));
  EXPECT_EQ("1.2.3.4", GetBasePartHelper("1.2.3.4.log.tar"));
  EXPECT_EQ("1.2.3.4", GetBasePartHelper("1.2.3.4.log.tar.gz"));
  // Directory should be preserved.
  EXPECT_EQ("/d/1.2", GetBasePartHelper("/d/1.2"));
  EXPECT_EQ("/d/1.2.3.4", GetBasePartHelper("/d/1.2.3.4.log"));
  // Dots in directory name should not affect the function.
  EXPECT_EQ("/d.d.d.d/1.2.3.4", GetBasePartHelper("/d.d.d.d/1.2.3.4.log"));
}

TEST_F(CrashSenderUtilTest, RemoveOrphanedCrashFiles) {
  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(crash_directory));

  const base::FilePath new_log = crash_directory.Append("0.0.0.0.log");
  const base::FilePath old1_log = crash_directory.Append("1.1.1.1.log");
  const base::FilePath old1_meta = crash_directory.Append("1.1.1.1.meta");
  const base::FilePath old2_log = crash_directory.Append("2.2.2.2.log");
  const base::FilePath old3_log = crash_directory.Append("3.3.3.3.log");
  const base::FilePath old4_log = crash_directory.Append("4.log");

  base::Time now = base::Time::Now();

  // new_log is new thus should not be removed.
  ASSERT_TRUE(test_util::CreateFile(new_log, ""));

  // old1_log is old but comes with the meta file thus should not be removed.
  ASSERT_TRUE(test_util::CreateFile(old1_log, ""));
  ASSERT_TRUE(test_util::CreateFile(old1_meta, ""));
  ASSERT_TRUE(TouchFileHelper(old1_log, now - base::TimeDelta::FromHours(24)));
  ASSERT_TRUE(TouchFileHelper(old1_meta, now - base::TimeDelta::FromHours(24)));

  // old2_log is old without the meta file thus should be removed.
  ASSERT_TRUE(test_util::CreateFile(old2_log, ""));
  ASSERT_TRUE(TouchFileHelper(old2_log, now - base::TimeDelta::FromHours(24)));

  // old3_log is very old without the meta file thus should be removed.
  ASSERT_TRUE(test_util::CreateFile(old3_log, ""));
  ASSERT_TRUE(TouchFileHelper(old3_log, now - base::TimeDelta::FromDays(365)));

  // old4_log is misnamed, but should be removed since it's old.
  ASSERT_TRUE(test_util::CreateFile(old4_log, ""));
  ASSERT_TRUE(TouchFileHelper(old4_log, now - base::TimeDelta::FromHours(24)));

  RemoveOrphanedCrashFiles(crash_directory);

  // Check what files were removed.
  EXPECT_TRUE(base::PathExists(new_log));
  EXPECT_TRUE(base::PathExists(old1_log));
  EXPECT_TRUE(base::PathExists(old1_meta));
  EXPECT_FALSE(base::PathExists(old2_log));
  EXPECT_FALSE(base::PathExists(old3_log));
  EXPECT_FALSE(base::PathExists(old4_log));
}

TEST_F(CrashSenderUtilTest, ChooseAction) {
  ASSERT_TRUE(SetConditions(kOfficialBuild, kSignInMode, kMetricsEnabled));

  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(CreateDirectory(crash_directory));
  ASSERT_TRUE(CreateTestCrashFiles(crash_directory));
  MetricsLibraryMock* raw_metrics_lib = metrics_lib_.get();

  Sender::Options options;
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);

  std::string reason;
  CrashInfo info;
  // The following files should be sent.
  EXPECT_CALL(*raw_metrics_lib,
              SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason", _, _))
      .Times(0);
  EXPECT_EQ(Sender::kSend, sender.ChooseAction(good_meta_, &reason, &info));
  EXPECT_EQ(Sender::kSend,
            sender.ChooseAction(recent_os_meta_, &reason, &info));
  EXPECT_EQ(Sender::kSend, sender.ChooseAction(absolute_meta_, &reason, &info));
  // Verify that RemoveReason wasn't sent
  testing::Mock::VerifyAndClearExpectations(raw_metrics_lib);

  // Sanity check that the valid crash info is returned.
  std::string value;
  EXPECT_EQ(absolute_log_.value(), info.payload_file.value());
  EXPECT_EQ("log", info.payload_kind);
  EXPECT_TRUE(info.metadata.GetString("payload", &value));

  const base::FilePath processing = good_meta_.ReplaceExtension(".processing");
  // ChooseAction was successful, so it should remove the file.
  EXPECT_FALSE(base::PathExists(processing));

  // If a ".processing" file exists, the meta file shouldn't be uploaded.
  ASSERT_TRUE(test_util::CreateFile(processing, ""));

  // Following calls should be in order.
  testing::InSequence seq;
  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kProcessingFileExists, Sender::kSendReasonCount));
  EXPECT_EQ(Sender::kRemove, sender.ChooseAction(good_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr(".processing file already exists"));
  ASSERT_TRUE(base::DeleteFile(processing, /*recursive=*/false));

  // The following file should be removed.
  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kAlreadyUploaded, Sender::kSendReasonCount));
  EXPECT_EQ(Sender::kRemove,
            sender.ChooseAction(uploaded_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Removing already-uploaded crash"));
  EXPECT_FALSE(
      base::PathExists(uploaded_meta_.ReplaceExtension(".processing")));

  // The following files should be ignored.
  EXPECT_EQ(Sender::kIgnore,
            sender.ChooseAction(new_incomplete_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Recent incomplete metadata"));
  EXPECT_FALSE(
      base::PathExists(new_incomplete_meta_.ReplaceExtension(".processing")));

  EXPECT_EQ(Sender::kIgnore,
            sender.ChooseAction(new_empty_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Recent incomplete metadata"));
  EXPECT_FALSE(
      base::PathExists(new_empty_meta_.ReplaceExtension(".processing")));

  // Device coredump should be ignored by default.
  EXPECT_EQ(Sender::kIgnore,
            sender.ChooseAction(devcore_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Device coredump upload not allowed"));
  EXPECT_FALSE(base::PathExists(devcore_meta_.ReplaceExtension(".processing")));

  // Device coredump should be sent, if uploading is allowed.
  CreateDeviceCoredumpUploadAllowedFile();
  EXPECT_EQ(Sender::kSend, sender.ChooseAction(devcore_meta_, &reason, &info));

  // The following files should be removed.
  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kPayloadUnspecified, Sender::kSendReasonCount));
  EXPECT_EQ(Sender::kRemove, sender.ChooseAction(empty_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Payload is not found"));
  EXPECT_FALSE(base::PathExists(empty_meta_.ReplaceExtension(".processing")));

  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kUnparseableMetaFile, Sender::kSendReasonCount));
  EXPECT_EQ(Sender::kRemove,
            sender.ChooseAction(corrupted_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Corrupted metadata"));
  EXPECT_FALSE(
      base::PathExists(corrupted_meta_.ReplaceExtension(".processing")));

  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kPayloadNonexistent, Sender::kSendReasonCount));
  EXPECT_EQ(Sender::kRemove,
            sender.ChooseAction(nonexistent_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Missing payload"));
  EXPECT_FALSE(
      base::PathExists(nonexistent_meta_.ReplaceExtension(".processing")));

  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kPayloadKindUnknown, Sender::kSendReasonCount));
  EXPECT_EQ(Sender::kRemove,
            sender.ChooseAction(unknown_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Unknown kind"));
  EXPECT_FALSE(base::PathExists(unknown_meta_.ReplaceExtension(".processing")));

  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kOldIncompleteMeta, Sender::kSendReasonCount));
  EXPECT_EQ(Sender::kRemove,
            sender.ChooseAction(old_incomplete_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Removing old incomplete metadata"));
  EXPECT_FALSE(
      base::PathExists(old_incomplete_meta_.ReplaceExtension(".processing")));

  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kOSVersionTooOld, Sender::kSendReasonCount));
  EXPECT_EQ(Sender::kRemove, sender.ChooseAction(old_os_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Old OS version"));
  EXPECT_FALSE(base::PathExists(old_os_meta_.ReplaceExtension(".processing")));

  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kPayloadAbsolute, Sender::kSendReasonCount));
  EXPECT_EQ(Sender::kRemove,
            sender.ChooseAction(root_payload_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("payload path is absolute"));
  EXPECT_FALSE(
      base::PathExists(root_payload_meta_.ReplaceExtension(".processing")));

  EXPECT_CALL(*raw_metrics_lib,
              SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                            Sender::kLargeMetaFile, Sender::kSendReasonCount));
  EXPECT_EQ(Sender::kRemove, sender.ChooseAction(large_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Metadata file is unusually large"));
  EXPECT_FALSE(base::PathExists(large_meta_.ReplaceExtension(".processing")));

  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kNotOfficialImage, Sender::kSendReasonCount));
  ASSERT_TRUE(SetConditions(kUnofficialBuild, kSignInMode, kMetricsEnabled,
                            raw_metrics_lib));
  EXPECT_EQ(Sender::kRemove, sender.ChooseAction(good_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Not an official OS version"));
  EXPECT_FALSE(base::PathExists(good_meta_.ReplaceExtension(".processing")));

  ASSERT_TRUE(SetConditions(kOfficialBuild, kSignInMode, kMetricsDisabled,
                            raw_metrics_lib));
  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kNoMetricsConsent, Sender::kSendReasonCount));
  EXPECT_EQ(Sender::kRemove, sender.ChooseAction(good_meta_, &reason, &info));
  EXPECT_THAT(reason, HasSubstr("Crash reporting is disabled"));
  EXPECT_FALSE(base::PathExists(good_meta_.ReplaceExtension(".processing")));

  // Valid crash files should be kept in the guest mode.
  ASSERT_TRUE(SetConditions(kOfficialBuild, kGuestMode, kMetricsDisabled,
                            raw_metrics_lib));
  EXPECT_EQ(Sender::kIgnore, sender.ChooseAction(good_meta_, &reason, &info));
}

TEST_F(CrashSenderUtilDeathTest, ChooseActionCrash) {
  ASSERT_TRUE(SetConditions(kOfficialBuild, kSignInMode, kMetricsEnabled));

  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(CreateDirectory(crash_directory));
  ASSERT_TRUE(CreateTestCrashFiles(crash_directory));

  Sender::Options options;
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);

  ASSERT_TRUE(SetMockCrashSending(true));
  sender.SetCrashDuringSendForTesting(true);

  std::string reason;
  CrashInfo info;
  EXPECT_DEATH(sender.ChooseAction(good_meta_, &reason, &info),
               "crashing as requested");

  // ChooseAction crashed so the file should remain.
  EXPECT_TRUE(base::PathExists(good_meta_.ReplaceExtension(".processing")));
}

TEST_F(CrashSenderUtilTest, ChooseActionDevMode) {
  // If we set allow_dev_sending, then the OS check will be skipped.
  ASSERT_TRUE(SetConditions(kUnofficialBuild, kSignInMode, kMetricsEnabled));

  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(CreateDirectory(crash_directory));
  ASSERT_TRUE(CreateTestCrashFiles(crash_directory));

  Sender::Options options;
  options.allow_dev_sending = true;
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);

  std::string reason;
  CrashInfo info;

  EXPECT_EQ(Sender::kSend, sender.ChooseAction(good_meta_, &reason, &info));
  EXPECT_EQ(Sender::kSend, sender.ChooseAction(old_os_meta_, &reason, &info));
}

TEST_F(CrashSenderUtilTest, ChooseActionUploadOldReports) {
  ASSERT_TRUE(SetConditions(kOfficialBuild, kSignInMode, kMetricsEnabled));

  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(CreateDirectory(crash_directory));
  ASSERT_TRUE(CreateTestCrashFiles(crash_directory));

  Sender::Options options;
  options.upload_old_reports = true;
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);

  std::string reason;
  CrashInfo info;

  EXPECT_EQ(Sender::kSend, sender.ChooseAction(old_os_meta_, &reason, &info));
}

TEST_F(CrashSenderUtilTest, RemoveAndPickCrashFiles) {
  auto mock =
      std::make_unique<org::chromium::SessionManagerInterfaceProxyMock>();
  test_util::SetActiveSessions(mock.get(),
                               {{"user1", "hash1"}, {"user2", "hash2"}});
  MetricsLibraryMock* raw_metrics_lib = metrics_lib_.get();

  Sender::Options options;
  options.session_manager_proxy = mock.release();
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);

  ASSERT_TRUE(SetConditions(kOfficialBuild, kSignInMode, kMetricsEnabled,
                            raw_metrics_lib));

  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(CreateDirectory(crash_directory));
  ASSERT_TRUE(CreateTestCrashFiles(crash_directory));

  std::vector<MetaFile> to_send;
  sender.RemoveAndPickCrashFiles(crash_directory, &to_send);

  // Check what files were removed.
  EXPECT_TRUE(base::PathExists(good_meta_));
  EXPECT_TRUE(base::PathExists(good_log_));
  EXPECT_TRUE(base::PathExists(absolute_meta_));
  EXPECT_TRUE(base::PathExists(absolute_log_));
  EXPECT_FALSE(base::PathExists(uploaded_meta_));
  EXPECT_FALSE(base::PathExists(uploaded_log_));
  EXPECT_TRUE(base::PathExists(new_incomplete_meta_));
  EXPECT_TRUE(base::PathExists(new_empty_meta_));
  EXPECT_TRUE(base::PathExists(recent_os_meta_));
  EXPECT_TRUE(base::PathExists(recent_os_log_));
  EXPECT_FALSE(base::PathExists(empty_meta_));
  EXPECT_FALSE(base::PathExists(corrupted_meta_));
  EXPECT_FALSE(base::PathExists(nonexistent_meta_));
  EXPECT_FALSE(base::PathExists(unknown_meta_));
  EXPECT_FALSE(base::PathExists(unknown_xxx_));
  EXPECT_FALSE(base::PathExists(old_incomplete_meta_));
  EXPECT_FALSE(base::PathExists(old_os_meta_));
  EXPECT_FALSE(base::PathExists(root_payload_meta_));
  // Check what files were picked for sending.
  EXPECT_EQ(3, to_send.size());
  EXPECT_EQ(good_meta_.value(), to_send[0].first.value());
  EXPECT_EQ(absolute_meta_.value(), to_send[1].first.value());
  EXPECT_EQ(recent_os_meta_.value(), to_send[2].first.value());

  // Sanity check that the valid crash info is returned.
  std::string value;
  EXPECT_EQ(good_log_.value(), to_send[0].second.payload_file.value());
  EXPECT_EQ("log", to_send[0].second.payload_kind);
  EXPECT_TRUE(to_send[0].second.metadata.GetString("payload", &value));

  // All crash files should be removed for an unofficial build.
  ASSERT_TRUE(CreateTestCrashFiles(crash_directory));
  ASSERT_TRUE(SetConditions(kUnofficialBuild, kSignInMode, kMetricsEnabled,
                            raw_metrics_lib));
  to_send.clear();
  sender.RemoveAndPickCrashFiles(crash_directory, &to_send);
  EXPECT_TRUE(base::IsDirectoryEmpty(crash_directory));
  EXPECT_TRUE(to_send.empty());

  // All crash files should be removed if metrics are disabled.
  ASSERT_TRUE(CreateTestCrashFiles(crash_directory));
  ASSERT_TRUE(SetConditions(kOfficialBuild, kSignInMode, kMetricsDisabled,
                            raw_metrics_lib));
  to_send.clear();
  sender.RemoveAndPickCrashFiles(crash_directory, &to_send);
  EXPECT_TRUE(base::IsDirectoryEmpty(crash_directory));
  EXPECT_TRUE(to_send.empty());

  // Valid crash files should be kept in the guest mode, thus the directory
  // won't be empty. None should be selected for sending.
  ASSERT_TRUE(CreateTestCrashFiles(crash_directory));
  ASSERT_TRUE(SetConditions(kOfficialBuild, kGuestMode, kMetricsDisabled,
                            raw_metrics_lib));
  to_send.clear();
  sender.RemoveAndPickCrashFiles(crash_directory, &to_send);
  EXPECT_FALSE(base::IsDirectoryEmpty(crash_directory));
  EXPECT_TRUE(to_send.empty());

  // devcore_meta_ should be included in to_send, if uploading of device
  // coredumps is allowed.
  ASSERT_TRUE(CreateTestCrashFiles(crash_directory));
  ASSERT_TRUE(SetConditions(kOfficialBuild, kSignInMode, kMetricsEnabled,
                            raw_metrics_lib));
  CreateDeviceCoredumpUploadAllowedFile();
  to_send.clear();
  sender.RemoveAndPickCrashFiles(crash_directory, &to_send);
  EXPECT_EQ(4, to_send.size());
  EXPECT_EQ(devcore_meta_.value(), to_send[3].first.value());
}

TEST_F(CrashSenderUtilTest, RemoveReportFiles) {
  Sender::Options options;
  MetricsLibraryMock* raw_metrics_lib = metrics_lib_.get();
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);

  EXPECT_CALL(*raw_metrics_lib,
              SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                            Sender::kTotalRemoval, Sender::kSendReasonCount))
      .Times(1);

  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(crash_directory));

  const base::FilePath foo_meta = crash_directory.Append("foo.meta");
  const base::FilePath foo_log = crash_directory.Append("foo.log");
  const base::FilePath foo_dmp = crash_directory.Append("foo.dmp");
  const base::FilePath bar_log = crash_directory.Append("bar.log");

  ASSERT_TRUE(test_util::CreateFile(foo_meta, ""));
  ASSERT_TRUE(test_util::CreateFile(foo_log, ""));
  ASSERT_TRUE(test_util::CreateFile(foo_dmp, ""));
  ASSERT_TRUE(test_util::CreateFile(bar_log, ""));
  // This should remove foo.*.
  sender.RemoveReportFiles(foo_meta);
  // This should do nothing because the suffix is not ".meta".
  sender.RemoveReportFiles(bar_log);

  // Check what files were removed.
  EXPECT_FALSE(base::PathExists(foo_meta));
  EXPECT_FALSE(base::PathExists(foo_log));
  EXPECT_FALSE(base::PathExists(foo_dmp));
  EXPECT_TRUE(base::PathExists(bar_log));
}

TEST_F(CrashSenderUtilTest, FailRemoveReportFilesSendsMetric) {
  Sender::Options options;
  EXPECT_CALL(*metrics_lib_,
              SendCrosEventToUMA("Crash.Sender.AttemptedCrashRemoval"))
      .WillOnce(Return(true));
  EXPECT_CALL(*metrics_lib_,
              SendCrosEventToUMA("Crash.Sender.FailedCrashRemoval"))
      .WillOnce(Return(true));

  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);

  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(crash_directory));

  const base::FilePath foo_meta = crash_directory.Append("foo.meta");
  ASSERT_TRUE(test_util::CreateFile(foo_meta, ""));
  const base::FilePath foo_log = crash_directory.Append("foo.log");
  ASSERT_TRUE(test_util::CreateFile(foo_log, ""));

  // chmod the file so RemoveReportFiles fails
  ASSERT_EQ(chmod(crash_directory.value().c_str(), 0500), 0);

  sender.RemoveReportFiles(foo_meta);

  // Clean up after ourselves
  EXPECT_EQ(chmod(crash_directory.value().c_str(), 0700), 0);
  EXPECT_TRUE(base::DeleteFile(crash_directory, true));
}

TEST_F(CrashSenderUtilTest, GetMetaFiles) {
  const base::FilePath crash_directory =
      paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(crash_directory));

  // Use unsorted file names, to check that GetMetaFiles() sort files by
  // timestamps, not file names.
  const base::FilePath meta_1 = crash_directory.Append("a.meta");
  const base::FilePath meta_2 = crash_directory.Append("s.meta");
  const base::FilePath meta_3 = crash_directory.Append("d.meta");
  const base::FilePath meta_4 = crash_directory.Append("f.meta");
  // This one should not appear in the result.
  const base::FilePath metal_5 = crash_directory.Append("g.metal");

  ASSERT_TRUE(test_util::CreateFile(meta_1, ""));
  ASSERT_TRUE(test_util::CreateFile(meta_2, ""));
  ASSERT_TRUE(test_util::CreateFile(meta_3, ""));
  ASSERT_TRUE(test_util::CreateFile(meta_4, ""));
  ASSERT_TRUE(test_util::CreateFile(metal_5, ""));

  // Change timestamps so that meta_1 is the newest and metal_5 is the oldest.
  base::Time now = base::Time::Now();
  ASSERT_TRUE(TouchFileHelper(meta_1, now - base::TimeDelta::FromHours(1)));
  ASSERT_TRUE(TouchFileHelper(meta_2, now - base::TimeDelta::FromHours(2)));
  ASSERT_TRUE(TouchFileHelper(meta_3, now - base::TimeDelta::FromHours(3)));
  ASSERT_TRUE(TouchFileHelper(meta_4, now - base::TimeDelta::FromHours(4)));
  ASSERT_TRUE(TouchFileHelper(metal_5, now - base::TimeDelta::FromHours(5)));

  std::vector<base::FilePath> meta_files = GetMetaFiles(crash_directory);
  ASSERT_EQ(4, meta_files.size());
  // Confirm that files are sorted in the old-to-new order.
  EXPECT_EQ(meta_4.value(), meta_files[0].value());
  EXPECT_EQ(meta_3.value(), meta_files[1].value());
  EXPECT_EQ(meta_2.value(), meta_files[2].value());
  EXPECT_EQ(meta_1.value(), meta_files[3].value());
}

TEST_F(CrashSenderUtilTest, GetBaseNameFromMetadata) {
  brillo::KeyValueStore metadata;
  metadata.LoadFromString("");
  EXPECT_EQ("", GetBaseNameFromMetadata(metadata, "payload").value());

  metadata.LoadFromString("payload=test.log\n");
  EXPECT_EQ("test.log", GetBaseNameFromMetadata(metadata, "payload").value());

  metadata.LoadFromString("payload=/foo/test.log\n");
  EXPECT_EQ("test.log", GetBaseNameFromMetadata(metadata, "payload").value());
}

TEST_F(CrashSenderUtilTest, GetKindFromPayloadPath) {
  EXPECT_EQ("", GetKindFromPayloadPath(base::FilePath()));
  EXPECT_EQ("", GetKindFromPayloadPath(base::FilePath("foo")));
  EXPECT_EQ("log", GetKindFromPayloadPath(base::FilePath("foo.log")));
  // "dmp" is a special case.
  EXPECT_EQ("minidump", GetKindFromPayloadPath(base::FilePath("foo.dmp")));

  // ".gz" should be ignored.
  EXPECT_EQ("log", GetKindFromPayloadPath(base::FilePath("foo.log.gz")));
  EXPECT_EQ("minidump", GetKindFromPayloadPath(base::FilePath("foo.dmp.gz")));
  EXPECT_EQ("", GetKindFromPayloadPath(base::FilePath("foo.gz")));

  // The directory name should not afect the function.
  EXPECT_EQ("minidump",
            GetKindFromPayloadPath(base::FilePath("/1.2.3/foo.dmp.gz")));
}

TEST_F(CrashSenderUtilTest, ParseMetadata) {
  brillo::KeyValueStore metadata;
  std::string value;
  EXPECT_TRUE(ParseMetadata("", &metadata));
  EXPECT_TRUE(ParseMetadata("log=test.log\n", &metadata));
  EXPECT_TRUE(ParseMetadata("#comment\nlog=test.log\n", &metadata));

  EXPECT_TRUE(metadata.GetString("log", &value));
  // This will clear the previouly parsed data.
  EXPECT_TRUE(ParseMetadata("payload=test.dmp\n", &metadata));
  EXPECT_FALSE(metadata.GetString("log", &value));

  // Underscores, dashes, and periods should allowed, as Chrome uses them.
  // https://crbug.com/821530.
  EXPECT_TRUE(ParseMetadata("abcABC012_.-=test.log\n", &metadata));
  EXPECT_TRUE(metadata.GetString("abcABC012_.-", &value));
  EXPECT_EQ("test.log", value);

  // Invalid metadata should be detected.
  EXPECT_FALSE(ParseMetadata("=test.log\n", &metadata));
  EXPECT_FALSE(ParseMetadata("***\n", &metadata));
  EXPECT_FALSE(ParseMetadata("***=test.log\n", &metadata));
  EXPECT_FALSE(ParseMetadata("log\n", &metadata));
}

TEST_F(CrashSenderUtilTest, IsCompleteMetadata) {
  brillo::KeyValueStore metadata;
  metadata.LoadFromString("");
  EXPECT_FALSE(IsCompleteMetadata(metadata));

  metadata.LoadFromString("log=test.log\n");
  EXPECT_FALSE(IsCompleteMetadata(metadata));

  metadata.LoadFromString("log=test.log\ndone=1\n");
  EXPECT_TRUE(IsCompleteMetadata(metadata));

  metadata.LoadFromString("done=1\n");
  EXPECT_TRUE(IsCompleteMetadata(metadata));
}

TEST_F(CrashSenderUtilTest, IsTimestampNewEnough) {
  base::FilePath file;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(test_dir_, &file));

  // Should be new enough as it's just created.
  ASSERT_TRUE(IsTimestampNewEnough(file));

  // Make it older than 24 hours.
  const base::Time now = base::Time::Now();
  ASSERT_TRUE(TouchFileHelper(file, now - base::TimeDelta::FromHours(25)));

  // Should be no longer new enough.
  ASSERT_FALSE(IsTimestampNewEnough(file));
}

TEST_F(CrashSenderUtilTest, IsBelowRateReachesMaxRate) {
  const int kMaxRate = 3;
  const int kMaxBytes = 50;
  const base::FilePath timestamp_dir =
      test_dir_.Append("IsBelowRateReachesMaxRate");

  EXPECT_TRUE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));
  RecordSendAttempt(timestamp_dir, kMaxBytes - 5);
  EXPECT_TRUE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));
  RecordSendAttempt(timestamp_dir, kMaxBytes - 5);
  // Exceeds max bytes; should be allowed to upload since we have not hit max
  // rate.
  EXPECT_TRUE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));
  RecordSendAttempt(timestamp_dir, kMaxBytes - 5);

  // Should not pass the rate + byte limit.
  EXPECT_FALSE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));

  // Three files should be created for tracking timestamps.
  std::vector<base::FilePath> files = GetFileNamesIn(timestamp_dir);
  ASSERT_EQ(3, files.size());

  const base::Time now = base::Time::Now();

  // Make one of them older than 24 hours.
  ASSERT_TRUE(TouchFileHelper(files[0], now - base::TimeDelta::FromHours(25)));

  // It should now pass the rate limit.
  EXPECT_TRUE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));
  // The old file should now be gone.
  EXPECT_TRUE(!base::PathExists(files[0]));
}

TEST_F(CrashSenderUtilTest, IsBelowRateReachesMaxBytes) {
  const int kMaxRate = 3;
  const int kMaxBytes = 100;
  const base::FilePath timestamp_dir =
      test_dir_.Append("IsBelowRateReachesMaxBytes");

  EXPECT_TRUE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));
  RecordSendAttempt(timestamp_dir, 50);
  EXPECT_TRUE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));
  RecordSendAttempt(timestamp_dir, 20);
  EXPECT_TRUE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));
  RecordSendAttempt(timestamp_dir, 5);
  // Exceeds max rate, but passes because it's below max bytes.
  EXPECT_TRUE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));
  RecordSendAttempt(timestamp_dir, 5);
  EXPECT_TRUE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));
  RecordSendAttempt(timestamp_dir, 20);

  // Exceeds max bytes.
  EXPECT_FALSE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));

  // Make one file older than 24 hours, and we should get some bandwidth
  // marked available again.
  std::vector<base::FilePath> files = GetFileNamesIn(timestamp_dir);
  ASSERT_EQ(5, files.size());
  const base::Time now = base::Time::Now();
  ASSERT_TRUE(TouchFileHelper(files[0], now - base::TimeDelta::FromHours(25)));
  EXPECT_TRUE(IsBelowRate(timestamp_dir, kMaxRate, kMaxBytes));
}

TEST_F(CrashSenderUtilTest, GetSleepTime) {
  const base::FilePath meta_file = test_dir_.Append("test.meta");
  base::TimeDelta max_spread_time = base::TimeDelta::FromSeconds(0);

  // This should fail since meta_file does not exist.
  base::TimeDelta sleep_time;
  EXPECT_FALSE(
      GetSleepTime(meta_file, max_spread_time, kMaxHoldOffTime, &sleep_time));

  ASSERT_TRUE(test_util::CreateFile(meta_file, ""));

  // sleep_time should be close enough to kMaxHoldOffTime since the meta file
  // was just created, but 10% error is allowed just in case.
  EXPECT_TRUE(
      GetSleepTime(meta_file, max_spread_time, kMaxHoldOffTime, &sleep_time));
  EXPECT_NEAR(kMaxHoldOffTime.InSecondsF(), sleep_time.InSecondsF(),
              kMaxHoldOffTime.InSecondsF() * 0.1);

  // Zero hold-off time and zero sleep time should always give zero sleep time.
  EXPECT_TRUE(GetSleepTime(meta_file, max_spread_time,
                           base::TimeDelta::FromSeconds(0) /*hold_off_time*/,
                           &sleep_time));
  EXPECT_EQ(base::TimeDelta::FromSeconds(0), sleep_time);

  // Even if file is new, a zero hold-off time means we choose a time between
  // 0 and max_spread_time.
  ASSERT_TRUE(TouchFileHelper(meta_file, base::Time::Now()));
  EXPECT_TRUE(GetSleepTime(
      meta_file, base::TimeDelta::FromSeconds(60) /*max_spread_time*/,
      base::TimeDelta::FromSeconds(0) /*hold_off_time*/, &sleep_time));
  EXPECT_LE(base::TimeDelta::FromSeconds(0), sleep_time);
  EXPECT_GE(base::TimeDelta::FromSeconds(60), sleep_time);

  // Make the meta file old enough so hold-off time is not necessary.
  const base::Time now = base::Time::Now();
  ASSERT_TRUE(TouchFileHelper(meta_file, now - kMaxHoldOffTime));

  // sleep_time should always be 0, since max_spread_time is set to 0.
  EXPECT_TRUE(
      GetSleepTime(meta_file, max_spread_time, kMaxHoldOffTime, &sleep_time));
  EXPECT_EQ(base::TimeDelta::FromSeconds(0), sleep_time);

  // sleep_time should be in range [0, 10].
  max_spread_time = base::TimeDelta::FromSeconds(10);
  EXPECT_TRUE(
      GetSleepTime(meta_file, max_spread_time, kMaxHoldOffTime, &sleep_time));
  EXPECT_LE(base::TimeDelta::FromSeconds(0), sleep_time);
  EXPECT_GE(base::TimeDelta::FromSeconds(10), sleep_time);

  // If the meta file is current, the minimum sleep time should be
  // kMaxHoldOffTime but the maximum is still max_spread_time.
  max_spread_time = base::TimeDelta::FromSeconds(60);
  ASSERT_TRUE(TouchFileHelper(meta_file, base::Time::Now()));
  EXPECT_TRUE(
      GetSleepTime(meta_file, max_spread_time, kMaxHoldOffTime, &sleep_time));
  // 0.9 in case we got preempted for 3 seconds between the file touch and the
  // GetSleepTime().
  EXPECT_LE(kMaxHoldOffTime * 0.9, sleep_time);
  EXPECT_GE(base::TimeDelta::FromSeconds(60), sleep_time);
}

TEST_F(CrashSenderUtilTest, SortReports) {
  // Crashes from oldest to youngest will be a, b, c.
  CrashInfo crash_info_a;
  EXPECT_TRUE(base::Time::FromString("15 Nov 2018 12:45:26 GMT",
                                     &crash_info_a.last_modified));
  crash_info_a.metadata.SetString("order", "1");
  MetaFile file_a(base::FilePath("a"), std::move(crash_info_a));

  CrashInfo crash_info_b;
  EXPECT_TRUE(base::Time::FromString("7 Feb 2019 12:45:26 GMT",
                                     &crash_info_b.last_modified));
  crash_info_b.metadata.SetString("order", "2");
  MetaFile file_b(base::FilePath("b"), std::move(crash_info_b));

  CrashInfo crash_info_c;
  EXPECT_TRUE(base::Time::FromString("7 Feb 2019 12:48:26 GMT",
                                     &crash_info_c.last_modified));
  crash_info_c.metadata.SetString("order", "3");
  MetaFile file_c(base::FilePath("c"), std::move(crash_info_c));

  // Add out of order
  std::vector<MetaFile> crashes;
  crashes.emplace_back(std::move(file_c));
  crashes.emplace_back(std::move(file_b));
  crashes.emplace_back(std::move(file_a));
  SortReports(&crashes);

  ASSERT_EQ(crashes.size(), 3);

  EXPECT_EQ(crashes[0].first, base::FilePath("a"));
  std::string order_string;
  EXPECT_TRUE(crashes[0].second.metadata.GetString("order", &order_string));
  EXPECT_EQ(order_string, "1");

  EXPECT_EQ(crashes[1].first, base::FilePath("b"));
  EXPECT_TRUE(crashes[1].second.metadata.GetString("order", &order_string));
  EXPECT_EQ(order_string, "2");

  EXPECT_EQ(crashes[2].first, base::FilePath("c"));
  EXPECT_TRUE(crashes[2].second.metadata.GetString("order", &order_string));
  EXPECT_EQ(order_string, "3");
}

TEST_F(CrashSenderUtilTest, GetUserCrashDirectories) {
  auto mock =
      std::make_unique<org::chromium::SessionManagerInterfaceProxyMock>();
  test_util::SetActiveSessions(mock.get(),
                               {{"user1", "hash1"}, {"user2", "hash2"}});
  Sender::Options options;
  options.session_manager_proxy = mock.release();
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);

  EXPECT_THAT(
      sender.GetUserCrashDirectories(),
      UnorderedElementsAre(paths::Get("/home/user/hash1/crash"),
                           paths::Get("/home/user/hash2/crash"),
                           paths::Get("/run/daemon-store/crash/hash1"),
                           paths::Get("/run/daemon-store/crash/hash2")));
}

enum MissingFile {
  kNone,
  kPayloadFile,
  kLogFile,
  kTextFile,
  kBinFile,
};

class CreateCrashFormDataTest
    : public CrashSenderUtilTest,
      public ::testing::WithParamInterface<std::tuple<bool, MissingFile>> {
 protected:
  void SetUp() override {
    std::tie(absolute_paths_, missing_file_) = GetParam();
    CrashSenderUtilTest::SetUp();
  }
  bool absolute_paths_;
  MissingFile missing_file_;
};

TEST_P(CreateCrashFormDataTest, TestCreateCrashFormData) {
  const base::FilePath system_dir = paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(system_dir));

  const base::FilePath payload_file_relative("0.0.0.0.payload");
  const base::FilePath payload_file_absolute =
      system_dir.Append(payload_file_relative);
  const std::string payload_contents = "foobar_payload";
  if (missing_file_ != kPayloadFile) {
    ASSERT_TRUE(test_util::CreateFile(payload_file_absolute, payload_contents));
  }
  const base::FilePath& payload_file =
      absolute_paths_ ? payload_file_absolute : payload_file_relative;

  const base::FilePath log_file_relative("0.0.0.0.log");
  const base::FilePath log_file_absolute = system_dir.Append(log_file_relative);
  const std::string log_contents = "foobar_log";
  if (missing_file_ != kLogFile) {
    ASSERT_TRUE(test_util::CreateFile(log_file_absolute, log_contents));
  }
  const base::FilePath& log_file =
      absolute_paths_ ? log_file_absolute : log_file_relative;

  const base::FilePath text_var_file_relative("data.txt");
  const base::FilePath text_var_file_absolute =
      system_dir.Append(text_var_file_relative);
  const std::string text_var_contents = "upload_text_contents";
  if (missing_file_ != kTextFile) {
    ASSERT_TRUE(
        test_util::CreateFile(text_var_file_absolute, text_var_contents));
  }
  const base::FilePath& text_var_file =
      absolute_paths_ ? text_var_file_absolute : text_var_file_relative;

  const base::FilePath file_var_file_relative("data.bin");
  const base::FilePath file_var_file_absolute =
      system_dir.Append(file_var_file_relative);
  const std::string file_var_contents = "upload_file_contents";
  if (missing_file_ != kBinFile) {
    ASSERT_TRUE(
        test_util::CreateFile(file_var_file_absolute, file_var_contents));
  }
  const base::FilePath& file_var_file =
      absolute_paths_ ? file_var_file_absolute : file_var_file_relative;

  brillo::KeyValueStore metadata;
  metadata.SetString("exec_name", "fake_exec_name");
  metadata.SetString("ver", "fake_chromeos_ver");
  metadata.SetString("upload_var_prod", "fake_product");
  metadata.SetString("upload_var_ver", "fake_version");
  metadata.SetString("sig", "fake_sig");
  metadata.SetString("upload_var_guid", "SHOULD_NOT_BE_USED");
  metadata.SetString("upload_var_foovar", "bar");
  metadata.SetString("upload_text_footext", text_var_file.value());
  metadata.SetString("upload_file_log", log_file.value());
  metadata.SetString("upload_file_foofile", file_var_file.value());
  metadata.SetString("error_type", "fake_error");

  CrashDetails details = {
      .meta_file = base::FilePath(system_dir).Append("0.0.0.0.meta"),
      .payload_file = payload_file,
      .payload_kind = "fake_payload",
      .client_id = kFakeClientId,
      .metadata = metadata,
  };

  Sender::Options options;
  options.form_data_boundary = "boundary";

  Sender sender(nullptr, std::make_unique<test_util::AdvancingClock>(),
                options);

  std::unique_ptr<brillo::http::FormData> form_data =
      sender.CreateCrashFormData(details, nullptr);
  if (missing_file_ == kPayloadFile) {
    EXPECT_EQ(form_data, nullptr);
    return;
  }

  brillo::StreamPtr stream = form_data->ExtractDataStream();
  std::vector<uint8_t> data(stream->GetSize());
  ASSERT_TRUE(stream->ReadAllBlocking(data.data(), data.size(), nullptr));

  std::string expected_data = base::StrCat(
      {"--boundary\r\n"
       "Content-Disposition: form-data; name=\"exec_name\"\r\n"
       "\r\n"
       "fake_exec_name\r\n"
       "--boundary\r\n"
       "Content-Disposition: form-data; name=\"board\"\r\n"
       "\r\n"
       "undefined\r\n"
       "--boundary\r\n"
       "Content-Disposition: form-data; name=\"hwclass\"\r\n"
       "\r\n"
       "undefined\r\n"
       "--boundary\r\n"
       "Content-Disposition: form-data; name=\"prod\"\r\n"
       "\r\n"
       "fake_product\r\n"
       "--boundary\r\n"
       "Content-Disposition: form-data; name=\"ver\"\r\n"
       "\r\n"
       "fake_version\r\n"
       "--boundary\r\n"
       "Content-Disposition: form-data; name=\"sig\"\r\n"
       "\r\n"
       "fake_sig\r\n"
       "--boundary\r\n"
       "Content-Disposition: form-data; name=\"sig2\"\r\n"
       "\r\n"
       "fake_sig\r\n"
       "--boundary\r\n"
       "Content-Disposition: form-data; name=\"upload_file_fake_payload\"; "
       "filename=\"0.0.0.0.payload\"\r\n"
       "Content-Transfer-Encoding: binary\r\n"
       "\r\n"
       "foobar_payload\r\n",
       missing_file_ == kBinFile
           ? ""
           : "--boundary\r\n"
             "Content-Disposition: form-data; name=\"foofile\"; "
             "filename=\"data.bin\"\r\n"
             "Content-Transfer-Encoding: binary\r\n"
             "\r\n"
             "upload_file_contents\r\n",
       missing_file_ == kLogFile
           ? ""
           : "--boundary\r\n"
             "Content-Disposition: form-data; name=\"log\"; "
             "filename=\"0.0.0.0.log\"\r\n"
             "Content-Transfer-Encoding: binary\r\n"
             "\r\n"
             "foobar_log\r\n",
       missing_file_ == kTextFile
           ? ""
           : "--boundary\r\n"
             "Content-Disposition: form-data; name=\"footext\"\r\n"
             "\r\n"
             "upload_text_contents\r\n",
       "--boundary\r\n"
       "Content-Disposition: form-data; name=\"foovar\"\r\n"
       "\r\n"
       "bar\r\n"
       "--boundary\r\n"
       "Content-Disposition: form-data; name=\"boot_mode\"\r\n"
       "\r\n"
       "missing-crossystem\r\n"
       "--boundary\r\n"
       "Content-Disposition: form-data; name=\"error_type\"\r\n"
       "\r\n"
       "fake_error\r\n"
       "--boundary\r\n"
       "Content-Disposition: form-data; name=\"guid\"\r\n"
       "\r\n"
       "00112233445566778899aabbccddeeff\r\n"
       "--boundary--\r\n"});

  EXPECT_EQ(expected_data, std::string(data.begin(), data.end()));
}

INSTANTIATE_TEST_SUITE_P(
    CreateCrashFormDataInstantiation,
    CreateCrashFormDataTest,
    testing::Combine(
        testing::Bool(),
        testing::Values(kNone, kPayloadFile, kLogFile, kTextFile, kBinFile)));

TEST_F(CrashSenderUtilTest, SendCrashes) {
  // Set up the mock session manager client.
  auto mock =
      std::make_unique<org::chromium::SessionManagerInterfaceProxyMock>();
  test_util::SetActiveSessions(mock.get(), {{"user", "hash"}});
  std::vector<MetaFile> crashes_to_send;

  // Establish the client ID.
  ASSERT_TRUE(CreateClientIdFile());

  // Create the system crash directory, and crash files in it.
  const base::FilePath system_dir = paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(system_dir));
  const base::FilePath system_meta_file = system_dir.Append("0.0.0.0.meta");
  const base::FilePath system_log = system_dir.Append("0.0.0.0.log");
  const base::FilePath system_processing =
      system_dir.Append("0.0.0.0.processing");
  const char system_meta[] =
      "payload=0.0.0.0.log\n"
      "exec_name=exec_foo\n"
      "fake_report_id=123\n"
      "upload_var_prod=foo\n"
      "done=1\n"
      "upload_var_reportTimeMillis=1000000\n";
  ASSERT_TRUE(test_util::CreateFile(system_meta_file, system_meta));
  ASSERT_TRUE(test_util::CreateFile(system_log, ""));
  CrashInfo system_info;
  EXPECT_TRUE(system_info.metadata.LoadFromString(system_meta));
  system_info.payload_file = system_log;
  system_info.payload_kind = "log";
  EXPECT_TRUE(base::Time::FromString("25 Apr 2018 1:23:44 GMT",
                                     &system_info.last_modified));
  crashes_to_send.emplace_back(system_meta_file, std::move(system_info));

  // Create a user crash directory, and crash files in it.
  const base::FilePath user_dir = paths::Get("/home/user/hash/crash");
  ASSERT_TRUE(base::CreateDirectory(user_dir));
  const base::FilePath user_meta_file = user_dir.Append("0.0.0.0.meta");
  const base::FilePath user_log = user_dir.Append("0.0.0.0.log");
  const base::FilePath user_processing = user_dir.Append("0.0.0.0.processing");
  const char user_meta[] =
      "payload=0.0.0.0.log\n"
      "exec_name=exec_bar\n"
      "fake_report_id=456\n"
      "upload_var_prod=bar\n"
      "done=1\n"
      "upload_var_reportTimeMillis=2000000\n";
  ASSERT_TRUE(test_util::CreateFile(user_meta_file, user_meta));
  ASSERT_TRUE(test_util::CreateFile(user_log, ""));
  CrashInfo user_info;
  EXPECT_TRUE(user_info.metadata.LoadFromString(user_meta));
  user_info.payload_file = user_log;
  user_info.payload_kind = "log";
  EXPECT_TRUE(base::Time::FromString("25 Apr 2018 1:24:01 GMT",
                                     &user_info.last_modified));
  crashes_to_send.emplace_back(user_meta_file, std::move(user_info));

  // Create another user crash in "user". This will be skipped since the max
  // crash rate will be set to 2.
  const base::FilePath user_meta_file2 = user_dir.Append("1.1.1.1.meta");
  const base::FilePath user_log2 = user_dir.Append("1.1.1.1.log");
  const char user_meta2[] =
      "payload=1.1.1.1.log\n"
      "exec_name=exec_baz\n"
      "fake_report_id=789\n"
      "upload_var_prod=baz\n"
      "done=1\n";
  ASSERT_TRUE(test_util::CreateFile(user_meta_file2, user_meta2));
  ASSERT_TRUE(test_util::CreateFile(user_log2, ""));
  CrashInfo user_info2;
  EXPECT_TRUE(user_info2.metadata.LoadFromString(user_meta2));
  user_info2.payload_file = user_log2;
  user_info2.payload_kind = "log";
  EXPECT_TRUE(base::Time::FromString("25 Apr 2018 1:24:05 GMT",
                                     &user_info2.last_modified));
  crashes_to_send.emplace_back(user_meta_file2, std::move(user_info2));

  // Set up the conditions to emulate a device in guest mode; metrics are
  // disabled in guest mode.
  ASSERT_TRUE(SetConditions(kOfficialBuild, kGuestMode, kMetricsDisabled));
  // Keep the raw pointer, that's needed to exit from guest mode later.
  MetricsLibraryMock* raw_metrics_lib = metrics_lib_.get();

  // Set up the crash sender so that it succeeds.
  ASSERT_TRUE(SetMockCrashSending(true));

  // Set up the sender.
  std::vector<base::TimeDelta> sleep_times;
  Sender::Options options;
  options.session_manager_proxy = mock.release();
  options.max_crash_rate = 2;
  // Setting max_crash_bytes to 0 will limit to the uploader to max_crash_rate.
  options.max_crash_bytes = 0;
  options.sleep_function = base::Bind(&FakeSleep, &sleep_times);
  options.always_write_uploads_log = true;
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);

  // Send crashes.
  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kFinishedUploading, Sender::kSendReasonCount))
      .Times(0);
  base::TimeDelta total_sleep_time;
  sender.SendCrashes(crashes_to_send, &total_sleep_time);
  testing::Mock::VerifyAndClearExpectations(raw_metrics_lib);

  // We shouldn't be processing any crashes still.
  EXPECT_FALSE(base::PathExists(system_processing));
  EXPECT_FALSE(base::PathExists(user_processing));

  // The Chrome uploads.log file shouldn't exist because we had nothing to
  // upload, but we will have slept once until we determined we shouldn't be
  // doing uploads.
  EXPECT_FALSE(base::PathExists(paths::Get(paths::kChromeCrashLog)));
  EXPECT_EQ(1, sleep_times.size());
  EXPECT_EQ(total_sleep_time,
            std::accumulate(sleep_times.begin(), sleep_times.end(),
                            base::TimeDelta()));
  sleep_times.clear();

  // Exit from guest mode/re-enable metrics, and send crashes again.
  raw_metrics_lib->set_guest_mode(false);
  raw_metrics_lib->set_metrics_enabled(true);
  // 2 times because there are 2 valid crashes to send
  EXPECT_CALL(
      *raw_metrics_lib,
      SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                    Sender::kFinishedUploading, Sender::kSendReasonCount))
      .Times(2);
  EXPECT_CALL(*raw_metrics_lib,
              SendEnumToUMA("Platform.CrOS.CrashSenderRemoveReason",
                            Sender::kTotalRemoval, Sender::kSendReasonCount))
      .Times(2);
  sender.SendCrashes(crashes_to_send, &total_sleep_time);

  // We shouldn't be processing any crashes still.
  EXPECT_FALSE(base::PathExists(system_processing));
  EXPECT_FALSE(base::PathExists(user_processing));

  // Check the upload log from crash_sender.
  std::string contents;
  ASSERT_TRUE(
      base::ReadFileToString(paths::Get(paths::kChromeCrashLog), &contents));
  std::vector<base::Optional<base::Value>> rows =
      ParseChromeUploadsLog(contents);
  // Should only contain two results, since max_crash_rate is set to 2.
  // FakeSleep should be called three times since we sleep before we check the
  // crash rate.
  ASSERT_EQ(2, rows.size());
  EXPECT_EQ(3, sleep_times.size());
  EXPECT_EQ(total_sleep_time,
            std::accumulate(sleep_times.begin(), sleep_times.end(),
                            base::TimeDelta()));

  // Each line of the uploads.log file is "{"upload_time":<value>,"upload_id":
  // <value>,"local_id":<value>,"capture_time":<value>,"state":<value>,"source":
  // <value>}".
  // The first run should be for the meta file in the system directory.
  base::Optional<base::Value> row = std::move(rows[0]);
  ASSERT_TRUE(row.has_value());
  ASSERT_EQ(6, row->DictSize());
  EXPECT_TRUE(row->FindKey("upload_time"));
  EXPECT_EQ("123", row->FindKey("upload_id")->GetString());
  EXPECT_EQ("foo", row->FindKey("local_id")->GetString());
  EXPECT_EQ("1000", row->FindKey("capture_time")->GetString());
  EXPECT_EQ(3, row->FindKey("state")->GetInt());
  EXPECT_EQ("exec_foo", row->FindKey("source")->GetString());

  // The second run should be for the meta file in the "user" directory.
  row = std::move(rows[1]);
  ASSERT_TRUE(row.has_value());
  ASSERT_EQ(6, row->DictSize());
  EXPECT_TRUE(row->FindKey("upload_time"));
  EXPECT_EQ("456", row->FindKey("upload_id")->GetString());
  EXPECT_EQ("bar", row->FindKey("local_id")->GetString());
  EXPECT_EQ("2000", row->FindKey("capture_time")->GetString());
  EXPECT_EQ(3, row->FindKey("state")->GetInt());
  EXPECT_EQ("REDACTED", row->FindKey("source")->GetString());

  // The uploaded crash files should be removed now.
  EXPECT_FALSE(base::PathExists(system_meta_file));
  EXPECT_FALSE(base::PathExists(system_log));
  EXPECT_FALSE(base::PathExists(user_meta_file));
  EXPECT_FALSE(base::PathExists(user_log));

  // The following should be kept since the crash report was not uploaded.
  EXPECT_TRUE(base::PathExists(user_meta_file2));
  EXPECT_TRUE(base::PathExists(user_log2));
}

TEST_F(CrashSenderUtilTest, SendCrashes_Fail) {
  // Set up the mock session manager client.
  auto mock =
      std::make_unique<org::chromium::SessionManagerInterfaceProxyMock>();
  test_util::SetActiveSessions(mock.get(), {{"user", "hash"}});
  std::vector<MetaFile> crashes_to_send;

  // Create the system crash directory, and crash files in it.
  const base::FilePath system_dir = paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(system_dir));
  const base::FilePath system_meta_file = system_dir.Append("0.0.0.0.meta");
  const base::FilePath system_log = system_dir.Append("0.0.0.0.log");
  const base::FilePath system_processing =
      system_dir.Append("0.0.0.0.processing");
  const char system_meta[] =
      "payload=0.0.0.0.log\n"
      "done=1\n";
  ASSERT_TRUE(test_util::CreateFile(system_meta_file, system_meta));
  ASSERT_TRUE(test_util::CreateFile(system_log, ""));
  CrashInfo system_info;
  EXPECT_TRUE(system_info.metadata.LoadFromString(system_meta));
  system_info.payload_file = system_log;
  system_info.payload_kind = "log";
  EXPECT_TRUE(base::Time::FromString("25 Apr 2018 1:23:44 GMT",
                                     &system_info.last_modified));
  crashes_to_send.emplace_back(system_meta_file, std::move(system_info));

  ASSERT_TRUE(SetConditions(kOfficialBuild, kSignInMode, kMetricsEnabled));

  // Set up the crash sender so that it fails.
  ASSERT_TRUE(SetMockCrashSending(false));

  // Set up the sender.
  std::vector<base::TimeDelta> sleep_times;
  Sender::Options options;
  options.session_manager_proxy = mock.release();
  options.max_crash_rate = 2;
  options.sleep_function = base::Bind(&FakeSleep, &sleep_times);
  options.always_write_uploads_log = true;
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);

  sender.SendCrashes(crashes_to_send, nullptr);

  // We shouldn't be processing the crash still -- sending failed, but didn't
  // crash.
  EXPECT_FALSE(base::PathExists(system_processing));

  // The followings should be kept since the crash report was not uploaded.
  EXPECT_TRUE(base::PathExists(system_meta_file));
  EXPECT_TRUE(base::PathExists(system_log));

  // The Chrome uploads.log file shouldn't exist because we had nothing to
  // report.
  EXPECT_FALSE(base::PathExists(paths::Get(paths::kChromeCrashLog)));
}

// Verify behavior when SendCrashes itself crashes.
TEST_F(CrashSenderUtilDeathTest, SendCrashes_Crash) {
  // Set up the mock session manager client.
  auto mock =
      std::make_unique<org::chromium::SessionManagerInterfaceProxyMock>();
  test_util::SetActiveSessions(mock.get(), {{"user", "hash"}});
  std::vector<MetaFile> crashes_to_send;

  // Create the system crash directory, and crash files in it.
  const base::FilePath system_dir = paths::Get(paths::kSystemCrashDirectory);
  ASSERT_TRUE(base::CreateDirectory(system_dir));
  const base::FilePath system_meta_file = system_dir.Append("0.0.0.0.meta");
  const base::FilePath system_log = system_dir.Append("0.0.0.0.log");
  const base::FilePath system_processing =
      system_dir.Append("0.0.0.0.processing");
  const char system_meta[] =
      "payload=0.0.0.0.log\n"
      "done=1\n";
  ASSERT_TRUE(test_util::CreateFile(system_meta_file, system_meta));
  ASSERT_TRUE(test_util::CreateFile(system_log, ""));
  CrashInfo system_info;
  EXPECT_TRUE(system_info.metadata.LoadFromString(system_meta));
  system_info.payload_file = system_log;
  system_info.payload_kind = "log";
  EXPECT_TRUE(base::Time::FromString("25 Apr 2018 1:23:44 GMT",
                                     &system_info.last_modified));
  crashes_to_send.emplace_back(system_meta_file, std::move(system_info));

  ASSERT_TRUE(SetConditions(kOfficialBuild, kSignInMode, kMetricsEnabled));

  std::vector<base::TimeDelta> sleep_times;
  Sender::Options options;
  options.session_manager_proxy = mock.release();
  options.max_crash_rate = 2;
  options.sleep_function = base::Bind(&FakeSleep, &sleep_times);
  options.always_write_uploads_log = true;
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);

  ASSERT_TRUE(SetMockCrashSending(true));
  sender.SetCrashDuringSendForTesting(true);
  EXPECT_DEATH(sender.SendCrashes(crashes_to_send, nullptr),
               "crashing as requested");

  // We crashed, so the ".processing" file should still exist.
  EXPECT_TRUE(base::PathExists(system_processing));
}

TEST_F(CrashSenderUtilTest, LockFile) {
  auto clock = std::make_unique<MockClock>();
  base::Time start_time;
  ASSERT_TRUE(base::Time::FromUTCString("2019-04-20 13:53", &start_time));
  // Called twice -- once to get start time, once to see if we're already
  // past the start time + 5 minutes
  EXPECT_CALL(*clock, Now()).Times(2).WillRepeatedly(Return(start_time));
  std::vector<base::TimeDelta> sleep_times;
  Sender::Options options;
  options.sleep_function = base::Bind(&FakeSleep, &sleep_times);
  Sender sender(std::move(metrics_lib_), std::move(clock), options);

  EXPECT_FALSE(IsFileLocked(paths::Get(paths::kCrashSenderLockFile)));
  base::File lock(sender.AcquireLockFileOrDie());
  EXPECT_TRUE(IsFileLocked(paths::Get(paths::kCrashSenderLockFile)));
  // Should not have slept acquiring lock since the file was unlocked.
  EXPECT_THAT(sleep_times, IsEmpty());
}

TEST_F(CrashSenderUtilTest, LockFileTriesAgainIfFirstAttemptFails) {
  base::FilePath lock_file_path = paths::Get(paths::kCrashSenderLockFile);
  auto lock_process = LockFile(lock_file_path);

  auto clock = std::make_unique<MockClock>();
  base::Time start_time;
  ASSERT_TRUE(base::Time::FromUTCString("2019-04-20 13:53", &start_time));

  // Make AcquireLockFileOrDie sleep several times, and then unlock the file.
  EXPECT_CALL(*clock, Now())
      .WillOnce(Return(start_time))
      .WillOnce(Return(start_time))
      .WillOnce(Return(start_time + base::TimeDelta::FromMinutes(1)))
      .WillOnce(Return(start_time + base::TimeDelta::FromMinutes(2)))
      .WillOnce(Return(start_time + base::TimeDelta::FromMinutes(3)))
      .WillOnce(Invoke([&lock_process, start_time]() {
        lock_process->Kill(SIGKILL, 10);
        lock_process->Wait();
        return start_time + base::TimeDelta::FromMinutes(4);
      }));
  std::vector<base::TimeDelta> sleep_times;
  Sender::Options options;
  options.sleep_function = base::Bind(&FakeSleep, &sleep_times);
  Sender sender(std::move(metrics_lib_), std::move(clock), options);

  base::File lock(sender.AcquireLockFileOrDie());
  EXPECT_TRUE(IsFileLocked(lock_file_path));
  EXPECT_EQ(sleep_times.size(), 4);
}

TEST_F(CrashSenderUtilTest, LockFileTriesOneLastTimeAfterTimeout) {
  base::FilePath lock_file_path = paths::Get(paths::kCrashSenderLockFile);
  auto lock_process = LockFile(lock_file_path);

  auto clock = std::make_unique<MockClock>();
  base::Time start_time;
  ASSERT_TRUE(base::Time::FromUTCString("2019-04-20 13:53", &start_time));

  // Make AcquireLockFileOrDie sleep enough that the loop exits, then unlock the
  // file.
  EXPECT_CALL(*clock, Now())
      .WillOnce(Return(start_time))
      .WillOnce(Return(start_time))
      .WillOnce(Return(start_time + base::TimeDelta::FromMinutes(1)))
      .WillOnce(Return(start_time + base::TimeDelta::FromMinutes(2)))
      .WillOnce(Return(start_time + base::TimeDelta::FromMinutes(3)))
      .WillOnce(Return(start_time + base::TimeDelta::FromMinutes(4)))
      .WillOnce(Invoke([&lock_process, start_time]() {
        lock_process->Kill(SIGKILL, 10);
        lock_process->Wait();
        return start_time + base::TimeDelta::FromMinutes(6);
      }));
  std::vector<base::TimeDelta> sleep_times;
  Sender::Options options;
  options.sleep_function = base::Bind(&FakeSleep, &sleep_times);
  Sender sender(std::move(metrics_lib_), std::move(clock), options);

  base::File lock(sender.AcquireLockFileOrDie());
  EXPECT_TRUE(IsFileLocked(lock_file_path));
  EXPECT_EQ(sleep_times.size(), 5);
}

TEST_F(CrashSenderUtilDeathTest, LockFileDiesIfFileIsLocked) {
  base::FilePath lock_file_path = paths::Get(paths::kCrashSenderLockFile);
  auto lock_process = LockFile(lock_file_path);
  std::vector<base::TimeDelta> sleep_times;
  Sender::Options options;
  options.sleep_function = base::Bind(&FakeSleep, &sleep_times);
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);
  EXPECT_EXIT(sender.AcquireLockFileOrDie(), ExitedWithCode(EXIT_FAILURE),
              "Failed to acquire a lock");
}

TEST_F(CrashSenderUtilTest, CreateClientId) {
  std::string client_id = GetClientId();
  EXPECT_EQ(client_id.length(), 32);
  // Make sure it returns the same one multiple times.
  EXPECT_EQ(client_id, GetClientId());
}

TEST_F(CrashSenderUtilTest, RetrieveClientId) {
  CreateClientIdFile();
  EXPECT_EQ(kFakeClientId, GetClientId());
}

class IsNetworkOnlineTest : public CrashSenderUtilTest {
 public:
  void TestIsNetworkOnline(std::string connection_state,
                           bool get_properties_retval,
                           bool expected_result);
};

void IsNetworkOnlineTest::TestIsNetworkOnline(std::string connection_state,
                                              bool get_properties_retval,
                                              bool expected_result) {
  g_connection_state = &connection_state;
  // Set up the shill flimflam manager client.
  auto mock = std::make_unique<org::chromium::flimflam::ManagerProxyMock>();
  EXPECT_CALL(*mock, GetProperties(_, _, _))
      .WillOnce(
          DoAll(Invoke(&GetShillProperties), Return(get_properties_retval)));

  Sender::Options options;
  options.shill_proxy = mock.release();
  Sender sender(std::move(metrics_lib_),
                std::make_unique<test_util::AdvancingClock>(), options);
  EXPECT_EQ(sender.IsNetworkOnline(), expected_result);
}

TEST_F(IsNetworkOnlineTest, Online) {
  TestIsNetworkOnline("online", true, true);
}

TEST_F(IsNetworkOnlineTest, Offline) {
  TestIsNetworkOnline("offline", true, false);
}

TEST_F(IsNetworkOnlineTest, Fail) {
  TestIsNetworkOnline("", false, true);
}

}  // namespace util
