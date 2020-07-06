// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "diagnostics/common/file_test_utils.h"
#include "diagnostics/cros_healthd/fetchers/cpu_fetcher.h"
#include "diagnostics/cros_healthd/utils/cpu_file_helpers.h"
#include "diagnostics/cros_healthd/utils/procfs_utils.h"
#include "mojo/cros_healthd_probe.mojom.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

using ::testing::UnorderedElementsAreArray;

// No other logical IDs should be used, or the logic for writing C-state files
// will break.
constexpr char kFirstLogicalId[] = "0";
constexpr char kSecondLogicalId[] = "1";
constexpr char kThirdLogicalId[] = "12";

// First C-State directory to be written.
constexpr char kFirstCStateDir[] = "state0";

constexpr char kNonIntegralFileContents[] = "Not an integer!";

constexpr char kBadCpuinfoContents[] =
    "processor\t: 0\nmodel \t: Dank CPU 1 @ 8.90GHz\n\n";
constexpr char kNoPhysicalIdCpuinfoContents[] =
    "processor\t: 0\nmodel name\t: Dank CPU 1 @ 8.90GHz\n\n"
    "processor\t: 1\nmodel name\t: Dank CPU 1 @ 8.90GHzn\n\n"
    "processor\t: 12\nmodel name\t: Dank CPU 2 @ 2.80GHz\n\n";
constexpr char kFakeCpuinfoContents[] =
    "processor\t: 0\nmodel name\t: Dank CPU 1 @ 8.90GHz\nphysical id\t: 0\n\n"
    "processor\t: 1\nmodel name\t: Dank CPU 1 @ 8.90GHz\nphysical id\t: 0\n\n"
    "processor\t: 12\nmodel name\t: Dank CPU 2 @ 2.80GHz\nphysical id\t: 1\n\n";
constexpr char kFirstFakeModelName[] = "Dank CPU 1 @ 8.90GHz";
constexpr char kSecondFakeModelName[] = "Dank CPU 2 @ 2.80GHz";

constexpr uint32_t kFirstFakeMaxClockSpeed = 3400000;
constexpr uint32_t kSecondFakeMaxClockSpeed = 1600000;
constexpr uint32_t kThirdFakeMaxClockSpeed = 1800000;

constexpr char kBadPresentContents[] = "Char-7";
constexpr char kFakePresentContents[] = "0-7";
constexpr uint32_t kExpectedNumTotalThreads = 8;

constexpr uint32_t kFirstFakeScalingCurrentFrequency = 859429;
constexpr uint32_t kSecondFakeScalingCurrentFrequency = 637382;
constexpr uint32_t kThirdFakeScalingCurrentFrequency = 737382;

constexpr uint32_t kFirstFakeScalingMaxFrequency = 2800000;
constexpr uint32_t kSecondFakeScalingMaxFrequency = 1400000;
constexpr uint32_t kThirdFakeScalingMaxFrequency = 1700000;

constexpr char kFirstFakeCStateNameContents[] = "C1-SKL";
constexpr uint64_t kFirstFakeCStateTime = 536018855;
constexpr char kSecondFakeCStateNameContents[] = "C10-SKL";
constexpr uint64_t kSecondFakeCStateTime = 473634000891;
constexpr char kThirdFakeCStateNameContents[] = "C7s-SKL";
constexpr uint64_t kThirdFakeCStateTime = 473634000891;
constexpr char kFourthFakeCStateNameContents[] = "C1E-SKL";
constexpr uint64_t kFourthFakeCStateTime = 79901786;

constexpr char kBadStatContents[] =
    "cpu   12389 69724 98732420 420347203\ncpu0  0 10 890 473634000891\n";
constexpr char kMissingLogicalCpuStatContents[] =
    "cpu   12389 69724 98732420 420347203\n"
    "cpu0  69234 98 0 2349\n"
    "cpu12 0 64823 293802 871239\n";
constexpr char kFakeStatContents[] =
    "cpu   12389 69724 98732420 420347203\n"
    "cpu0  69234 98 0 2349\n"
    "cpu1  989 0 4536824 123\n"
    "cpu12 0 64823 293802 871239\n";
constexpr uint32_t kFirstFakeIdleTime = 2349;
constexpr uint32_t kSecondFakeIdleTime = 123;
constexpr uint32_t kThirdFakeIdleTime = 871239;

// Workaround for UnorderedElementsAreArray not accepting move-only types - this
// simple matcher expects a std::cref(mojo_ipc::CStateInfoPtr) and checks
// each of the fields for equality.
MATCHER_P(MatchesCStateInfoPtr, ptr, "") {
  return arg->name == ptr.get()->name &&
         arg->time_in_state_since_last_boot_us ==
             ptr.get()->time_in_state_since_last_boot_us;
}

// Note that this function only works for Logical CPUs with one or two C-states.
// Luckily, that's all we need for solid unit tests.
void VerifyLogicalCpu(
    uint32_t expected_max_clock_speed_khz,
    uint32_t expected_scaling_max_frequency_khz,
    uint32_t expected_scaling_current_frequency_khz,
    uint32_t expected_idle_time_user_hz,
    const std::vector<std::pair<std::string, uint64_t>>& expected_c_states,
    const mojo_ipc::LogicalCpuInfoPtr& actual_data) {
  ASSERT_FALSE(actual_data.is_null());
  EXPECT_EQ(actual_data->max_clock_speed_khz, expected_max_clock_speed_khz);
  EXPECT_EQ(actual_data->scaling_max_frequency_khz,
            expected_scaling_max_frequency_khz);
  EXPECT_EQ(actual_data->scaling_current_frequency_khz,
            expected_scaling_current_frequency_khz);
  EXPECT_EQ(actual_data->idle_time_user_hz, expected_idle_time_user_hz);

  const auto& c_states = actual_data->c_states;
  int c_state_size = c_states.size();
  int expected_c_state_size = expected_c_states.size();
  ASSERT_TRUE(c_state_size == expected_c_state_size &&
              (c_state_size == 1 || c_state_size == 2));
  if (c_state_size == 1) {
    const auto& c_state = c_states[0];
    ASSERT_FALSE(c_state.is_null());
    const auto& expected_c_state = expected_c_states[0];
    EXPECT_EQ(c_state->name, expected_c_state.first);
    EXPECT_EQ(c_state->time_in_state_since_last_boot_us,
              expected_c_state.second);
  } else {
    // Since fetching C-states uses base::FileEnumerator, we're not guaranteed
    // the order of the two results.
    auto first_expected_c_state = mojo_ipc::CpuCStateInfo::New(
        expected_c_states[0].first, expected_c_states[0].second);
    auto second_expected_c_state = mojo_ipc::CpuCStateInfo::New(
        expected_c_states[1].first, expected_c_states[1].second);
    EXPECT_THAT(
        c_states,
        UnorderedElementsAreArray(
            {MatchesCStateInfoPtr(std::cref(first_expected_c_state)),
             MatchesCStateInfoPtr(std::cref(second_expected_c_state))}));
  }
}

}  // namespace

class CpuUtilsTest : public testing::Test {
 protected:
  CpuUtilsTest() = default;

  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());

    // Set up valid files for two physical CPUs, the first of which has two
    // logical CPUs. Individual tests are expected to override this
    // configuration when necessary.

    // Write /proc/cpuinfo.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetProcCpuInfoPath(temp_dir_path()), kFakeCpuinfoContents));
    // Write /proc/stat.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcStatPath(temp_dir_path()),
                                             kFakeStatContents));
    // Write /sys/devices/system/cpu/present.
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        GetCpuDirectoryPath(temp_dir_path()).Append(kCpuPresentFile),
        kFakePresentContents));
    // Write policy data for the first logical CPU.
    WritePolicyData(std::to_string(kFirstFakeMaxClockSpeed),
                    std::to_string(kFirstFakeScalingMaxFrequency),
                    std::to_string(kFirstFakeScalingCurrentFrequency),
                    kFirstLogicalId);
    // Write policy data for the second logical CPU.
    WritePolicyData(std::to_string(kSecondFakeMaxClockSpeed),
                    std::to_string(kSecondFakeScalingMaxFrequency),
                    std::to_string(kSecondFakeScalingCurrentFrequency),
                    kSecondLogicalId);
    // Write policy data for the third logical CPU.
    WritePolicyData(std::to_string(kThirdFakeMaxClockSpeed),
                    std::to_string(kThirdFakeScalingMaxFrequency),
                    std::to_string(kThirdFakeScalingCurrentFrequency),
                    kThirdLogicalId);
    // Write C-state data for the first logical CPU.
    WriteCStateData(kFirstCStates, kFirstLogicalId);
    // Write C-state data for the second logical CPU.
    WriteCStateData(kSecondCStates, kSecondLogicalId);
    // Write C-state data for the third logical CPU.
    WriteCStateData(kThirdCStates, kThirdLogicalId);
  }

  const base::FilePath& temp_dir_path() const { return temp_dir_.GetPath(); }

  const std::vector<std::pair<std::string, uint64_t>>& GetCStateVector(
      const std::string& logical_id) {
    if (logical_id == kFirstLogicalId) {
      return kFirstCStates;
    } else if (logical_id == kSecondLogicalId) {
      return kSecondCStates;
    } else if (logical_id == kThirdLogicalId) {
      return kThirdCStates;
    }

    NOTREACHED();
    return kFirstCStates;
  }

 private:
  // Writes pairs of data into the name and time files of the appropriate
  // C-state directory.
  void WriteCStateData(
      const std::vector<std::pair<std::string, uint64_t>>& data,
      const std::string& logical_id) {
    for (const auto& pair : data)
      WriteCStateFiles(logical_id, pair.first, std::to_string(pair.second));
  }

  // Writes to cpuinfo_max_freq, scaling_max_freq, and scaling_cur_freq. If any
  // of the optional values are base::nullopt, the corresponding file will not
  // be written.
  void WritePolicyData(const std::string cpuinfo_max_freq_contents,
                       const std::string scaling_max_freq_contents,
                       const std::string scaling_cur_freq_contents,
                       const std::string& logical_id) {
    WritePolicyFile(logical_id, kCpuinfoMaxFreqFile, cpuinfo_max_freq_contents);

    WritePolicyFile(logical_id, kCpuScalingMaxFreqFile,
                    scaling_max_freq_contents);

    WritePolicyFile(logical_id, kCpuScalingCurFreqFile,
                    scaling_cur_freq_contents);
  }

  // Helper to write individual C-state files.
  void WriteCStateFiles(const std::string& logical_id,
                        const std::string& name_contents,
                        const std::string& time_contents) {
    auto policy_dir = GetCStateDirectoryPath(temp_dir_path(), logical_id);
    int state_to_write = c_states_written[logical_id];
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        policy_dir.Append("state" + std::to_string(state_to_write))
            .Append(kCStateNameFile),
        name_contents));
    ASSERT_TRUE(WriteFileAndCreateParentDirs(
        policy_dir.Append("state" + std::to_string(state_to_write))
            .Append(kCStateTimeFile),
        time_contents));
    c_states_written[logical_id] += 1;
  }

  // Helper to write individual policy files.
  void WritePolicyFile(const std::string& logical_id,
                       const std::string& file_name,
                       const std::string& file_contents) {
    auto policy_dir = GetCpuFreqDirectoryPath(temp_dir_path(), logical_id);
    ASSERT_TRUE(WriteFileAndCreateParentDirs(policy_dir.Append(file_name),
                                             file_contents));
  }

  base::ScopedTempDir temp_dir_;
  // Records the next C-state file to be written.
  std::map<std::string, int> c_states_written = {
      {kFirstLogicalId, 0}, {kSecondLogicalId, 0}, {kThirdLogicalId, 0}};
  // C-state data for each of the three logical CPUs tested.
  const std::vector<std::pair<std::string, uint64_t>> kFirstCStates = {
      {kFirstFakeCStateNameContents, kFirstFakeCStateTime},
      {kSecondFakeCStateNameContents, kSecondFakeCStateTime}};
  const std::vector<std::pair<std::string, uint64_t>> kSecondCStates = {
      {kThirdFakeCStateNameContents, kThirdFakeCStateTime}};
  const std::vector<std::pair<std::string, uint64_t>> kThirdCStates = {
      {kFourthFakeCStateNameContents, kFourthFakeCStateTime}};
};

// Test that CPU info can be read when it exists.
TEST_F(CpuUtilsTest, TestFetchCpuInfo) {
  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info->num_total_threads, kExpectedNumTotalThreads);
  const auto& physical_cpus = cpu_info->physical_cpus;
  ASSERT_EQ(physical_cpus.size(), 2);
  const auto& first_physical_cpu = physical_cpus[0];
  ASSERT_FALSE(first_physical_cpu.is_null());
  EXPECT_EQ(first_physical_cpu->model_name, kFirstFakeModelName);
  const auto& first_logical_cpus = first_physical_cpu->logical_cpus;
  ASSERT_EQ(first_logical_cpus.size(), 2);
  VerifyLogicalCpu(kFirstFakeMaxClockSpeed, kFirstFakeScalingMaxFrequency,
                   kFirstFakeScalingCurrentFrequency, kFirstFakeIdleTime,
                   GetCStateVector(kFirstLogicalId), first_logical_cpus[0]);
  VerifyLogicalCpu(kSecondFakeMaxClockSpeed, kSecondFakeScalingMaxFrequency,
                   kSecondFakeScalingCurrentFrequency, kSecondFakeIdleTime,
                   GetCStateVector(kSecondLogicalId), first_logical_cpus[1]);
  const auto& second_physical_cpu = physical_cpus[1];
  ASSERT_FALSE(second_physical_cpu.is_null());
  EXPECT_EQ(second_physical_cpu->model_name, kSecondFakeModelName);
  const auto& second_logical_cpus = second_physical_cpu->logical_cpus;
  ASSERT_EQ(second_logical_cpus.size(), 1);
  VerifyLogicalCpu(kThirdFakeMaxClockSpeed, kThirdFakeScalingMaxFrequency,
                   kThirdFakeScalingCurrentFrequency, kThirdFakeIdleTime,
                   GetCStateVector(kThirdLogicalId), second_logical_cpus[0]);
}

// Test that we handle a cpuinfo file for processors without physical_ids.
TEST_F(CpuUtilsTest, NoPhysicalIdCpuinfoFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(temp_dir_path()),
                                           kNoPhysicalIdCpuinfoContents));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_cpu_info());
  const auto& cpu_info = cpu_result->get_cpu_info();
  EXPECT_EQ(cpu_info->num_total_threads, kExpectedNumTotalThreads);
  const auto& physical_cpus = cpu_info->physical_cpus;
  ASSERT_EQ(physical_cpus.size(), 3);
  const auto& first_physical_cpu = physical_cpus[0];
  ASSERT_FALSE(first_physical_cpu.is_null());
  EXPECT_EQ(first_physical_cpu->model_name, kFirstFakeModelName);
  const auto& first_logical_cpus = first_physical_cpu->logical_cpus;
  ASSERT_EQ(first_logical_cpus.size(), 1);
  VerifyLogicalCpu(kFirstFakeMaxClockSpeed, kFirstFakeScalingMaxFrequency,
                   kFirstFakeScalingCurrentFrequency, kFirstFakeIdleTime,
                   GetCStateVector(kFirstLogicalId), first_logical_cpus[0]);
  const auto& second_physical_cpu = physical_cpus[1];
  ASSERT_FALSE(second_physical_cpu.is_null());
  const auto& second_logical_cpu = second_physical_cpu->logical_cpus;
  ASSERT_EQ(second_logical_cpu.size(), 1);
  VerifyLogicalCpu(kSecondFakeMaxClockSpeed, kSecondFakeScalingMaxFrequency,
                   kSecondFakeScalingCurrentFrequency, kSecondFakeIdleTime,
                   GetCStateVector(kSecondLogicalId), second_logical_cpu[0]);
  const auto& third_physical_cpu = physical_cpus[2];
  ASSERT_FALSE(third_physical_cpu.is_null());
  const auto& third_logical_cpu = third_physical_cpu->logical_cpus;
  ASSERT_EQ(third_logical_cpu.size(), 1);
  VerifyLogicalCpu(kThirdFakeMaxClockSpeed, kThirdFakeScalingMaxFrequency,
                   kThirdFakeScalingCurrentFrequency, kThirdFakeIdleTime,
                   GetCStateVector(kThirdLogicalId), third_logical_cpu[0]);
}

// Test that we handle a missing cpuinfo file.
TEST_F(CpuUtilsTest, MissingCpuinfoFile) {
  ASSERT_TRUE(base::DeleteFile(GetProcCpuInfoPath(temp_dir_path()), false));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted cpuinfo file.
TEST_F(CpuUtilsTest, IncorrectlyFormattedCpuinfoFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcCpuInfoPath(temp_dir_path()),
                                           kBadCpuinfoContents));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a missing stat file.
TEST_F(CpuUtilsTest, MissingStatFile) {
  ASSERT_TRUE(base::DeleteFile(GetProcStatPath(temp_dir_path()), false));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted stat file.
TEST_F(CpuUtilsTest, IncorrectlyFormattedStatFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcStatPath(temp_dir_path()),
                                           kBadStatContents));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a stat file which is missing an entry for an existing
// logical CPU.
TEST_F(CpuUtilsTest, StatFileMissingLogicalCpuEntry) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(GetProcStatPath(temp_dir_path()),
                                           kMissingLogicalCpuStatContents));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a missing present file.
TEST_F(CpuUtilsTest, MissingPresentFile) {
  ASSERT_TRUE(base::DeleteFile(
      GetCpuDirectoryPath(temp_dir_path()).Append(kCpuPresentFile), false));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted present file.
TEST_F(CpuUtilsTest, IncorrectlyFormattedPresentFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCpuDirectoryPath(temp_dir_path()).Append(kCpuPresentFile),
      kBadPresentContents));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kParseError);
}

// Test that we handle a missing cpuinfo_max_freq file.
TEST_F(CpuUtilsTest, MissingCpuinfoMaxFreqFile) {
  ASSERT_TRUE(
      base::DeleteFile(GetCpuFreqDirectoryPath(temp_dir_path(), kFirstLogicalId)
                           .Append(kCpuinfoMaxFreqFile),
                       false));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted cpuinfo_max_freq file.
TEST_F(CpuUtilsTest, IncorrectlyFormattedCpuinfoMaxFreqFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCpuFreqDirectoryPath(temp_dir_path(), kFirstLogicalId)
          .Append(kCpuinfoMaxFreqFile),
      kNonIntegralFileContents));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle a missing scaling_max_freq file.
TEST_F(CpuUtilsTest, MissingScalingMaxFreqFile) {
  ASSERT_TRUE(
      base::DeleteFile(GetCpuFreqDirectoryPath(temp_dir_path(), kFirstLogicalId)
                           .Append(kCpuScalingMaxFreqFile),
                       false));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted scaling_max_freq file.
TEST_F(CpuUtilsTest, IncorrectlyFormattedScalingMaxFreqFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCpuFreqDirectoryPath(temp_dir_path(), kFirstLogicalId)
          .Append(kCpuScalingMaxFreqFile),
      kNonIntegralFileContents));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle a missing scaling_cur_freq file.
TEST_F(CpuUtilsTest, MissingScalingCurFreqFile) {
  ASSERT_TRUE(
      base::DeleteFile(GetCpuFreqDirectoryPath(temp_dir_path(), kFirstLogicalId)
                           .Append(kCpuScalingCurFreqFile),
                       false));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted scaling_cur_freq file.
TEST_F(CpuUtilsTest, IncorrectlyFormattedScalingCurFreqFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCpuFreqDirectoryPath(temp_dir_path(), kFirstLogicalId)
          .Append(kCpuScalingCurFreqFile),
      kNonIntegralFileContents));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle a missing C-state name file.
TEST_F(CpuUtilsTest, MissingCStateNameFile) {
  ASSERT_TRUE(
      base::DeleteFile(GetCStateDirectoryPath(temp_dir_path(), kFirstLogicalId)
                           .Append(kFirstCStateDir)
                           .Append(kCStateNameFile),
                       false));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle a missing C-state time file.
TEST_F(CpuUtilsTest, MissingCStateTimeFile) {
  ASSERT_TRUE(
      base::DeleteFile(GetCStateDirectoryPath(temp_dir_path(), kFirstLogicalId)
                           .Append(kFirstCStateDir)
                           .Append(kCStateTimeFile),
                       false));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

// Test that we handle an incorrectly-formatted C-state time file.
TEST_F(CpuUtilsTest, IncorrectlyFormattedCStateTimeFile) {
  ASSERT_TRUE(WriteFileAndCreateParentDirs(
      GetCStateDirectoryPath(temp_dir_path(), kFirstLogicalId)
          .Append(kFirstCStateDir)
          .Append(kCStateTimeFile),
      kNonIntegralFileContents));

  auto cpu_result = FetchCpuInfo(temp_dir_path());

  ASSERT_TRUE(cpu_result->is_error());
  EXPECT_EQ(cpu_result->get_error()->type, mojo_ipc::ErrorType::kFileReadError);
}

}  // namespace diagnostics
