// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/run_loop.h>
#include <base/strings/string_split.h>
#include <base/test/task_environment.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/process_fetcher.h"
#include "diagnostics/cros_healthd/system/mock_context.h"
#include "diagnostics/cros_healthd/utils/procfs_utils.h"
#include "mojo/cros_healthd_executor.mojom.h"
#include "mojo/cros_healthd_probe.mojom.h"

using testing::_;
using ::testing::Invoke;
using ::testing::WithArg;

namespace diagnostics {

namespace {

namespace executor_ipc = ::chromeos::cros_healthd_executor::mojom;
namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// POD struct for ParseProcessStateTest.
struct ParseProcessStateTestParams {
  std::string raw_state;
  mojo_ipc::ProcessState expected_mojo_state;
};

// ID of the process to be probed.
constexpr pid_t kPid = 6098;

// Valid fake data for /proc/uptime.
constexpr char kFakeProcUptimeContents[] = "339214.60 2707855.71";
// Incorrectly-formatted /proc/uptime file contents.
constexpr char kInvalidProcUptimeContents[] = "NotANumber 870.980";

// Valid fake data for /proc/|kPid/stat.
constexpr char kFakeProcPidStatContents[] =
    "6098 (fake_exe) S 1 1015 1015 0 -1 4210944 1536 158 1 0 10956 17428 19 37 "
    "20 0 1 0 358 36884480 3515";
// Data parsed from kFakeProcPidStatContents.
constexpr mojo_ipc::ProcessState kExpectedMojoState =
    mojo_ipc::ProcessState::kSleeping;
constexpr int8_t kExpectedPriority = 20;
constexpr int8_t kExpectedNice = 0;
// Invalid /proc/|kPid|/stat: not enough tokens.
constexpr char kProcPidStatContentsInsufficientTokens[] =
    "6098 (fake_exe) S 1 1015 1015 0 -1 4210944";
// Invalid raw process state.
constexpr char kInvalidRawState[] = "InvalidState";
// Invalid priority value.
constexpr char kInvalidPriority[] = "InvalidPriority";
// Priority value too large to fit inside an 8-bit integer.
constexpr char kOverflowingPriority[] = "128";
// Invalid nice value.
constexpr char kInvalidNice[] = "InvalidNice";
// Invalid starttime value.
constexpr char kInvalidStarttime[] = "InvalidStarttime";

// Valid fake data for /proc/|kPid|/statm.
constexpr char kFakeProcPidStatmContents[] = "25648 2657 2357 151 0 18632 0";
// Invalid /proc/|kPid|/statm: not enough tokens.
constexpr char kProcPidStatmContentsInsufficientTokens[] =
    "25648 2657 2357 151 0 18632";
// Invalid /proc/|kPid|/statm: total memory less than resident memory.
constexpr char kProcPidStatmContentsExcessiveResidentMemory[] =
    "2657 25648 2357 151 0 18632 0";
// Invalid /proc/|kPid|/statm: total memory overflows 32-bit unsigned int.
constexpr char kProcPidStatmContentsOverflowingTotalMemory[] =
    "4294967296 2657 2357 151 0 18632 0";
// Invalid /proc/|kPid|/statm: resident memory overflows 32-bit unsigned int.
constexpr char kProcPidStatmContentsOverflowingResidentMemory[] =
    "25648 4294967296 2357 151 0 18632 0";

// Valid fake data for /proc/|kPid|/io.
constexpr char kFakeProcPidIOContents[] =
    "rchar: 44846\n"
    "wchar: 10617\n"
    "syscr: 248\n"
    "syscw: 317\n"
    "read_bytes: 56799232\n"
    "write_bytes: 32768\n"
    "cancelled_write_bytes: 0";

// Data parsed from kFakeProcPidIOContents.
constexpr uint32_t kExpectedBytesRead = 44846;
constexpr uint32_t kExpectedBytesWritten = 10617;
constexpr uint32_t kExpectedReadSystemCalls = 248;
constexpr uint32_t kExpectedWriteSystemCalls = 317;
constexpr uint32_t kExpectedPhysicalBytesRead = 56799232;
constexpr uint32_t kExpectedPhysicalBytesWritten = 32768;
constexpr uint32_t kExpectedCancelledBytesWritten = 0;

// Invalid /proc/|kPid|/io: not enough fields.
constexpr char kFakeProcPidIOContentsInsufficientFields[] =
    "rchar: 44846\n"
    "wchar: 10617\n"
    "syscr: 248\n"
    "read_bytes: 56799232\n"
    "write_bytes: 32768\n"
    "cancelled_write_bytes: 0";

// Valid fake data for /proc/|kPid|/status.
constexpr char kFakeProcPidStatusContents[] =
    "Name:\tfake_exe\nState:\tS (sleeping)\nUid:\t20104 20104 20104 20104\n";
// Data parsed from kFakeProcPidStatusContents.
constexpr uint32_t kExpectedUid = 20104;
// Invalid /proc/|kPid|/status contents: doesn't tokenize on ":".
constexpr char kProcPidStatusContentsNotTokenizeable[] =
    "Name:\tfake_exe\nState;\tS (sleeping)\nUid:\t20104 20104 20104 20104\n";
// Invalid /proc/|kPid|/status contents: Uid key not present.
constexpr char kProcPidStatusContentsNoUidKey[] =
    "Name:\tfake_exe\nState:\tS (sleeping)\n";
// Invalid /proc/|kPid|/status contents: Uid key doesn't have four values.
constexpr char kProcPidStatusContentsNotEnoughUidValues[] =
    "Name:\tfake_exe\nState:\tS (sleeping)\nUid:\t20104 20104 20104\n";
// Invalid /proc/|kPid|/status contents: Uid key value is negative.
constexpr char kProcPidStatusContentsNegativeUidValue[] =
    "Name:\tfake_exe\nState:\tS (sleeping)\nUid:\t-20104 20104 20104 20104\n";

// Valid fake data for /proc/|kPid|/cmdline. Note that this is an arbitrary
// string, so there is no invalid data for this file.
constexpr char kFakeProcPidCmdlineContents[] = "/usr/bin/fake_exe --arg=yes";

// Saves |response| to |response_destination|.
void OnMojoResponseReceived(mojo_ipc::ProcessResultPtr* response_destination,
                            base::Closure quit_closure,
                            mojo_ipc::ProcessResultPtr response) {
  DCHECK(response_destination);
  *response_destination = std::move(response);
  quit_closure.Run();
}

}  // namespace

class ProcessFetcherTest : public testing::Test {
 protected:
  ProcessFetcherTest() = default;

  void SetUp() override {
    ASSERT_TRUE(mock_context_.Initialize());
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    // Set up valid files for the process with PID |kPid|. Individual tests are
    // expected to override this configuration when necessary.

    // Write /proc/uptime.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcUptimePath(temp_dir_path()),
                                             kFakeProcUptimeContents));
    // Write /proc/|kPid|/stat.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetProcProcessDirectoryPath(temp_dir_path(), kPid)
            .Append(kProcessStatFile),
        kFakeProcPidStatContents));
    // Write /proc/|kPid|/statm.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetProcProcessDirectoryPath(temp_dir_path(), kPid)
            .Append(kProcessStatmFile),
        kFakeProcPidStatmContents));
    // Write /proc/|kPid|/io.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetProcProcessDirectoryPath(temp_dir_path(), kPid)
            .Append(kProcessIOFile),
        kFakeProcPidIOContents));
    // Write /proc/|kPid|/status.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetProcProcessDirectoryPath(temp_dir_path(), kPid)
            .Append(kProcessStatusFile),
        kFakeProcPidStatusContents));
    // Write /proc/|kPid|/cmdline.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetProcProcessDirectoryPath(temp_dir_path(), kPid)
            .Append(kProcessCmdlineFile),
        kFakeProcPidCmdlineContents));
  }

  MockExecutorAdapter* mock_executor() { return mock_context_.mock_executor(); }

  mojo_ipc::ProcessResultPtr FetchProcessInfo() {
    mojo_ipc::ProcessResultPtr result;
    base::RunLoop run_loop;
    ProcessFetcher(&mock_context_, kPid, temp_dir_path())
        .FetchProcessInfo(base::BindOnce(&OnMojoResponseReceived, &result,
                                         run_loop.QuitClosure()));
    run_loop.Run();

    return result;
  }

  bool WriteProcPidStatData(const std::string& new_data,
                            ProcPidStatIndices index) {
    // Tokenize the fake /proc/|kPid|/stat data.
    std::vector<std::string> tokens =
        base::SplitString(kFakeProcPidStatContents, " ", base::TRIM_WHITESPACE,
                          base::SPLIT_WANT_NONEMPTY);

    // Shove in the new data.
    tokens[index] = new_data;

    // Reconstruct the fake data in the correct format.
    std::string new_fake_data;
    for (const auto& token : tokens)
      new_fake_data = new_fake_data + token + " ";

    // Write the new fake data.
    return WriteFileAndCreateParentDirs(
        GetProcProcessDirectoryPath(temp_dir_path(), kPid)
            .Append(kProcessStatFile),
        new_fake_data);
  }

  void ExpectAndSetExecutorGetProcessIOContentsResponse(
      const std::string& io_contents) {
    // Set the mock executor response.
    EXPECT_CALL(*mock_context_.mock_executor(), GetProcessIOContents(_, _))
        .WillOnce(WithArg<1>(Invoke(
            [=](executor_ipc::Executor::GetProcessIOContentsCallback callback) {
              std::string content;
              content = io_contents;
              std::move(callback).Run(content);
            })));
  }

  const base::FilePath& temp_dir_path() const { return temp_dir_.GetPath(); }

 private:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  base::ScopedTempDir temp_dir_;
  MockContext mock_context_;
};

// Test that process info can be read when it exists.
TEST_F(ProcessFetcherTest, FetchProcessInfo) {
  ExpectAndSetExecutorGetProcessIOContentsResponse(kFakeProcPidIOContents);
  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_process_info());
  const auto& process_info = process_result->get_process_info();
  EXPECT_EQ(process_info->command, kFakeProcPidCmdlineContents);
  EXPECT_EQ(process_info->user_id, kExpectedUid);
  EXPECT_EQ(process_info->priority, kExpectedPriority);
  EXPECT_EQ(process_info->nice, kExpectedNice);
  // TODO(crbug/1105605): Test the expected uptime, once it no longer depends on
  // sysconf.
  EXPECT_EQ(process_info->state, kExpectedMojoState);
  EXPECT_EQ(process_info->bytes_read, kExpectedBytesRead);
  EXPECT_EQ(process_info->bytes_written, kExpectedBytesWritten);
  EXPECT_EQ(process_info->read_system_calls, kExpectedReadSystemCalls);
  EXPECT_EQ(process_info->write_system_calls, kExpectedWriteSystemCalls);
  EXPECT_EQ(process_info->physical_bytes_read, kExpectedPhysicalBytesRead);
  EXPECT_EQ(process_info->physical_bytes_written,
            kExpectedPhysicalBytesWritten);
  EXPECT_EQ(process_info->cancelled_bytes_written,
            kExpectedCancelledBytesWritten);
}

// Test that we handle a missing /proc/uptime file.
TEST_F(ProcessFetcherTest, MissingProcUptimeFile) {
  ASSERT_TRUE(base::DeleteFile(GetProcUptimePath(temp_dir_path())));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted /proc/uptime file.
TEST_F(ProcessFetcherTest, IncorrectlyFormattedProcUptimeFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcUptimePath(temp_dir_path()),
                                           kInvalidProcUptimeContents));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a missing /proc/|kPid|/cmdline file.
TEST_F(ProcessFetcherTest, MissingProcPidCmdlineFile) {
  ASSERT_TRUE(
      base::DeleteFile(GetProcProcessDirectoryPath(temp_dir_path(), kPid)
                           .Append(kProcessCmdlineFile)));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle a missing /proc/|kPid|/stat file.
TEST_F(ProcessFetcherTest, MissingProcPidStatFile) {
  ASSERT_TRUE(
      base::DeleteFile(GetProcProcessDirectoryPath(temp_dir_path(), kPid)
                           .Append(kProcessStatFile)));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle a missing /proc/|kPid|/statm file.
TEST_F(ProcessFetcherTest, MissingProcPidStatmFile) {
  ASSERT_TRUE(
      base::DeleteFile(GetProcProcessDirectoryPath(temp_dir_path(), kPid)
                           .Append(kProcessStatmFile)));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle a missing /proc/|kPid|/io file.
TEST_F(ProcessFetcherTest, MissingProcPidIOFile) {
  ASSERT_TRUE(base::DeleteFile(
      GetProcProcessDirectoryPath(temp_dir_path(), kPid).Append(kProcessIOFile),
      false));
  ExpectAndSetExecutorGetProcessIOContentsResponse("");

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle a /proc/|kPid|/stat file with insufficient tokens.
TEST_F(ProcessFetcherTest, ProcPidStatFileInsufficientTokens) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcProcessDirectoryPath(temp_dir_path(), kPid)
          .Append(kProcessStatFile),
      kProcPidStatContentsInsufficientTokens));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle an invalid state read from the /proc/|kPid|/stat file.
TEST_F(ProcessFetcherTest, InvalidProcessStateRead) {
  ASSERT_TRUE(
      WriteProcPidStatData(kInvalidRawState, ProcPidStatIndices::kState));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle an invalid priority read from the /proc/|kPid|/stat file.
TEST_F(ProcessFetcherTest, InvalidProcessPriorityRead) {
  ASSERT_TRUE(
      WriteProcPidStatData(kInvalidPriority, ProcPidStatIndices::kPriority));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle an invalid nice value read from the /proc/|kPid|/stat
// file.
TEST_F(ProcessFetcherTest, InvalidProcessNiceRead) {
  ASSERT_TRUE(WriteProcPidStatData(kInvalidNice, ProcPidStatIndices::kNice));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we can handle an overflowing priority value from the
// /proc/|kPid|/stat file.
TEST_F(ProcessFetcherTest, OverflowingPriorityRead) {
  ASSERT_TRUE(WriteProcPidStatData(kOverflowingPriority,
                                   ProcPidStatIndices::kPriority));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle an invalid starttime read from the /proc/|kPid|/stat
// file.
TEST_F(ProcessFetcherTest, InvalidProcessStarttimeRead) {
  ASSERT_TRUE(
      WriteProcPidStatData(kInvalidStarttime, ProcPidStatIndices::kStartTime));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a /proc/|kPid|/statm file with insufficient tokens.
TEST_F(ProcessFetcherTest, ProcPidStatmFileInsufficientTokens) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcProcessDirectoryPath(temp_dir_path(), kPid)
          .Append(kProcessStatmFile),
      kProcPidStatmContentsInsufficientTokens));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a /proc/|kPid|/statm file with an invalid total memory
// value.
TEST_F(ProcessFetcherTest, ProcPidStatmFileInvalidTotalMemory) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcProcessDirectoryPath(temp_dir_path(), kPid)
          .Append(kProcessStatmFile),
      kProcPidStatmContentsOverflowingTotalMemory));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a /proc/|kPid|/statm file with an invalid resident memory
// value.
TEST_F(ProcessFetcherTest, ProcPidStatmFileInvalidResidentMemory) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcProcessDirectoryPath(temp_dir_path(), kPid)
          .Append(kProcessStatmFile),
      kProcPidStatmContentsOverflowingResidentMemory));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a /proc/|kPid|/io file with insufficient fields.
TEST_F(ProcessFetcherTest, ProcPidIOFileInsufficientTokens) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcProcessDirectoryPath(temp_dir_path(), kPid).Append(kProcessIOFile),
      kFakeProcPidIOContentsInsufficientFields));
  ExpectAndSetExecutorGetProcessIOContentsResponse(
      kFakeProcPidIOContentsInsufficientFields);

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a /proc/|kPid|/statm file with resident memory value
// higher than the total memory value.
TEST_F(ProcessFetcherTest, ProcPidStatmFileExcessiveResidentMemory) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcProcessDirectoryPath(temp_dir_path(), kPid)
          .Append(kProcessStatmFile),
      kProcPidStatmContentsExcessiveResidentMemory));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a missing /proc/|kPid|/status file.
TEST_F(ProcessFetcherTest, MissingProcPidStatusFile) {
  ASSERT_TRUE(
      base::DeleteFile(GetProcProcessDirectoryPath(temp_dir_path(), kPid)
                           .Append(kProcessStatusFile)));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle a /proc/|kPid|/status file which doesn't tokenize.
TEST_F(ProcessFetcherTest, NonTokenizeableProcPidStatusFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcProcessDirectoryPath(temp_dir_path(), kPid)
          .Append(kProcessStatusFile),
      kProcPidStatusContentsNotTokenizeable));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a /proc/|kPid|/status file which doesn't have the Uid
// key.
TEST_F(ProcessFetcherTest, ProcPidStatusFileNoUidKey) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcProcessDirectoryPath(temp_dir_path(), kPid)
          .Append(kProcessStatusFile),
      kProcPidStatusContentsNoUidKey));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a /proc/|kPid|/status file with a Uid key with less than
// four values.
TEST_F(ProcessFetcherTest, ProcPidStatusFileUidKeyInsufficientValues) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcProcessDirectoryPath(temp_dir_path(), kPid)
          .Append(kProcessStatusFile),
      kProcPidStatusContentsNotEnoughUidValues));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a /proc/|kPid|/status file with a Uid key with negative
// values.
TEST_F(ProcessFetcherTest, ProcPidStatusFileUidKeyWithNegativeValues) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetProcProcessDirectoryPath(temp_dir_path(), kPid)
          .Append(kProcessStatusFile),
      kProcPidStatusContentsNegativeUidValue));

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_error());
  EXPECT_EQ(process_result->get_error()->type,
            mojo_ipc::ErrorType::kParseError);
}

// Tests that ProcessFetcher can correctly parse each process state.
//
// This is a parameterized test with the following parameters (accessed
// through the ParseProcessStateTestParams POD struct):
// * |raw_state| - written to /proc/|kPid|/stat's process state field.
// * |expected_mojo_state| - expected value of the returned ProcessInfo's state
//                           field.
class ParseProcessStateTest
    : public ProcessFetcherTest,
      public testing::WithParamInterface<ParseProcessStateTestParams> {
 protected:
  // Accessors to the test parameters returned by gtest's GetParam():
  ParseProcessStateTestParams params() const { return GetParam(); }
};

// Test that we can parse the given process state.
TEST_P(ParseProcessStateTest, ParseState) {
  ASSERT_TRUE(
      WriteProcPidStatData(params().raw_state, ProcPidStatIndices::kState));
  ExpectAndSetExecutorGetProcessIOContentsResponse(kFakeProcPidIOContents);

  auto process_result = FetchProcessInfo();

  ASSERT_TRUE(process_result->is_process_info());
  EXPECT_EQ(process_result->get_process_info()->state,
            params().expected_mojo_state);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ParseProcessStateTest,
    testing::Values(ParseProcessStateTestParams{
                        /*raw_state=*/"R", mojo_ipc::ProcessState::kRunning},
                    ParseProcessStateTestParams{
                        /*raw_state=*/"S", mojo_ipc::ProcessState::kSleeping},
                    ParseProcessStateTestParams{
                        /*raw_state=*/"D", mojo_ipc::ProcessState::kWaiting},
                    ParseProcessStateTestParams{
                        /*raw_state=*/"Z", mojo_ipc::ProcessState::kZombie},
                    ParseProcessStateTestParams{
                        /*raw_state=*/"T", mojo_ipc::ProcessState::kStopped},
                    ParseProcessStateTestParams{
                        /*raw_state=*/"t",
                        mojo_ipc::ProcessState::kTracingStop},
                    ParseProcessStateTestParams{
                        /*raw_state=*/"X", mojo_ipc::ProcessState::kDead}));

}  // namespace diagnostics
