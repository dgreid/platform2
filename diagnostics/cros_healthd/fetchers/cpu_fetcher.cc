// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/fetchers/cpu_fetcher.h"

#include <sys/utsname.h>

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/optional.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <re2/re2.h>

#include "diagnostics/cros_healthd/system/system_utilities_constants.h"
#include "diagnostics/cros_healthd/utils/cpu_file_helpers.h"
#include "diagnostics/cros_healthd/utils/error_utils.h"
#include "diagnostics/cros_healthd/utils/file_utils.h"
#include "diagnostics/cros_healthd/utils/procfs_utils.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

// Regex used to parse kCpuPresentFile.
constexpr char kPresentFileRegex[] = R"((\d+)-(\d+))";

// Pattern that all C-state directories follow.
constexpr char kCStateDirectoryMatcher[] = "state*";

// Keys used to parse information from /proc/cpuinfo.
constexpr char kModelNameKey[] = "model name";
constexpr char kPhysicalIdKey[] = "physical id";
constexpr char kProcessorIdKey[] = "processor";

// Regex used to parse /proc/stat.
constexpr char kRelativeStatFileRegex[] = R"(cpu(\d+)\s+(\d+) \d+ (\d+) (\d+))";

// Directory containing all CPU temperature subdirectories.
const char kHwmonDir[] = "sys/class/hwmon/";
// Subdirectory of sys/class/hwmon/hwmon*/ which sometimes contains the CPU
// temperature files.
const char kDeviceDir[] = "device";
// Matches all CPU temperature subdirectories of |kHwmonDir|.
const char kHwmonDirectoryPattern[] = "hwmon*";
// Matches all files containing CPU temperatures.
const char kCPUTempFilePattern[] = "temp*_input";

// Contains the values parsed from /proc/stat for a single logical CPU.
struct ParsedStatContents {
  uint64_t user_time_user_hz;
  uint64_t system_time_user_hz;
  uint32_t idle_time_user_hz;
};

// Read system temperature sensor data and appends it to |out_contents|. Returns
// |true| iff there was at least one sensor value in given |sensor_dir|.
bool ReadTemperatureSensorInfo(
    const base::FilePath& sensor_dir,
    std::vector<mojo_ipc::CpuTemperatureChannelPtr>* out_contents) {
  bool has_data = false;

  base::FileEnumerator enumerator(
      sensor_dir, false, base::FileEnumerator::FILES, kCPUTempFilePattern);
  for (base::FilePath temperature_path = enumerator.Next();
       !temperature_path.empty(); temperature_path = enumerator.Next()) {
    // Get appropriate temp*_label file.
    std::string label_path = temperature_path.MaybeAsASCII();
    if (label_path.empty()) {
      LOG(WARNING) << "Unable to parse a path to temp*_input file as ASCII";
      continue;
    }
    base::ReplaceSubstringsAfterOffset(&label_path, 0, "input", "label");
    base::FilePath name_path = sensor_dir.Append("name");

    // Get the label describing this temperature. Use temp*_label
    // if present, fall back on name file.
    std::string label;
    if (base::PathExists(base::FilePath(label_path))) {
      ReadAndTrimString(base::FilePath(label_path), &label);
    } else if (base::PathExists(base::FilePath(name_path))) {
      ReadAndTrimString(name_path, &label);
    }

    // Read temperature in millidegree Celsius.
    int32_t temperature = 0;
    if (ReadInteger(temperature_path, base::StringToInt, &temperature)) {
      has_data = true;
      // Convert from millidegree Celsius to Celsius.
      temperature /= 1000;

      mojo_ipc::CpuTemperatureChannel channel;
      if (!label.empty())
        channel.label = label;
      channel.temperature_celsius = temperature;
      out_contents->push_back(channel.Clone());
    } else {
      LOG(WARNING) << "Unable to read CPU temp from "
                   << temperature_path.MaybeAsASCII();
    }
  }
  return has_data;
}

// Fetches and returns information about the device's CPU temperature channels.
std::vector<mojo_ipc::CpuTemperatureChannelPtr> GetCpuTemperatures(
    const base::FilePath& root_dir) {
  std::vector<mojo_ipc::CpuTemperatureChannelPtr> temps;
  // Get directories /sys/class/hwmon/hwmon*
  base::FileEnumerator hwmon_enumerator(root_dir.AppendASCII(kHwmonDir), false,
                                        base::FileEnumerator::DIRECTORIES,
                                        kHwmonDirectoryPattern);
  for (base::FilePath hwmon_path = hwmon_enumerator.Next(); !hwmon_path.empty();
       hwmon_path = hwmon_enumerator.Next()) {
    // Get temp*_input files in hwmon*/ and hwmon*/device/
    base::FilePath device_path = hwmon_path.Append(kDeviceDir);
    if (base::PathExists(device_path)) {
      // We might have hwmon*/device/, but sensor values are still in hwmon*/
      if (!ReadTemperatureSensorInfo(device_path, &temps)) {
        ReadTemperatureSensorInfo(hwmon_path, &temps);
      }
    } else {
      ReadTemperatureSensorInfo(hwmon_path, &temps);
    }
  }
  return temps;
}

// Gets the time spent in each C-state for the logical processor whose ID is
// |logical_id|. Returns base::nullopt if a required sysfs node was not found.
base::Optional<std::vector<mojo_ipc::CpuCStateInfoPtr>> GetCStates(
    const base::FilePath& root_dir, const std::string logical_id) {
  std::vector<mojo_ipc::CpuCStateInfoPtr> c_states;
  // Find all directories matching /sys/devices/system/cpu/cpuN/cpudidle/stateX.
  base::FileEnumerator c_state_it(
      GetCStateDirectoryPath(root_dir, logical_id), false,
      base::FileEnumerator::SHOW_SYM_LINKS | base::FileEnumerator::FILES |
          base::FileEnumerator::DIRECTORIES,
      kCStateDirectoryMatcher);
  for (base::FilePath c_state_dir = c_state_it.Next(); !c_state_dir.empty();
       c_state_dir = c_state_it.Next()) {
    mojo_ipc::CpuCStateInfo c_state;
    if (!ReadAndTrimString(c_state_dir, kCStateNameFile, &c_state.name) ||
        !ReadInteger(c_state_dir, kCStateTimeFile, &base::StringToUint64,
                     &c_state.time_in_state_since_last_boot_us)) {
      return base::nullopt;
    }
    c_states.push_back(c_state.Clone());
  }

  return c_states;
}

// Reads and parses the total number of threads available on the device. Returns
// an error if encountered, otherwise returns base::nullopt and populates
// |num_total_threads|.
base::Optional<mojo_ipc::ProbeErrorPtr> GetNumTotalThreads(
    const base::FilePath& root_dir, uint32_t* num_total_threads) {
  DCHECK(num_total_threads);

  std::string cpu_present;
  auto cpu_dir = GetCpuDirectoryPath(root_dir);
  if (!ReadAndTrimString(cpu_dir, kCpuPresentFile, &cpu_present)) {
    return CreateAndLogProbeError(mojo_ipc::ErrorType::kFileReadError,
                                  "Unable to read CPU present file: " +
                                      cpu_dir.Append(kCpuPresentFile).value());
  }

  // Two strings will be parsed directly from the regex, then converted to
  // uint32_t's. Expect |cpu_present| to contain the pattern "%d-%d", where the
  // first integer is strictly smaller than the second.
  std::string low_thread_num;
  std::string high_thread_num;
  uint32_t low_thread_int;
  uint32_t high_thread_int;
  if (!RE2::FullMatch(cpu_present, kPresentFileRegex, &low_thread_num,
                      &high_thread_num) ||
      !base::StringToUint(low_thread_num, &low_thread_int) ||
      !base::StringToUint(high_thread_num, &high_thread_int)) {
    return CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Unable to parse CPU present file: " + cpu_present);
  }

  DCHECK_GT(high_thread_int, low_thread_int);
  *num_total_threads = high_thread_int - low_thread_int + 1;
  return base::nullopt;
}

// Parses the contents of /proc/stat into a map of logical IDs to
// ParsedStatContents. Returns base::nullopt if an error was encountered while
// parsing.
base::Optional<std::map<std::string, ParsedStatContents>> ParseStatContents(
    const std::string& stat_contents) {
  std::stringstream stat_sstream(stat_contents);

  // Ignore the first line, since it's aggregated data for the individual
  // logical CPUs.
  std::string line;
  std::getline(stat_sstream, line);

  // Parse lines of the format "cpu%d %d %d %d %d ...", where each line
  // corresponds to a separate logical CPU.
  std::map<std::string, ParsedStatContents> parsed_contents;
  std::string logical_cpu_id;
  std::string user_time_str;
  std::string system_time_str;
  std::string idle_time_str;
  while (std::getline(stat_sstream, line) &&
         RE2::PartialMatch(line, kRelativeStatFileRegex, &logical_cpu_id,
                           &user_time_str, &system_time_str, &idle_time_str)) {
    ParsedStatContents contents;
    if (!base::StringToUint64(user_time_str, &contents.user_time_user_hz) ||
        !base::StringToUint64(system_time_str, &contents.system_time_user_hz) ||
        !base::StringToUint(idle_time_str, &contents.idle_time_user_hz)) {
      return base::nullopt;
    }
    DCHECK_EQ(parsed_contents.count(logical_cpu_id), 0);
    parsed_contents[logical_cpu_id] = std::move(contents);
  }

  return parsed_contents;
}

// Parses |block| to determine if the block parsed from /proc/cpuinfo is a
// processor block.
bool IsProcessorBlock(const std::string& block) {
  base::StringPairs pairs;
  base::SplitStringIntoKeyValuePairs(block, ':', '\n', &pairs);

  if (pairs.size() &&
      pairs[0].first.find(kProcessorIdKey) != std::string::npos) {
    return true;
  }

  return false;
}

// Parses |processor| to obtain |processor_id|, |physical_id|, and |model_name|
// if applicable. Returns true on success.
bool ParseProcessor(const std::string& processor,
                    std::string* processor_id,
                    std::string* physical_id,
                    std::string* model_name) {
  base::StringPairs pairs;
  base::SplitStringIntoKeyValuePairs(processor, ':', '\n', &pairs);
  for (const auto& key_value : pairs) {
    if (key_value.first.find(kProcessorIdKey) != std::string::npos)
      base::TrimWhitespaceASCII(key_value.second, base::TRIM_ALL, processor_id);
    else if (key_value.first.find(kPhysicalIdKey) != std::string::npos)
      base::TrimWhitespaceASCII(key_value.second, base::TRIM_ALL, physical_id);
    else if (key_value.first.find(kModelNameKey) != std::string::npos)
      base::TrimWhitespaceASCII(key_value.second, base::TRIM_ALL, model_name);
  }

  // If the processor does not have a distinction between physical_id and
  // processor_id, make them the same value.
  if (!processor_id->empty() && physical_id->empty()) {
    *physical_id = *processor_id;
  }

  return (!processor_id->empty() && !physical_id->empty());
}

// Aggregates data from |processor_info| and |logical_ids_to_stat_contents| to
// form the final CpuResultPtr. It's assumed that all CPUs on the device share
// the same |architecture|.
mojo_ipc::CpuResultPtr GetCpuInfoFromProcessorInfo(
    const std::vector<std::string>& processor_info,
    const std::map<std::string, ParsedStatContents>&
        logical_ids_to_stat_contents,
    const base::FilePath& root_dir,
    mojo_ipc::CpuArchitectureEnum architecture) {
  std::map<std::string, mojo_ipc::PhysicalCpuInfoPtr> physical_cpus;
  for (const auto& processor : processor_info) {
    if (!IsProcessorBlock(processor))
      continue;

    std::string processor_id;
    std::string physical_id;
    std::string model_name;
    if (!ParseProcessor(processor, &processor_id, &physical_id, &model_name)) {
      return mojo_ipc::CpuResult::NewError(CreateAndLogProbeError(
          mojo_ipc::ErrorType::kParseError,
          "Unable to parse processor string: " + processor));
    }

    // Find the physical CPU corresponding to this logical CPU, if it already
    // exists. If not, make one.
    auto itr = physical_cpus.find(physical_id);
    if (itr == physical_cpus.end()) {
      mojo_ipc::PhysicalCpuInfo physical_cpu;
      if (!model_name.empty())
        physical_cpu.model_name = std::move(model_name);
      const auto result =
          physical_cpus.insert({physical_id, physical_cpu.Clone()});
      DCHECK(result.second);
      itr = result.first;
    }

    // Populate the logical CPU info values.
    mojo_ipc::LogicalCpuInfo logical_cpu;
    const auto parsed_stat_itr =
        logical_ids_to_stat_contents.find(processor_id);
    if (parsed_stat_itr == logical_ids_to_stat_contents.end()) {
      return mojo_ipc::CpuResult::NewError(CreateAndLogProbeError(
          mojo_ipc::ErrorType::kParseError,
          "No parsed stat contents for logical ID: " + processor_id));
    }
    logical_cpu.user_time_user_hz = parsed_stat_itr->second.user_time_user_hz;
    logical_cpu.system_time_user_hz =
        parsed_stat_itr->second.system_time_user_hz;
    logical_cpu.idle_time_user_hz = parsed_stat_itr->second.idle_time_user_hz;

    auto c_states = GetCStates(root_dir, processor_id);
    if (c_states == base::nullopt) {
      return mojo_ipc::CpuResult::NewError(CreateAndLogProbeError(
          mojo_ipc::ErrorType::kFileReadError, "Unable to read C States."));
    }
    logical_cpu.c_states = std::move(c_states.value());

    auto cpufreq_dir = GetCpuFreqDirectoryPath(root_dir, processor_id);
    if (!ReadInteger(cpufreq_dir, kCpuinfoMaxFreqFile, &base::StringToUint,
                     &logical_cpu.max_clock_speed_khz)) {
      return mojo_ipc::CpuResult::NewError(CreateAndLogProbeError(
          mojo_ipc::ErrorType::kFileReadError,
          "Unable to read max CPU frequency file to integer: " +
              cpufreq_dir.Append(kCpuinfoMaxFreqFile).value()));
    }

    if (!ReadInteger(cpufreq_dir, kCpuScalingMaxFreqFile, &base::StringToUint,
                     &logical_cpu.scaling_max_frequency_khz)) {
      return mojo_ipc::CpuResult::NewError(CreateAndLogProbeError(
          mojo_ipc::ErrorType::kFileReadError,
          "Unable to read scaling max frequency file to integer: " +
              cpufreq_dir.Append(kCpuScalingMaxFreqFile).value()));
    }

    if (!ReadInteger(cpufreq_dir, kCpuScalingCurFreqFile, &base::StringToUint,
                     &logical_cpu.scaling_current_frequency_khz)) {
      return mojo_ipc::CpuResult::NewError(CreateAndLogProbeError(
          mojo_ipc::ErrorType::kFileReadError,
          "Unable to read scaling current frequency file to integer: " +
              cpufreq_dir.Append(kCpuScalingCurFreqFile).value()));
    }

    // Add this logical CPU to the corresponding physical CPU.
    itr->second->logical_cpus.push_back(logical_cpu.Clone());
  }

  // Populate the final CpuInfo struct.
  mojo_ipc::CpuInfo cpu_info;
  auto thread_error = GetNumTotalThreads(root_dir, &cpu_info.num_total_threads);
  if (thread_error != base::nullopt) {
    return mojo_ipc::CpuResult::NewError(std::move(thread_error.value()));
  }

  cpu_info.architecture = architecture;

  for (const auto& temperature : GetCpuTemperatures(root_dir))
    cpu_info.temperature_channels.push_back(temperature.Clone());

  for (const auto& key_value : physical_cpus) {
    // TODO(crbug/1143763): Change this to Clone() |key_value|.
    cpu_info.physical_cpus.push_back(mojo_ipc::PhysicalCpuInfo::New(
        std::move(key_value.second->model_name),
        std::move(key_value.second->logical_cpus)));
  }

  return mojo_ipc::CpuResult::NewCpuInfo(cpu_info.Clone());
}

}  // namespace

CpuFetcher::CpuFetcher(Context* context) : context_(context) {
  DCHECK(context_);
}

CpuFetcher::~CpuFetcher() = default;

mojo_ipc::CpuResultPtr CpuFetcher::FetchCpuInfo(
    const base::FilePath& root_dir) {
  std::string stat_contents;
  auto stat_file = GetProcStatPath(root_dir);
  if (!ReadFileToString(stat_file, &stat_contents)) {
    return mojo_ipc::CpuResult::NewError(CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read stat file: " + stat_file.value()));
  }

  const auto parsed_stat_contents = ParseStatContents(stat_contents);
  if (!parsed_stat_contents.has_value()) {
    return mojo_ipc::CpuResult::NewError(CreateAndLogProbeError(
        mojo_ipc::ErrorType::kParseError,
        "Unable to parse stat contents: " + stat_contents));
  }

  std::string cpu_info_contents;
  auto cpu_info_file = GetProcCpuInfoPath(root_dir);
  if (!ReadFileToString(cpu_info_file, &cpu_info_contents)) {
    return mojo_ipc::CpuResult::NewError(CreateAndLogProbeError(
        mojo_ipc::ErrorType::kFileReadError,
        "Unable to read CPU info file: " + cpu_info_file.value()));
  }

  std::vector<std::string> processor_info = base::SplitStringUsingSubstr(
      cpu_info_contents, "\n\n", base::KEEP_WHITESPACE,
      base::SPLIT_WANT_NONEMPTY);
  return GetCpuInfoFromProcessorInfo(processor_info,
                                     parsed_stat_contents.value(), root_dir,
                                     GetArchitecture());
}

mojo_ipc::CpuArchitectureEnum CpuFetcher::GetArchitecture() {
  struct utsname buf;
  if (context_->system_utils()->Uname(&buf))
    return mojo_ipc::CpuArchitectureEnum::kUnknown;

  std::stringstream ss;
  ss << buf.machine;
  std::string machine = ss.str();
  if (machine == kUnameMachineX86_64)
    return mojo_ipc::CpuArchitectureEnum::kX86_64;
  else if (machine == kUnameMachineAArch64)
    return mojo_ipc::CpuArchitectureEnum::kAArch64;
  else if (machine == kUnameMachineArmv7l)
    return mojo_ipc::CpuArchitectureEnum::kArmv7l;

  return mojo_ipc::CpuArchitectureEnum::kUnknown;
}

}  // namespace diagnostics
