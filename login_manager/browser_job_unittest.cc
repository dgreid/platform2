// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "login_manager/browser_job.h"

#include <stdint.h>
#include <unistd.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <base/command_line.h>
#include <base/logging.h>
#include <base/memory/scoped_ptr.h>
#include <base/strings/string_util.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "login_manager/mock_file_checker.h"
#include "login_manager/mock_metrics.h"
#include "login_manager/mock_system_utils.h"

namespace login_manager {

using ::testing::AnyNumber;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::_;

class BrowserJobTest : public ::testing::Test {
 public:
  BrowserJobTest() {}

  virtual ~BrowserJobTest() {}

  void SetUp() override;

 protected:
  static const char* kArgv[];
  static const char kUser[];
  static const char kHash[];

  void ExpectArgsToContainFlag(const std::vector<std::string>& argv,
                               const char name[],
                               const char value[]) {
    std::vector<std::string>::const_iterator user_flag = std::find(
        argv.begin(), argv.end(), base::StringPrintf("%s%s", name, value));
    EXPECT_NE(user_flag, argv.end()) << "argv should contain " << name << value;
  }

  void ExpectArgsNotToContainFlag(const std::vector<std::string>& argv,
                                  const char name[],
                                  const char value[]) {
    std::vector<std::string>::const_iterator user_flag = std::find(
        argv.begin(), argv.end(), base::StringPrintf("%s%s", name, value));
    EXPECT_EQ(user_flag, argv.end()) << "argv shouldn't contain " << name
                                     << value;
  }

  void ExpectArgsToContainAll(const std::vector<std::string>& argv,
                              const std::vector<std::string>& contained) {
    std::set<std::string> argv_set(argv.begin(), argv.end());
    for (std::vector<std::string>::const_iterator it = contained.begin();
         it != contained.end();
         ++it) {
      EXPECT_EQ(argv_set.count(*it), 1) << "argv should contain " << *it;
    }
  }

  std::map<std::string, std::string> env_;
  std::vector<std::string> argv_;
  MockFileChecker checker_;
  MockMetrics metrics_;
  MockSystemUtils utils_;
  scoped_ptr<BrowserJob> job_;

 private:
  DISALLOW_COPY_AND_ASSIGN(BrowserJobTest);
};

// Default argument list for a job to use in mostly all test cases.
const char* BrowserJobTest::kArgv[] = {"zero", "one", "two"};

// Normal username to test session for.
const char BrowserJobTest::kUser[] = "test@gmail.com";
const char BrowserJobTest::kHash[] = "fake_hash";

void BrowserJobTest::SetUp() {
  argv_ =
      std::vector<std::string>(kArgv, kArgv + arraysize(BrowserJobTest::kArgv));
  job_.reset(new BrowserJob(argv_, env_, 1, &checker_, &metrics_, &utils_));
}

TEST_F(BrowserJobTest, InitializationTest) {
  EXPECT_FALSE(job_->removed_login_manager_flag_);
  std::vector<std::string> job_args = job_->ExportArgv();
  ASSERT_EQ(argv_.size(), job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
}

TEST_F(BrowserJobTest, WaitAndAbort) {
  pid_t kDummyPid = 4;
  EXPECT_CALL(utils_, fork()).WillOnce(Return(kDummyPid));
  EXPECT_CALL(utils_, kill(-kDummyPid, _, SIGABRT)).Times(1);
  EXPECT_CALL(utils_, time(NULL)).WillRepeatedly(Return(0));
  EXPECT_CALL(utils_, ProcessGroupIsGone(kDummyPid, _)).WillOnce(Return(false));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  ASSERT_TRUE(job_->RunInBackground());
  job_->WaitAndAbort(base::TimeDelta::FromSeconds(3));

  // Check for termination message.
  base::FilePath term_file(utils_.GetUniqueFilename());
  ASSERT_FALSE(term_file.empty());
}

TEST_F(BrowserJobTest, WaitAndAbort_AlreadyGone) {
  pid_t kDummyPid = 4;
  EXPECT_CALL(utils_, fork()).WillOnce(Return(kDummyPid));
  EXPECT_CALL(utils_, time(NULL)).WillRepeatedly(Return(0));
  EXPECT_CALL(utils_, ProcessGroupIsGone(kDummyPid, _)).WillOnce(Return(true));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  ASSERT_TRUE(job_->RunInBackground());
  job_->WaitAndAbort(base::TimeDelta::FromSeconds(3));

  // Check for no termination message.
  std::string sent_message;
  base::FilePath term_file(utils_.GetUniqueFilename());
  ASSERT_FALSE(utils_.ReadFileToString(term_file, &sent_message));
}

TEST_F(BrowserJobTest, ShouldStopTest) {
  EXPECT_CALL(utils_, time(NULL))
      .WillRepeatedly(Return(BrowserJob::kRestartWindowSeconds));
  for (uint32_t i = 0; i < BrowserJob::kRestartTries - 1; ++i)
    job_->RecordTime();
  // We haven't yet saturated the list of start times, so...
  EXPECT_FALSE(job_->ShouldStop());

  // Go ahead and saturate.
  job_->RecordTime();
  EXPECT_NE(0, job_->start_times_.front());
  EXPECT_TRUE(job_->ShouldStop());
}

TEST_F(BrowserJobTest, ShouldNotStopTest) {
  EXPECT_CALL(utils_, time(NULL))
      .WillOnce(Return(BrowserJob::kRestartWindowSeconds))
      .WillOnce(Return(3 * BrowserJob::kRestartWindowSeconds));
  job_->RecordTime();
  EXPECT_FALSE(job_->ShouldStop());
}

TEST_F(BrowserJobTest, ShouldNotRunTest) {
  EXPECT_CALL(checker_, exists()).WillRepeatedly(Return(true));
  EXPECT_FALSE(job_->ShouldRunBrowser());
}

TEST_F(BrowserJobTest, ShouldRunTest) {
  EXPECT_CALL(checker_, exists()).WillRepeatedly(Return(false));
  EXPECT_TRUE(job_->ShouldRunBrowser());
}

TEST_F(BrowserJobTest, NullFileCheckerTest) {
  BrowserJob job(argv_, env_, 1, NULL, &metrics_, &utils_);
  EXPECT_TRUE(job.ShouldRunBrowser());
}

// On the job's first run, it should have a one-time-flag.  That
// should get cleared and not used again.
TEST_F(BrowserJobTest, OneTimeBootFlags) {
  EXPECT_CALL(utils_, fork()).WillRepeatedly(Return(1));
  EXPECT_CALL(utils_, time(NULL)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec())
      .WillOnce(Return(false))
      .WillOnce(Return(true));
  EXPECT_CALL(metrics_, RecordStats(StrEq(("chrome-exec")))).Times(2);

  ASSERT_TRUE(job_->RunInBackground());
  ExpectArgsToContainFlag(
      job_->ExportArgv(), BrowserJob::kFirstExecAfterBootFlag, "");

  ASSERT_TRUE(job_->RunInBackground());
  ExpectArgsNotToContainFlag(
      job_->ExportArgv(), BrowserJob::kFirstExecAfterBootFlag, "");
}

TEST_F(BrowserJobTest, RunBrowserTermMessage) {
  pid_t kDummyPid = 4;
  int signal = SIGKILL;
  EXPECT_CALL(utils_, fork()).WillOnce(Return(kDummyPid));
  EXPECT_CALL(utils_, kill(kDummyPid, _, signal)).Times(1);
  EXPECT_CALL(utils_, time(NULL)).WillRepeatedly(Return(0));

  EXPECT_CALL(metrics_, HasRecordedChromeExec()).WillRepeatedly(Return(false));
  EXPECT_CALL(metrics_, RecordStats(_)).Times(AnyNumber());

  std::string term_message("killdya");
  ASSERT_TRUE(job_->RunInBackground());
  job_->Kill(signal, term_message);

  // Check for termination message.
  std::string sent_message;
  base::FilePath term_file(utils_.GetUniqueFilename());
  ASSERT_FALSE(term_file.empty());
  ASSERT_TRUE(utils_.ReadFileToString(term_file, &sent_message));
  EXPECT_EQ(term_message, sent_message);
}

TEST_F(BrowserJobTest, StartStopSessionTest) {
  job_->StartSession(kUser, kHash);

  std::vector<std::string> job_args = job_->ExportArgv();
  ASSERT_LT(argv_.size(), job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginUserFlag, kUser);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginProfileFlag, kHash);

  // Should remove login user flag.
  job_->StopSession();
  job_args = job_->ExportArgv();
  ASSERT_EQ(argv_.size(), job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
}

TEST_F(BrowserJobTest, StartStopMultiSessionTest) {
  BrowserJob job(argv_, env_, 1, &checker_, &metrics_, &utils_);
  job.StartSession(kUser, kHash);

  std::vector<std::string> job_args = job.ExportArgv();
  ASSERT_EQ(argv_.size() + 2, job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginUserFlag, kUser);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginProfileFlag, kHash);

  // Start another session, expect the args to be unchanged.
  job.StartSession(kUser, kHash);
  job_args = job.ExportArgv();
  ASSERT_EQ(argv_.size() + 2, job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginUserFlag, kUser);
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginProfileFlag, kHash);

  // Should remove login user and login profile flags.
  job.StopSession();
  job_args = job.ExportArgv();
  ASSERT_EQ(argv_.size(), job_args.size());
  ExpectArgsToContainAll(job_args, argv_);
}

TEST_F(BrowserJobTest, StartStopSessionFromLoginTest) {
  const char* kArgvWithLoginFlag[] = {"zero", "one", "two", "--login-manager"};
  std::vector<std::string> argv(
      kArgvWithLoginFlag, kArgvWithLoginFlag + arraysize(kArgvWithLoginFlag));
  BrowserJob job(argv, env_, 1, &checker_, &metrics_, &utils_);

  job.StartSession(kUser, kHash);

  std::vector<std::string> job_args = job.ExportArgv();
  ASSERT_EQ(argv.size() + 1, job_args.size());
  ExpectArgsToContainAll(
      job_args, std::vector<std::string>(argv.begin(), argv.end() - 1));
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginUserFlag, kUser);

  // Should remove login user/hash flags and append --login-manager flag back.
  job.StopSession();
  job_args = job.ExportArgv();
  ASSERT_EQ(argv.size(), job_args.size());
  ExpectArgsToContainAll(job_args, argv);
}

TEST_F(BrowserJobTest, SetArguments) {
  const char* kNewArgs[] = {"--ichi", "--ni dfs", "--san"};
  std::vector<std::string> new_args(kNewArgs, kNewArgs + arraysize(kNewArgs));
  job_->SetArguments(new_args);

  std::vector<std::string> job_args = job_->ExportArgv();
  ASSERT_EQ(new_args.size(), job_args.size());
  EXPECT_EQ(kArgv[0], job_args[0]);
  for (size_t i = 1; i < arraysize(kNewArgs); ++i) {
    EXPECT_EQ(kNewArgs[i], job_args[i]);
  }

  job_->StartSession(kUser, kHash);
  job_args = job_->ExportArgv();
  ExpectArgsToContainFlag(job_args, BrowserJob::kLoginUserFlag, kUser);
}

TEST_F(BrowserJobTest, SetExtraArguments) {
  const char* kExtraArgs[] = {"--ichi", "--ni", "--san"};
  std::vector<std::string> extra_args(kExtraArgs,
                                      kExtraArgs + arraysize(kExtraArgs));
  job_->SetExtraArguments(extra_args);

  std::vector<std::string> job_args = job_->ExportArgv();
  ExpectArgsToContainAll(job_args, argv_);
  ExpectArgsToContainAll(job_args, extra_args);
}

TEST_F(BrowserJobTest, ExportArgv) {
  std::vector<std::string> argv(kArgv, kArgv + arraysize(kArgv));
  BrowserJob job(argv, env_, -1, &checker_, &metrics_, &utils_);

  const char* kExtraArgs[] = {"--ichi", "--ni", "--san"};
  std::vector<std::string> extra_args(kExtraArgs,
                                      kExtraArgs + arraysize(kExtraArgs));
  argv.insert(argv.end(), extra_args.begin(), extra_args.end());
  job.SetExtraArguments(extra_args);
  EXPECT_EQ(argv, job.ExportArgv());
}

}  // namespace login_manager
