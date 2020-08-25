// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/cros_healthd_tool.h"

#include <map>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/process/launch.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>

#include "debugd/src/error_utils.h"
#include "debugd/src/process_with_output.h"

namespace debugd {

namespace {

constexpr char kErrorPath[] = "org.chromium.debugd.CrosHealthdToolError";
constexpr char kSandboxDirPath[] = "/usr/share/policy/";
constexpr char kRunAs[] = "healthd_ec";
// The ectool i2cread command below follows the format:
// ectool i2cread [NUM_BITS] [PORT] [BATTERY_I2C_ADDRESS (addr8)] [OFFSET]
// Note that [NUM_BITS] can either be 8 or 16.
constexpr char kEctoolBinary[] = "/usr/sbin/ectool";
constexpr char kI2cReadCommand[] = "i2cread";
// The specification for smart battery can be found at:
// http://sbs-forum.org/specs/sbdat110.pdf. This states
// that both the temperature and manufacture_date commands
// use the "Read Word" SMBus Protocol, which is 16 bits.
constexpr char kNumBits[] = "16";
// The i2c address is well defined at:
// src/platform/ec/include/battery_smart.h
constexpr char kBatteryI2cAddress[] = "0x16";
// The only i2cread argument different across models is the port.
const std::map<std::string, std::string> kModelToPort = {
    {"sona", "2"},
    {"careena", "0"},
    {"dratini", "5"},
    {"dorp", "0"},
};
const std::map<std::string, std::string> kMetricNameToOffset = {
    {"temperature_smart", "0x08"},
    {"manufacture_date_smart", "0x1b"},
};
// The ectool command used to collect fan speed in RPM.
constexpr char kGetFanRpmCommand[] = "pwmgetfanrpm";

// Returns the ectool policy file corresponding to the provided
// |ectool_command|.
std::string GetEctoolPolicyFile(const std::string& ectool_command) {
  return base::StringPrintf("ectool_%s-seccomp.policy", ectool_command.c_str());
}

// Runs ectool with the provided |ectool_args| in a sandboxed process. Returns
// true on success.
bool RunEctoolWithArgs(brillo::ErrorPtr* error,
                       const base::FilePath& seccomp_policy_path,
                       const std::vector<std::string> ectool_args,
                       std::string* output) {
  if (!base::PathExists(seccomp_policy_path)) {
    DEBUGD_ADD_ERROR(error, kErrorPath,
                     "Sandbox info is missing for this architecture.");
    return false;
  }

  // Minijail setup for ectool.
  std::vector<std::string> parsed_args;
  parsed_args.push_back("-c");
  parsed_args.push_back("cap_sys_rawio=e");
  parsed_args.push_back("-b");
  parsed_args.push_back("/dev/cros_ec");

  ProcessWithOutput process;
  process.SandboxAs(kRunAs, kRunAs);
  process.SetSeccompFilterPolicyFile(seccomp_policy_path.MaybeAsASCII());
  process.InheritUsergroups();
  if (!process.Init(parsed_args)) {
    DEBUGD_ADD_ERROR(error, kErrorPath, "Process initialization failure.");
    return false;
  }

  process.AddArg(kEctoolBinary);
  for (const auto& arg : ectool_args)
    process.AddArg(arg);
  if (process.Run() != EXIT_SUCCESS) {
    DEBUGD_ADD_ERROR(error, kErrorPath, "Failed to run process.");
    return false;
  }

  if (!process.GetOutput(output)) {
    DEBUGD_ADD_ERROR(error, kErrorPath, "Failed to get output from process.");
    return false;
  }

  return true;
}

}  // namespace

bool CrosHealthdTool::CollectFanSpeed(brillo::ErrorPtr* error,
                                      std::string* output) {
  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath)
          .Append(GetEctoolPolicyFile(kGetFanRpmCommand));
  std::vector<std::string> ectool_args = {kGetFanRpmCommand};
  if (!RunEctoolWithArgs(error, seccomp_policy_path, ectool_args, output))
    return false;

  return true;
}

// Note that this is a short-term solution to retrieving battery metrics.
// A long term solution is being discussed at: crbug.com/1047277.
bool CrosHealthdTool::CollectSmartBatteryMetric(brillo::ErrorPtr* error,
                                                const std::string& metric_name,
                                                std::string* output) {
  std::string model_name;
  if (!base::GetAppOutputAndError({"cros_config", "/", "name"}, &model_name)) {
    DEBUGD_ADD_ERROR(error, kErrorPath,
                     base::StringPrintf("Failed to run cros_config: %s",
                                        model_name.c_str()));
    return false;
  }

  auto it = kModelToPort.find(model_name);
  if (it == kModelToPort.end()) {
    DEBUGD_ADD_ERROR(
        error, kErrorPath,
        base::StringPrintf("Failed to find port for model: %s and metric: %s",
                           model_name.c_str(), metric_name.c_str()));
    return false;
  }

  const std::string port_number = it->second;
  auto metric_name_it = kMetricNameToOffset.find(metric_name);
  if (metric_name_it == kMetricNameToOffset.end()) {
    DEBUGD_ADD_ERROR(
        error, kErrorPath,
        base::StringPrintf("Failed to find offset for model: %s and metric: %s",
                           model_name.c_str(), metric_name.c_str()));
    return false;
  }

  const std::string offset = metric_name_it->second;
  std::vector<std::string> ectool_args = {
      kI2cReadCommand, kNumBits, port_number, kBatteryI2cAddress, offset};
  const auto seccomp_policy_path =
      base::FilePath(kSandboxDirPath)
          .Append(GetEctoolPolicyFile(kI2cReadCommand));
  if (!RunEctoolWithArgs(error, seccomp_policy_path, ectool_args, output))
    return false;

  return true;
}

}  // namespace debugd
