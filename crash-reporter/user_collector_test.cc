// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/user_collector.h"

#include <bits/wordsize.h>
#include <elf.h>
#include <unistd.h>

#include <vector>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_split.h>
#include <brillo/syslog_logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "crash-reporter/test_util.h"
#include "crash-reporter/vm_support.h"

using base::FilePath;
using brillo::FindLog;
using ::testing::_;
using ::testing::AllOf;
using ::testing::EndsWith;
using ::testing::Property;
using ::testing::Return;
using ::testing::StartsWith;

namespace {

bool s_metrics = false;

const char kFilePath[] = "/my/path";

// Keep in sync with UserCollector::ShouldDump.
const char kChromeIgnoreMsg[] =
    "ignoring call by kernel - chrome crash; "
    "waiting for chrome to call us directly";

bool IsMetrics() {
  return s_metrics;
}

}  // namespace

class UserCollectorMock : public UserCollector {
 public:
  MOCK_METHOD(void, SetUpDBus, (), (override));
  MOCK_METHOD(std::vector<std::string>,
              GetCommandLine,
              (pid_t),
              (const, override));
  MOCK_METHOD(void, AccounceUserCrash, (), (override));
  MOCK_METHOD(ErrorType,
              ConvertCoreToMinidump,
              (pid_t pid,
               const base::FilePath&,
               const base::FilePath&,
               const base::FilePath&),
              (override));
};

class UserCollectorTest : public ::testing::Test {
  void SetUp() {
    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(testing::Return());

    const std::vector<std::string> default_command_line = {"test_command",
                                                           "--test-arg"};
    EXPECT_CALL(collector_, GetCommandLine(testing::_))
        .WillRepeatedly(testing::Return(default_command_line));

    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    test_dir_ = scoped_temp_dir_.GetPath();

    const pid_t pid = getpid();
    collector_.Initialize(kFilePath, IsMetrics, false, false, false);
    // Setup paths for output files.
    test_core_pattern_file_ = test_dir_.Append("core_pattern");
    collector_.set_core_pattern_file(test_core_pattern_file_.value());
    test_core_pipe_limit_file_ = test_dir_.Append("core_pipe_limit");
    collector_.set_core_pipe_limit_file(test_core_pipe_limit_file_.value());
    collector_.set_filter_path(test_dir_.Append("no_filter").value());
    crash_dir_ = test_dir_.Append("crash_dir");
    ASSERT_TRUE(base::CreateDirectory(crash_dir_));
    collector_.set_crash_directory_for_test(crash_dir_);
    pid_ = pid;

    brillo::ClearLog();
  }

 protected:
  void ExpectFileEquals(const char* golden, const FilePath& file_path) {
    std::string contents;
    EXPECT_TRUE(base::ReadFileToString(file_path, &contents));
    EXPECT_EQ(golden, contents);
  }

  std::vector<std::string> SplitLines(const std::string& lines) const {
    return base::SplitString(lines, "\n", base::KEEP_WHITESPACE,
                             base::SPLIT_WANT_ALL);
  }

  // Verify that the root directory is not writable. Several tests depend on
  // this fact, and are failing in ways that might be explained by having a
  // writable root directory.
  // Not using base::PathIsWritable because that doesn't actually check if the
  // user can write to a path :-/ See 'man 2 access'.
  static bool IsRootDirectoryWritable() {
    base::FilePath temp_file_path;
    if (!CreateTemporaryFileInDir(base::FilePath("/"), &temp_file_path)) {
      return false;
    }
    base::DeleteFile(temp_file_path, false);
    return true;
  }

  UserCollectorMock collector_;
  pid_t pid_;
  FilePath test_dir_;
  FilePath crash_dir_;
  FilePath test_core_pattern_file_;
  FilePath test_core_pipe_limit_file_;
  base::ScopedTempDir scoped_temp_dir_;
};

TEST_F(UserCollectorTest, EnableOK) {
  ASSERT_TRUE(collector_.Enable(false));
  ExpectFileEquals("|/my/path --user=%P:%s:%u:%g:%f", test_core_pattern_file_);
  ExpectFileEquals("4", test_core_pipe_limit_file_);
  EXPECT_TRUE(FindLog("Enabling user crash handling"));
}

TEST_F(UserCollectorTest, EnableNoPatternFileAccess) {
  // Sanity checking:
  // Confirm we don't have junk left over from other tests.
  ASSERT_FALSE(base::PathExists(base::FilePath("/does_not_exist")));
  // We've seen strange problems that might be explained by having / writable.
  ASSERT_FALSE(IsRootDirectoryWritable());

  collector_.set_core_pattern_file("/does_not_exist");
  ASSERT_FALSE(collector_.Enable(false));
  EXPECT_TRUE(FindLog("Enabling user crash handling"));
  EXPECT_TRUE(FindLog("Unable to write /does_not_exist"));
}

TEST_F(UserCollectorTest, EnableNoPipeLimitFileAccess) {
  // Sanity checking:
  // Confirm we don't have junk left over from other tests.
  ASSERT_FALSE(base::PathExists(base::FilePath("/does_not_exist")));
  // We've seen strange problems that might be explained by having / writable.
  ASSERT_FALSE(IsRootDirectoryWritable());

  collector_.set_core_pipe_limit_file("/does_not_exist");
  ASSERT_FALSE(collector_.Enable(false));
  // Core pattern should not be written if we cannot access the pipe limit
  // or otherwise we may set a pattern that results in infinite recursion.
  ASSERT_FALSE(base::PathExists(test_core_pattern_file_));
  EXPECT_TRUE(FindLog("Enabling user crash handling"));
  EXPECT_TRUE(FindLog("Unable to write /does_not_exist"));
}

TEST_F(UserCollectorTest, DisableOK) {
  ASSERT_TRUE(collector_.Disable());
  ExpectFileEquals("core", test_core_pattern_file_);
  EXPECT_TRUE(FindLog("Disabling user crash handling"));
}

TEST_F(UserCollectorTest, DisableNoFileAccess) {
  // Sanity checking:
  // Confirm we don't have junk left over from other tests.
  ASSERT_FALSE(base::PathExists(base::FilePath("/does_not_exist")));
  // We've seen strange problems that might be explained by having / writable.
  ASSERT_FALSE(IsRootDirectoryWritable());

  collector_.set_core_pattern_file("/does_not_exist");
  ASSERT_FALSE(collector_.Disable());
  EXPECT_TRUE(FindLog("Disabling user crash handling"));
  EXPECT_TRUE(FindLog("Unable to write /does_not_exist"));
}

TEST_F(UserCollectorTest, ParseCrashAttributes) {
  base::Optional<UserCollectorBase::CrashAttributes> attrs =
      UserCollectorBase::ParseCrashAttributes("123456:11:1000:2000:foobar");
  ASSERT_TRUE(attrs);
  EXPECT_EQ(123456, attrs->pid);
  EXPECT_EQ(11, attrs->signal);
  EXPECT_EQ(1000, attrs->uid);
  EXPECT_EQ(2000, attrs->gid);
  EXPECT_EQ("foobar", attrs->exec_name);

  attrs = UserCollectorBase::ParseCrashAttributes("4321:6:0:0:barfoo");
  ASSERT_TRUE(attrs);
  EXPECT_EQ(4321, attrs->pid);
  EXPECT_EQ(6, attrs->signal);
  EXPECT_EQ(0, attrs->uid);
  EXPECT_EQ(0, attrs->gid);
  EXPECT_EQ("barfoo", attrs->exec_name);

  EXPECT_FALSE(UserCollectorBase::ParseCrashAttributes("123456:11:1000"));
  EXPECT_FALSE(UserCollectorBase::ParseCrashAttributes("123456:11:1000:100"));

  attrs =
      UserCollectorBase::ParseCrashAttributes("123456:11:1000:100:exec:extra");
  ASSERT_TRUE(attrs);
  EXPECT_EQ("exec:extra", attrs->exec_name);

  EXPECT_FALSE(
      UserCollectorBase::ParseCrashAttributes("12345p:11:1000:100:foobar"));

  EXPECT_FALSE(
      UserCollectorBase::ParseCrashAttributes("123456:1 :1000:0:foobar"));

  EXPECT_FALSE(UserCollectorBase::ParseCrashAttributes("123456::::foobar"));
}

TEST_F(UserCollectorTest, ShouldDumpDeveloperImageOverridesConsent) {
  std::string reason;
  EXPECT_TRUE(
      collector_.ShouldDump(pid_, false, true, false, "chrome-wm", &reason));
  EXPECT_EQ("developer build - not testing - always dumping", reason);

  // When running a crash test, behave as normal.
  EXPECT_FALSE(
      collector_.ShouldDump(pid_, false, false, false, "chrome-wm", &reason));
  EXPECT_EQ("ignoring - no consent", reason);
}

TEST_F(UserCollectorTest, ShouldDumpChromeOverridesDeveloperImage) {
  std::string reason;
  // When running a crash test, behave as normal.
  EXPECT_FALSE(
      collector_.ShouldDump(pid_, false, false, false, "chrome", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "supplied_Compositor", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "supplied_PipelineThread", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "Chrome_ChildIOThread", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "supplied_Chrome_ChildIOT", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "supplied_ChromotingClien", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);
  EXPECT_FALSE(collector_.ShouldDump(pid_, false, false, false,
                                     "supplied_LocalInputMonit", &reason));
  EXPECT_EQ(kChromeIgnoreMsg, reason);

  // When running a developer image, test that chrome crashes are handled
  // when the "handle_chrome_crashes" flag is set.
  EXPECT_TRUE(
      collector_.ShouldDump(pid_, false, true, true, "chrome", &reason));
  EXPECT_EQ("developer build - not testing - always dumping", reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "supplied_Compositor", &reason));
  EXPECT_EQ("developer build - not testing - always dumping", reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "supplied_PipelineThread", &reason));
  EXPECT_EQ("developer build - not testing - always dumping", reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "Chrome_ChildIOThread", &reason));
  EXPECT_EQ("developer build - not testing - always dumping", reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "supplied_Chrome_ChildIOT", &reason));
  EXPECT_EQ("developer build - not testing - always dumping", reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "supplied_ChromotingClien", &reason));
  EXPECT_EQ("developer build - not testing - always dumping", reason);
  EXPECT_TRUE(collector_.ShouldDump(pid_, false, true, true,
                                    "supplied_LocalInputMonit", &reason));
  EXPECT_EQ("developer build - not testing - always dumping", reason);
}

TEST_F(UserCollectorTest, ShouldDumpUserConsentProductionImage) {
  std::string reason;
  EXPECT_FALSE(
      collector_.ShouldDump(pid_, false, false, false, "chrome-wm", &reason));
  EXPECT_EQ("ignoring - no consent", reason);

  EXPECT_TRUE(
      collector_.ShouldDump(pid_, true, false, false, "chrome-wm", &reason));
  EXPECT_EQ("handling", reason);
}

// HandleCrashWithoutConsent tests that we do not attempt to create a dmp file
// if we don't have user consent to collect crash data.
TEST_F(UserCollectorTest, HandleCrashWithoutConsent) {
  s_metrics = false;
  EXPECT_CALL(collector_, AccounceUserCrash()).Times(0);
  EXPECT_CALL(collector_, ConvertCoreToMinidump(_, _, _, _)).Times(0);

  UserCollectorBase::CrashAttributes attrs;
  attrs.pid = 20;
  attrs.signal = 10;
  attrs.uid = 1000;
  attrs.gid = 1000;
  attrs.exec_name = "ignored";
  EXPECT_TRUE(collector_.HandleCrash(attrs, "foobar"));
  if (!VmSupport::Get()) {
    EXPECT_TRUE(FindLog("Received crash notification for foobar[20] sig 10"));
  }
}

// HandleNonChromeCrashWithConsent tests that we will create a dmp file if we
// (a) have user consent to collect crash data and
// (b) the process is not a Chrome process.
TEST_F(UserCollectorTest, HandleNonChromeCrashWithConsent) {
  s_metrics = true;
  // Note the _ which is different from the - in the original |force_exec|
  // passed to HandleCrash. This is due to the CrashCollector::Sanitize call in
  // FormatDumpBasename.
  const std::string crash_prefix = crash_dir_.Append("chromeos_wm").value();
  int expected_mock_calls = 1;
  if (VmSupport::Get()) {
    expected_mock_calls = 0;
  }
  EXPECT_CALL(collector_, AccounceUserCrash()).Times(expected_mock_calls);
  // NOTE: The '5' which appears in several strings below is the pid of the
  // simulated crashing process.
  EXPECT_CALL(collector_,
              ConvertCoreToMinidump(
                  5, FilePath("/tmp/crash_reporter/5"),
                  Property(&FilePath::value,
                           AllOf(StartsWith(crash_prefix), EndsWith("core"))),
                  Property(&FilePath::value,
                           AllOf(StartsWith(crash_prefix), EndsWith("dmp")))))
      .Times(expected_mock_calls)
      .WillRepeatedly(Return(CrashCollector::kErrorNone));

  UserCollectorBase::CrashAttributes attrs;
  attrs.pid = 5;
  attrs.signal = 2;
  attrs.uid = 1000;
  attrs.gid = 1000;
  attrs.exec_name = "ignored";
  EXPECT_TRUE(collector_.HandleCrash(attrs, "chromeos-wm"));
  if (!VmSupport::Get()) {
    EXPECT_TRUE(
        FindLog("Received crash notification for chromeos-wm[5] sig 2"));
  }
}

// HandleChromeCrashWithConsent tests that we do not attempt to create a dmp
// file if the process is named chrome. This is because we expect Chrome's own
// crash handling library (Breakpad or Crashpad) to call us directly -- see
// chrome_collector.h.
TEST_F(UserCollectorTest, HandleChromeCrashWithConsent) {
  s_metrics = true;
  EXPECT_CALL(collector_, AccounceUserCrash()).Times(0);
  EXPECT_CALL(collector_, ConvertCoreToMinidump(_, _, _, _)).Times(0);

  UserCollectorBase::CrashAttributes attrs;
  attrs.pid = 5;
  attrs.signal = 2;
  attrs.uid = 1000;
  attrs.gid = 1000;
  attrs.exec_name = "ignored";
  EXPECT_TRUE(collector_.HandleCrash(attrs, "chrome"));
  if (!VmSupport::Get()) {
    EXPECT_TRUE(FindLog("Received crash notification for chrome[5] sig 2"));
    EXPECT_TRUE(FindLog(kChromeIgnoreMsg));
  }
}

// HandleSuppliedChromeCrashWithConsent also tests that we do not attempt to
// create a dmp file if the process is named chrome. This differs only in the
// fact that we are using the kernel's supplied name instead of the |force_exec|
// name. This is actually much closer to the real usage.
TEST_F(UserCollectorTest, HandleSuppliedChromeCrashWithConsent) {
  s_metrics = true;
  EXPECT_CALL(collector_, AccounceUserCrash()).Times(0);
  EXPECT_CALL(collector_, ConvertCoreToMinidump(_, _, _, _)).Times(0);

  UserCollectorBase::CrashAttributes attrs;
  attrs.pid = 5;
  attrs.signal = 2;
  attrs.uid = 1000;
  attrs.gid = 1000;
  attrs.exec_name = "chrome";
  EXPECT_TRUE(collector_.HandleCrash(attrs, nullptr));
  if (!VmSupport::Get()) {
    EXPECT_TRUE(
        FindLog("Received crash notification for supplied_chrome[5] sig 2"));
    EXPECT_TRUE(FindLog(kChromeIgnoreMsg));
  }
}

TEST_F(UserCollectorTest, GetProcessPath) {
  FilePath path = collector_.GetProcessPath(100);
  ASSERT_EQ("/proc/100", path.value());
}

TEST_F(UserCollectorTest, GetExecutableBaseNameFromPid) {
  std::string base_name;
  EXPECT_FALSE(collector_.GetExecutableBaseNameFromPid(0, &base_name));
  EXPECT_TRUE(
      FindLog("ReadSymbolicLink failed - Path /proc/0 DirectoryExists: 0"));
  EXPECT_TRUE(FindLog("stat /proc/0/exe failed: -1 2"));

  brillo::ClearLog();
  pid_t my_pid = getpid();
  EXPECT_TRUE(collector_.GetExecutableBaseNameFromPid(my_pid, &base_name));
  EXPECT_FALSE(FindLog("Readlink failed"));
  EXPECT_EQ("crash_reporter_test", base_name);
}

TEST_F(UserCollectorTest, GetFirstLineWithPrefix) {
  std::vector<std::string> lines;
  std::string line;

  EXPECT_FALSE(collector_.GetFirstLineWithPrefix(lines, "Name:", &line));
  EXPECT_EQ("", line);

  lines.push_back("Name:\tls");
  lines.push_back("State:\tR (running)");
  lines.push_back(" Foo:\t1000");

  line.clear();
  EXPECT_TRUE(collector_.GetFirstLineWithPrefix(lines, "Name:", &line));
  EXPECT_EQ(lines[0], line);

  line.clear();
  EXPECT_TRUE(collector_.GetFirstLineWithPrefix(lines, "State:", &line));
  EXPECT_EQ(lines[1], line);

  line.clear();
  EXPECT_FALSE(collector_.GetFirstLineWithPrefix(lines, "Foo:", &line));
  EXPECT_EQ("", line);

  line.clear();
  EXPECT_TRUE(collector_.GetFirstLineWithPrefix(lines, " Foo:", &line));
  EXPECT_EQ(lines[2], line);

  line.clear();
  EXPECT_FALSE(collector_.GetFirstLineWithPrefix(lines, "Bar:", &line));
  EXPECT_EQ("", line);
}

TEST_F(UserCollectorTest, GetIdFromStatus) {
  int id = 1;
  EXPECT_FALSE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                          UserCollector::kIdEffective,
                                          SplitLines("nothing here"), &id));
  EXPECT_EQ(id, 1);

  // Not enough parameters.
  EXPECT_FALSE(
      collector_.GetIdFromStatus(UserCollector::kUserId, UserCollector::kIdReal,
                                 SplitLines("line 1\nUid:\t1\n"), &id));

  const std::vector<std::string> valid_contents =
      SplitLines("\nUid:\t1\t2\t3\t4\nGid:\t5\t6\t7\t8\n");
  EXPECT_TRUE(collector_.GetIdFromStatus(
      UserCollector::kUserId, UserCollector::kIdReal, valid_contents, &id));
  EXPECT_EQ(1, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                         UserCollector::kIdEffective,
                                         valid_contents, &id));
  EXPECT_EQ(2, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kUserId,
                                         UserCollector::kIdFileSystem,
                                         valid_contents, &id));
  EXPECT_EQ(4, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(UserCollector::kGroupId,
                                         UserCollector::kIdEffective,
                                         valid_contents, &id));
  EXPECT_EQ(6, id);

  EXPECT_TRUE(collector_.GetIdFromStatus(
      UserCollector::kGroupId, UserCollector::kIdSet, valid_contents, &id));
  EXPECT_EQ(7, id);

  EXPECT_FALSE(collector_.GetIdFromStatus(
      UserCollector::kGroupId, UserCollector::IdKind(5), valid_contents, &id));
  EXPECT_FALSE(collector_.GetIdFromStatus(
      UserCollector::kGroupId, UserCollector::IdKind(-1), valid_contents, &id));

  // Fail if junk after number
  EXPECT_FALSE(
      collector_.GetIdFromStatus(UserCollector::kUserId, UserCollector::kIdReal,
                                 SplitLines("Uid:\t1f\t2\t3\t4\n"), &id));
  EXPECT_TRUE(
      collector_.GetIdFromStatus(UserCollector::kUserId, UserCollector::kIdReal,
                                 SplitLines("Uid:\t1\t2\t3\t4\n"), &id));
  EXPECT_EQ(1, id);

  // Fail if more than 4 numbers.
  EXPECT_FALSE(
      collector_.GetIdFromStatus(UserCollector::kUserId, UserCollector::kIdReal,
                                 SplitLines("Uid:\t1\t2\t3\t4\t5\n"), &id));
}

TEST_F(UserCollectorTest, GetStateFromStatus) {
  std::string state;
  EXPECT_FALSE(
      collector_.GetStateFromStatus(SplitLines("nothing here"), &state));
  EXPECT_EQ("", state);

  EXPECT_TRUE(
      collector_.GetStateFromStatus(SplitLines("State:\tR (running)"), &state));
  EXPECT_EQ("R (running)", state);

  EXPECT_TRUE(collector_.GetStateFromStatus(
      SplitLines("Name:\tls\nState:\tZ (zombie)\n"), &state));
  EXPECT_EQ("Z (zombie)", state);
}

TEST_F(UserCollectorTest, ClobberContainerDirectory) {
  // Try a path that is not writable.
  ASSERT_FALSE(collector_.ClobberContainerDirectory(FilePath("/bad/path")));
  EXPECT_TRUE(FindLog("Could not create /bad/path"));
}

TEST_F(UserCollectorTest, CopyOffProcFilesBadPid) {
  FilePath container_path = test_dir_.Append("container");
  ASSERT_TRUE(collector_.ClobberContainerDirectory(container_path));

  ASSERT_FALSE(collector_.CopyOffProcFiles(0, container_path));
  EXPECT_TRUE(FindLog("Path /proc/0 does not exist"));
}

TEST_F(UserCollectorTest, CopyOffProcFilesOK) {
  FilePath container_path = test_dir_.Append("container");
  ASSERT_TRUE(collector_.ClobberContainerDirectory(container_path));

  ASSERT_TRUE(collector_.CopyOffProcFiles(pid_, container_path));
  EXPECT_FALSE(FindLog("Could not copy"));
  static const struct {
    const char* name;
    bool exists;
  } kExpectations[] = {
      {"auxv", true}, {"cmdline", true}, {"environ", true}, {"maps", true},
      {"mem", false}, {"mounts", false}, {"sched", false},  {"status", true},
  };
  for (const auto& expectation : kExpectations) {
    EXPECT_EQ(expectation.exists,
              base::PathExists(container_path.Append(expectation.name)));
  }
}

TEST_F(UserCollectorTest, ValidateProcFiles) {
  FilePath container_dir = test_dir_;

  // maps file not exists (i.e. GetFileSize fails)
  EXPECT_FALSE(collector_.ValidateProcFiles(container_dir));

  // maps file is empty
  FilePath maps_file = container_dir.Append("maps");
  ASSERT_TRUE(test_util::CreateFile(maps_file, ""));
  ASSERT_TRUE(base::PathExists(maps_file));
  EXPECT_FALSE(collector_.ValidateProcFiles(container_dir));

  // maps file is not empty
  const char data[] = "test data";
  ASSERT_TRUE(test_util::CreateFile(maps_file, data));
  ASSERT_TRUE(base::PathExists(maps_file));
  EXPECT_TRUE(collector_.ValidateProcFiles(container_dir));
}

TEST_F(UserCollectorTest, ValidateCoreFile) {
  FilePath core_file = test_dir_.Append("core");

  // Core file does not exist
  EXPECT_EQ(UserCollector::kErrorReadCoreData,
            collector_.ValidateCoreFile(core_file));
  char e_ident[EI_NIDENT];
  e_ident[EI_MAG0] = ELFMAG0;
  e_ident[EI_MAG1] = ELFMAG1;
  e_ident[EI_MAG2] = ELFMAG2;
  e_ident[EI_MAG3] = ELFMAG3;
#if __WORDSIZE == 32
  e_ident[EI_CLASS] = ELFCLASS32;
#elif __WORDSIZE == 64
  e_ident[EI_CLASS] = ELFCLASS64;
#else
#error Unknown/unsupported value of __WORDSIZE.
#endif

  // Core file has the expected header
  ASSERT_TRUE(
      test_util::CreateFile(core_file, std::string(e_ident, sizeof(e_ident))));
  EXPECT_EQ(UserCollector::kErrorNone, collector_.ValidateCoreFile(core_file));

#if __WORDSIZE == 64
  // 32-bit core file on 64-bit platform
  e_ident[EI_CLASS] = ELFCLASS32;
  ASSERT_TRUE(
      test_util::CreateFile(core_file, std::string(e_ident, sizeof(e_ident))));
  EXPECT_EQ(UserCollector::kErrorUnsupported32BitCoreFile,
            collector_.ValidateCoreFile(core_file));
  e_ident[EI_CLASS] = ELFCLASS64;
#endif

  // Invalid core files
  ASSERT_TRUE(test_util::CreateFile(core_file,
                                    std::string(e_ident, sizeof(e_ident) - 1)));
  EXPECT_EQ(UserCollector::kErrorInvalidCoreFile,
            collector_.ValidateCoreFile(core_file));

  e_ident[EI_MAG0] = 0;
  ASSERT_TRUE(
      test_util::CreateFile(core_file, std::string(e_ident, sizeof(e_ident))));
  EXPECT_EQ(UserCollector::kErrorInvalidCoreFile,
            collector_.ValidateCoreFile(core_file));
}
