// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/telem/telem.h"

#include <sys/types.h>

#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <base/at_exit.h>
#include <base/logging.h>
#include <base/optional.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/task/single_thread_task_executor.h>
#include <brillo/flag_helper.h>
#include <brillo/syslog_logging.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter.h"
#include "mojo/cros_healthd_probe.mojom.h"
#include "mojo/network_health.mojom.h"
#include "mojo/network_types.mojom.h"

namespace diagnostics {

namespace {

using chromeos::cros_healthd::mojom::CpuArchitectureEnum;
using chromeos::network_config::mojom::NetworkType;
using chromeos::network_health::mojom::NetworkState;

// Value printed for optional fields when they aren't populated.
constexpr char kNotApplicableString[] = "N/A";

constexpr std::pair<const char*,
                    chromeos::cros_healthd::mojom::ProbeCategoryEnum>
    kCategorySwitches[] = {
        {"battery", chromeos::cros_healthd::mojom::ProbeCategoryEnum::kBattery},
        {"storage", chromeos::cros_healthd::mojom::ProbeCategoryEnum::
                        kNonRemovableBlockDevices},
        {"cpu", chromeos::cros_healthd::mojom::ProbeCategoryEnum::kCpu},
        {"timezone",
         chromeos::cros_healthd::mojom::ProbeCategoryEnum::kTimezone},
        {"memory", chromeos::cros_healthd::mojom::ProbeCategoryEnum::kMemory},
        {"backlight",
         chromeos::cros_healthd::mojom::ProbeCategoryEnum::kBacklight},
        {"fan", chromeos::cros_healthd::mojom::ProbeCategoryEnum::kFan},
        {"stateful_partition",
         chromeos::cros_healthd::mojom::ProbeCategoryEnum::kStatefulPartition},
        {"bluetooth",
         chromeos::cros_healthd::mojom::ProbeCategoryEnum::kBluetooth},
        {"system", chromeos::cros_healthd::mojom::ProbeCategoryEnum::kSystem},
        {"network", chromeos::cros_healthd::mojom::ProbeCategoryEnum::kNetwork},
};

std::string ProcessStateToString(
    chromeos::cros_healthd::mojom::ProcessState state) {
  switch (state) {
    case chromeos::cros_healthd::mojom::ProcessState::kRunning:
      return "Running";
    case chromeos::cros_healthd::mojom::ProcessState::kSleeping:
      return "Sleeping";
    case chromeos::cros_healthd::mojom::ProcessState::kWaiting:
      return "Waiting";
    case chromeos::cros_healthd::mojom::ProcessState::kZombie:
      return "Zombie";
    case chromeos::cros_healthd::mojom::ProcessState::kStopped:
      return "Stopped";
    case chromeos::cros_healthd::mojom::ProcessState::kTracingStop:
      return "Tracing Stop";
    case chromeos::cros_healthd::mojom::ProcessState::kDead:
      return "Dead";
  }
}

std::string ErrorTypeToString(chromeos::cros_healthd::mojom::ErrorType type) {
  switch (type) {
    case chromeos::cros_healthd::mojom::ErrorType::kFileReadError:
      return "File Read Error";
    case chromeos::cros_healthd::mojom::ErrorType::kParseError:
      return "Parse Error";
    case chromeos::cros_healthd::mojom::ErrorType::kSystemUtilityError:
      return "Error running system utility";
    case chromeos::cros_healthd::mojom::ErrorType::kServiceUnavailable:
      return "External service not aviailable";
  }
}

void DisplayError(const chromeos::cros_healthd::mojom::ProbeErrorPtr& error) {
  std::cout << ErrorTypeToString(error->type) << ": " << error->msg
            << std::endl;
}

std::string GetArchitectureString(CpuArchitectureEnum architecture) {
  switch (architecture) {
    case CpuArchitectureEnum::kUnknown:
      return "unknown";
    case CpuArchitectureEnum::kX86_64:
      return "x86_64";
    case CpuArchitectureEnum::kAArch64:
      return "aarch64";
    case CpuArchitectureEnum::kArmv7l:
      return "armv7l";
  }
}

std::string NetworkTypeToString(NetworkType type) {
  switch (type) {
    case NetworkType::kAll:
      return "Unknown";
    case NetworkType::kCellular:
      return "Cellular";
    case NetworkType::kEthernet:
      return "Ethernet";
    case NetworkType::kMobile:
      return "Mobile";
    case NetworkType::kTether:
      return "Tether";
    case NetworkType::kVPN:
      return "VPN";
    case NetworkType::kWireless:
      return "Wireless";
    case NetworkType::kWiFi:
      return "WiFi";
  }
}

std::string NetworkStateToString(NetworkState state) {
  switch (state) {
    case NetworkState::kUninitialized:
      return "Uninitialized";
    case NetworkState::kDisabled:
      return "Disabled";
    case NetworkState::kProhibited:
      return "Prohibited";
    case NetworkState::kNotConnected:
      return "Not Connected";
    case NetworkState::kConnecting:
      return "Connecting";
    case NetworkState::kPortal:
      return "Portal";
    case NetworkState::kConnected:
      return "Connected";
    case NetworkState::kOnline:
      return "Online";
  }
}

void DisplayProcessInfo(
    const chromeos::cros_healthd::mojom::ProcessResultPtr& process_result) {
  if (process_result.is_null())
    return;

  if (process_result->is_error()) {
    DisplayError(process_result->get_error());
    return;
  }

  const auto& process = process_result->get_process_info();

  std::cout << "command,user_id,priority,nice,uptime_ticks,state,total_memory_"
               "kib,resident_memory_kib,free_memory_kib"
            << std::endl;

  // The int8_t fields need to be cast to a larger int type, otherwise they will
  // be treated as chars and display garbage. Also, wrap the command in quotes,
  // because the command-line options included in the command sometimes have
  // their own commas.
  std::cout << "\"" << process->command << "\"," << process->user_id << ","
            << static_cast<int>(process->priority) << ","
            << static_cast<int>(process->nice) << "," << process->uptime_ticks
            << "," << ProcessStateToString(process->state) << ","
            << process->total_memory_kib << "," << process->resident_memory_kib
            << "," << process->free_memory_kib << std::endl;
}

void DisplayBatteryInfo(
    const chromeos::cros_healthd::mojom::BatteryResultPtr& battery_result) {
  if (battery_result->is_error()) {
    DisplayError(battery_result->get_error());
    return;
  }

  const auto& battery = battery_result->get_battery_info();
  if (battery.is_null()) {
    std::cout << "Device does not have battery" << std::endl;
    return;
  }

  std::cout << "charge_full,charge_full_design,cycle_count,serial_number,"
               "vendor(manufacturer),voltage_now,voltage_min_design,"
               "manufacture_date_smart,temperature_smart,model_name,charge_now,"
               "current_now,technology,status"
            << std::endl;

  std::string manufacture_date_smart =
      battery->manufacture_date.value_or(kNotApplicableString);
  std::string temperature_smart =
      !battery->temperature.is_null()
          ? std::to_string(battery->temperature->value)
          : kNotApplicableString;

  std::cout << battery->charge_full << "," << battery->charge_full_design << ","
            << battery->cycle_count << "," << battery->serial_number << ","
            << battery->vendor << "," << battery->voltage_now << ","
            << battery->voltage_min_design << "," << manufacture_date_smart
            << "," << temperature_smart << "," << battery->model_name << ","
            << battery->charge_now << "," << battery->current_now << ","
            << battery->technology << "," << battery->status << std::endl;
}

void DisplayBlockDeviceInfo(
    const chromeos::cros_healthd::mojom::NonRemovableBlockDeviceResultPtr&
        block_device_result) {
  if (block_device_result->is_error()) {
    DisplayError(block_device_result->get_error());
    return;
  }

  const auto& block_devices = block_device_result->get_block_device_info();
  std::cout << "path,size,type,manfid,name,serial,bytes_read_since_last_boot,"
               "bytes_written_since_last_boot,read_time_seconds_since_last_"
               "boot,write_time_seconds_since_last_boot,io_time_seconds_since_"
               "last_boot,discard_time_seconds_since_last_boot"
            << std::endl;
  for (const auto& device : block_devices) {
    std::string discard_time =
        !device->discard_time_seconds_since_last_boot.is_null()
            ? std::to_string(
                  device->discard_time_seconds_since_last_boot->value)
            : kNotApplicableString;
    std::cout << device->path << "," << device->size << "," << device->type
              << "," << device->manufacturer_id << "," << device->name << ","
              << device->serial << "," << device->bytes_read_since_last_boot
              << "," << device->bytes_written_since_last_boot << ","
              << device->read_time_seconds_since_last_boot << ","
              << device->write_time_seconds_since_last_boot << ","
              << device->io_time_seconds_since_last_boot << "," << discard_time
              << std::endl;
  }
}

void DisplayBluetoothInfo(
    const chromeos::cros_healthd::mojom::BluetoothResultPtr& bluetooth_result) {
  if (bluetooth_result->is_error()) {
    DisplayError(bluetooth_result->get_error());
    return;
  }

  const auto& adapters = bluetooth_result->get_bluetooth_adapter_info();
  std::cout << "name,address,powered,num_connected_devices" << std::endl;
  for (const auto& adapter : adapters) {
    std::cout << adapter->name << "," << adapter->address << ","
              << (adapter->powered ? "true" : "false") << ","
              << adapter->num_connected_devices << std::endl;
  }
}

void DisplayCpuInfo(
    const chromeos::cros_healthd::mojom::CpuResultPtr& cpu_result) {
  if (cpu_result->is_error()) {
    DisplayError(cpu_result->get_error());
    return;
  }

  // An example CpuInfo output containing a single physical CPU, which in turn
  // contains two logical CPUs, would look like the following:
  //
  // num_total_threads,architecture
  // some_uint32,some_string
  // Physical CPU:
  //     model_name
  //     some_string
  //     Logical CPU:
  //         max_clock_speed_khz,scaling_max_frequency_khz,... (six keys total)
  //         some_uint32,... (six values total)
  //         C-states:
  //             name,time_in_state_since_last_boot_us
  //             some_string,some_uint_64
  //             ... (repeated per C-state)
  //             some_string,some_uint_64
  //     Logical CPU:
  //         max_clock_speed_khz,scaling_max_frequency_khz,... (six keys total)
  //         some_uint32,... (six values total)
  //         C-states:
  //             name,time_in_state_since_last_boot_us
  //             some_string,some_uint_64
  //             ... (repeated per C-state)
  //             some_string,some_uint_64
  // Temperature Channels:
  // label, temperature_celsius
  // some_label, some_int32_t
  // some_other_label, some_other_int32_t
  //
  // Any additional physical CPUs would repeat, similarly to the two logical
  // CPUs in the example.
  const auto& cpu_info = cpu_result->get_cpu_info();
  std::cout << "num_total_threads,architecture" << std::endl;
  std::cout << cpu_info->num_total_threads << ","
            << GetArchitectureString(cpu_info->architecture) << std::endl;
  for (const auto& physical_cpu : cpu_info->physical_cpus) {
    std::cout << "Physical CPU:" << std::endl;
    std::cout << "\tmodel_name" << std::endl;
    // Remove commas from the model name before printing CSVs.
    std::string csv_model_name;
    base::RemoveChars(physical_cpu->model_name.value_or(kNotApplicableString),
                      ",", &csv_model_name);
    std::cout << "\t" << csv_model_name << std::endl;

    for (const auto& logical_cpu : physical_cpu->logical_cpus) {
      std::cout << "\tLogical CPU:" << std::endl;
      std::cout << "\t\tmax_clock_speed_khz,scaling_max_frequency_khz,scaling_"
                   "current_frequency_khz,user_time_user_hz,system_time_user_"
                   "hz,idle_time_user_hz"
                << std::endl;
      std::cout << "\t\t" << logical_cpu->max_clock_speed_khz << ","
                << logical_cpu->scaling_max_frequency_khz << ","
                << logical_cpu->scaling_current_frequency_khz << ","
                << logical_cpu->user_time_user_hz << ","
                << logical_cpu->system_time_user_hz << ","
                << logical_cpu->idle_time_user_hz << std::endl;

      std::cout << "\t\tC-states:" << std::endl;
      std::cout << "\t\t\tname,time_in_state_since_last_boot_us" << std::endl;
      for (const auto& c_state : logical_cpu->c_states) {
        std::cout << "\t\t\t" << c_state->name << ","
                  << c_state->time_in_state_since_last_boot_us << std::endl;
      }
    }
  }
  std::cout << "Temperature Channels:" << std::endl;
  std::cout << "label,temperature_celsius" << std::endl;
  for (const auto& channel : cpu_info->temperature_channels) {
    std::cout << channel->label.value_or(kNotApplicableString) << ","
              << channel->temperature_celsius << std::endl;
  }
}

void DisplayFanInfo(
    const chromeos::cros_healthd::mojom::FanResultPtr& fan_result) {
  if (fan_result->is_error()) {
    DisplayError(fan_result->get_error());
    return;
  }

  const auto& fans = fan_result->get_fan_info();
  std::cout << "speed_rpm" << std::endl;
  for (const auto& fan : fans) {
    std::cout << fan->speed_rpm << std::endl;
  }
}

void DisplayNetworkInfo(
    const chromeos::cros_healthd::mojom::NetworkResultPtr& network_result) {
  if (network_result->is_error()) {
    DisplayError(network_result->get_error());
    return;
  }

  const auto& network_health = network_result->get_network_health();
  std::cout << "type,state,guid,name,signal_strength,mac_address" << std::endl;
  for (const auto& network : network_health->networks) {
    auto signal_strength = network->signal_strength
                               ? std::to_string(network->signal_strength->value)
                               : kNotApplicableString;
    std::cout << NetworkTypeToString(network->type) << ","
              << NetworkStateToString(network->state) << ","
              << network->guid.value_or(kNotApplicableString) << ","
              << network->name.value_or(kNotApplicableString) << ","
              << signal_strength << ","
              << network->mac_address.value_or(kNotApplicableString)
              << std::endl;
  }
}

void DisplayTimezoneInfo(
    const chromeos::cros_healthd::mojom::TimezoneResultPtr& timezone_result) {
  if (timezone_result->is_error()) {
    DisplayError(timezone_result->get_error());
    return;
  }

  const auto& timezone = timezone_result->get_timezone_info();
  // Replace commas in POSIX timezone before printing CSVs.
  std::string csv_posix_timezone;
  base::ReplaceChars(timezone->posix, ",", " ", &csv_posix_timezone);
  std::cout << "posix_timezone,timezone_region" << std::endl;
  std::cout << csv_posix_timezone << "," << timezone->region << std::endl;
}

void DisplayMemoryInfo(
    const chromeos::cros_healthd::mojom::MemoryResultPtr& memory_result) {
  if (memory_result->is_error()) {
    DisplayError(memory_result->get_error());
    return;
  }

  const auto& memory = memory_result->get_memory_info();
  std::cout << "total_memory_kib,free_memory_kib,available_memory_kib,"
               "page_faults_since_last_boot"
            << std::endl;
  std::cout << memory->total_memory_kib << "," << memory->free_memory_kib << ","
            << memory->available_memory_kib << ","
            << memory->page_faults_since_last_boot << std::endl;
}

void DisplayBacklightInfo(
    const chromeos::cros_healthd::mojom::BacklightResultPtr& backlight_result) {
  if (backlight_result->is_error()) {
    DisplayError(backlight_result->get_error());
    return;
  }

  const auto& backlights = backlight_result->get_backlight_info();
  std::cout << "path,max_brightness,brightness" << std::endl;
  for (const auto& backlight : backlights) {
    std::cout << backlight->path.c_str() << "," << backlight->max_brightness
              << "," << backlight->brightness << std::endl;
  }
}

void DisplayStatefulPartitionInfo(
    const chromeos::cros_healthd::mojom::StatefulPartitionResultPtr&
        stateful_partition_result) {
  if (stateful_partition_result->is_error()) {
    DisplayError(stateful_partition_result->get_error());
    return;
  }

  const auto& stateful_partition_info =
      stateful_partition_result->get_partition_info();
  std::cout << "available_space,total_space" << std::endl;
  std::cout << stateful_partition_info->available_space << ","
            << stateful_partition_info->total_space << std::endl;
}

void DisplaySystemInfo(
    const chromeos::cros_healthd::mojom::SystemResultPtr& system_result) {
  if (system_result->is_error()) {
    DisplayError(system_result->get_error());
    return;
  }

  const auto& system_info = system_result->get_system_info();
  std::cout << "first_power_date,manufacture_date,product_sku_number,"
            << "product_serial_number,marketing_name,bios_version,board_name,"
            << "board_version,chassis_type,product_name,os_version,os_channel"
            << std::endl;
  std::string chassis_type =
      !system_info->chassis_type.is_null()
          ? std::to_string(system_info->chassis_type->value)
          : kNotApplicableString;
  std::string os_version =
      base::JoinString({system_info->os_version->release_milestone,
                        system_info->os_version->build_number,
                        system_info->os_version->patch_number},
                       ".");

  // The marketing name sometimes has a comma, for example:
  // "Acer Chromebook Spin 11 (CP311-H1, CP311-1HN)"
  // This messes up the tast logic, which splits on commas. To fix it, we
  // replace any ", " patterns found with "/".
  std::string marketing_name = system_info->marketing_name;
  base::ReplaceSubstringsAfterOffset(&marketing_name, 0, ", ", "/");

  std::cout << system_info->first_power_date.value_or(kNotApplicableString)
            << ","
            << system_info->manufacture_date.value_or(kNotApplicableString)
            << ","
            << system_info->product_sku_number.value_or(kNotApplicableString)
            << ","
            << system_info->product_serial_number.value_or(kNotApplicableString)
            << "," << marketing_name << ","
            << system_info->bios_version.value_or(kNotApplicableString) << ","
            << system_info->board_name.value_or(kNotApplicableString) << ","
            << system_info->board_version.value_or(kNotApplicableString) << ","
            << chassis_type << ","
            << system_info->product_name.value_or(kNotApplicableString) << ","
            << os_version << ',' << system_info->os_version->release_channel
            << std::endl;
}

// Displays the retrieved telemetry information to the console.
void DisplayTelemetryInfo(
    const chromeos::cros_healthd::mojom::TelemetryInfoPtr& info) {
  const auto& battery_result = info->battery_result;
  if (battery_result)
    DisplayBatteryInfo(battery_result);

  const auto& block_device_result = info->block_device_result;
  if (block_device_result)
    DisplayBlockDeviceInfo(block_device_result);

  const auto& cpu_result = info->cpu_result;
  if (cpu_result)
    DisplayCpuInfo(cpu_result);

  const auto& timezone_result = info->timezone_result;
  if (timezone_result)
    DisplayTimezoneInfo(timezone_result);

  const auto& memory_result = info->memory_result;
  if (memory_result)
    DisplayMemoryInfo(memory_result);

  const auto& backlight_result = info->backlight_result;
  if (backlight_result)
    DisplayBacklightInfo(backlight_result);

  const auto& fan_result = info->fan_result;
  if (fan_result)
    DisplayFanInfo(fan_result);

  const auto& stateful_partition_result = info->stateful_partition_result;
  if (stateful_partition_result)
    DisplayStatefulPartitionInfo(stateful_partition_result);

  const auto& bluetooth_result = info->bluetooth_result;
  if (bluetooth_result)
    DisplayBluetoothInfo(bluetooth_result);

  const auto& system_result = info->system_result;
  if (system_result)
    DisplaySystemInfo(system_result);

  const auto& network_result = info->network_result;
  if (network_result)
    DisplayNetworkInfo(network_result);
}

// Create a stringified list of the category names for use in help.
std::string GetCategoryHelp() {
  std::stringstream ss;
  ss << "Category or categories to probe, as comma-separated list: [";
  const char* sep = "";
  for (auto pair : kCategorySwitches) {
    ss << sep << pair.first;
    sep = ", ";
  }
  ss << "]";
  return ss.str();
}

}  // namespace

// 'telem' sub-command for cros-health-tool:
//
// Test driver for cros_healthd's telemetry collection. Supports requesting a
// comma-separate list of categories and/or a single process at a time.
int telem_main(int argc, char** argv) {
  std::string category_help = GetCategoryHelp();
  DEFINE_string(category, "", category_help.c_str());
  DEFINE_uint32(process, 0, "Process ID to probe.");
  brillo::FlagHelper::Init(argc, argv, "telem - Device telemetry tool.");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  base::AtExitManager at_exit_manager;

  std::map<std::string, chromeos::cros_healthd::mojom::ProbeCategoryEnum>
      switch_to_category(std::begin(kCategorySwitches),
                         std::end(kCategorySwitches));

  logging::InitLogging(logging::LoggingSettings());

  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);

  std::unique_ptr<CrosHealthdMojoAdapter> adapter =
      CrosHealthdMojoAdapter::Create();

  // Make sure at least one flag is specified.
  if (FLAGS_category == "" && FLAGS_process == 0) {
    LOG(ERROR) << "No category or process specified.";
    return EXIT_FAILURE;
  }

  // Probe a process, if requested.
  if (FLAGS_process != 0) {
    DisplayProcessInfo(
        adapter->GetProcessInfo(static_cast<pid_t>(FLAGS_process)));
  }

  // Probe category info, if requested.
  if (FLAGS_category != "") {
    // Validate the category flag.
    std::vector<chromeos::cros_healthd::mojom::ProbeCategoryEnum>
        categories_to_probe;
    std::vector<std::string> input_categories = base::SplitString(
        FLAGS_category, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    for (const auto& category : input_categories) {
      auto iterator = switch_to_category.find(category);
      if (iterator == switch_to_category.end()) {
        LOG(ERROR) << "Invalid category: " << category;
        return EXIT_FAILURE;
      }
      categories_to_probe.push_back(iterator->second);
    }

    // Probe and display the category or categories.
    DisplayTelemetryInfo(adapter->GetTelemetryInfo(categories_to_probe));
  }

  return EXIT_SUCCESS;
}

}  // namespace diagnostics
