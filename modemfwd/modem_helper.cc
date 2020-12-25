// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "modemfwd/modem_helper.h"

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/macros.h>
#include <base/memory/ptr_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/process/process.h>
#include <chromeos/switches/modemfwd_switches.h>

namespace modemfwd {

namespace {

// This lock file prevents powerd from suspending the system. Take it
// while we are attempting to flash the modem.
constexpr char kPowerOverrideLockFilePath[] =
    "/run/lock/power_override/modemfwd.lock";

constexpr char kModemfwdLogDirectory[] = "/var/log/modemfwd";

bool RunHelperProcessWithLogs(const HelperInfo& helper_info,
                              const std::vector<std::string>& arguments) {
  brillo::ProcessImpl helper;
  helper.AddArg(helper_info.executable_path.value());
  for (const std::string& argument : arguments)
    helper.AddArg("--" + argument);
  for (const std::string& extra_argument : helper_info.extra_arguments)
    helper.AddArg(extra_argument);

  base::Time::Exploded time;
  base::Time::Now().LocalExplode(&time);
  const std::string output_log_file = base::StringPrintf(
      "%s/helper_log.%4u%02u%02u-%02u%02u%02u%03u", kModemfwdLogDirectory,
      time.year, time.month, time.day_of_month, time.hour, time.minute,
      time.second, time.millisecond);
  helper.RedirectOutput(output_log_file);

  int exit_code = helper.Run();
  if (exit_code != 0) {
    LOG(ERROR) << "Failed to perform \"" << base::JoinString(arguments, ",")
               << "\" on the modem";
    return false;
  }

  return true;
}

bool RunHelperProcess(const HelperInfo& helper_info,
                      const std::vector<std::string>& arguments,
                      std::string* output) {
  brillo::ProcessImpl helper;
  helper.AddArg(helper_info.executable_path.value());
  for (const std::string& argument : arguments)
    helper.AddArg("--" + argument);
  for (const std::string& extra_argument : helper_info.extra_arguments)
    helper.AddArg(extra_argument);

  // Set up output redirection, if requested. We keep the file open
  // across the process lifetime to ensure nobody is swapping out the
  // file from underneath us while the helper is running.
  base::File output_base_file;
  if (output) {
    base::FilePath output_path;
    FILE* output_file =
        base::CreateAndOpenTemporaryStream(&output_path).release();
    if (output_file == nullptr) {
      LOG(ERROR) << "Failed to create tempfile for helper process output";
      return false;
    }
    output_base_file = base::File(fileno(output_file));
    DCHECK(output_base_file.IsValid());

    helper.RedirectOutput(output_path.value());
  }

  int exit_code = helper.Run();

  // Collect output if requested. Note that we only collect 1024 bytes of
  // output here. We could read everything, but the API is simple enough
  // that we shouldn't need more than this (at least for the time being).
  if (output && output_base_file.IsValid()) {
    const int kBufSize = 1024;
    char buf[kBufSize];
    int bytes_read = output_base_file.ReadAtCurrentPos(buf, kBufSize);
    if (bytes_read != -1)
      output->assign(buf, bytes_read);
  }

  if (exit_code != 0) {
    LOG(ERROR) << "Failed to perform \"" << base::JoinString(arguments, ",")
               << "\" on the modem";
    return false;
  }

  return true;
}

// Ensures we reboot the modem to prevent us from leaving it in a bad state.
// Also takes a power override lock so we don't suspend while we're in the
// middle of flashing and ensures it's cleaned up later.
class FlashMode {
 public:
  static std::unique_ptr<FlashMode> Create(const HelperInfo& helper_info) {
    const base::FilePath lock_path(kPowerOverrideLockFilePath);
    // If the lock directory doesn't exist, then powerd is probably not running.
    // Don't worry about it in that case.
    if (base::DirectoryExists(lock_path.DirName())) {
      base::File lock_file(lock_path,
                           base::File::FLAG_CREATE | base::File::FLAG_WRITE);
      if (lock_file.IsValid()) {
        std::string lock_contents = base::StringPrintf("%d", getpid());
        lock_file.WriteAtCurrentPos(lock_contents.data(), lock_contents.size());
      }
    }

    if (!RunHelperProcess(helper_info, {kPrepareToFlash}, nullptr)) {
      base::DeleteFile(lock_path);
      return nullptr;
    }

    return base::WrapUnique(new FlashMode(helper_info));
  }

  ~FlashMode() {
    RunHelperProcess(helper_info_, {kReboot}, nullptr);
    base::DeleteFile(base::FilePath(kPowerOverrideLockFilePath));
  }

 private:
  // Use the static factory method above.
  explicit FlashMode(const HelperInfo& helper_info)
      : helper_info_(helper_info) {}
  FlashMode(const FlashMode&) = delete;
  FlashMode& operator=(const FlashMode&) = delete;

  HelperInfo helper_info_;
};

}  // namespace

class ModemHelperImpl : public ModemHelper {
 public:
  explicit ModemHelperImpl(const HelperInfo& helper_info)
      : helper_info_(helper_info) {}
  ModemHelperImpl(const ModemHelperImpl&) = delete;
  ModemHelperImpl& operator=(const ModemHelperImpl&) = delete;

  ~ModemHelperImpl() override = default;

  bool GetFirmwareInfo(FirmwareInfo* out_info) override {
    CHECK(out_info);

    std::string helper_output;
    if (!RunHelperProcess(helper_info_, {kGetFirmwareInfo}, &helper_output))
      return false;

    std::vector<std::string> parsed_output = base::SplitString(
        helper_output, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
    if (parsed_output.size() != 3) {
      LOG(WARNING) << "Modem helper returned malformed firmware version info";
      return false;
    }

    out_info->main_version = parsed_output[0];
    out_info->carrier_uuid = parsed_output[1];
    out_info->carrier_version = parsed_output[2];
    return true;
  }

  // modemfwd::ModemHelper overrides.
  bool FlashMainFirmware(const base::FilePath& path_to_fw,
                         const std::string& version) override {
    auto flash_mode = FlashMode::Create(helper_info_);
    if (!flash_mode)
      return false;

    return RunHelperProcessWithLogs(
        helper_info_,
        {base::StringPrintf("%s=%s", kFlashMainFirmware,
                            path_to_fw.value().c_str()),
         base::StringPrintf("%s=%s", kFwVersion, version.c_str())});
  }

  bool FlashCarrierFirmware(const base::FilePath& path_to_fw,
                            const std::string& version) override {
    auto flash_mode = FlashMode::Create(helper_info_);
    if (!flash_mode)
      return false;

    return RunHelperProcessWithLogs(
        helper_info_,
        {base::StringPrintf("%s=%s", kFlashCarrierFirmware,
                            path_to_fw.value().c_str()),
         base::StringPrintf("%s=%s", kFwVersion, version.c_str())});
  }

  bool FlashModeCheck() override {
    std::string output;
    if (!RunHelperProcess(helper_info_, {kFlashModeCheck}, &output))
      return false;

    return base::TrimWhitespaceASCII(output, base::TRIM_ALL) == "true";
  }

  bool Reboot() override {
    return RunHelperProcess(helper_info_, {kReboot}, nullptr);
  }

  bool ClearAttachAPN(const std::string& carrier_uuid) override {
    return RunHelperProcess(
        helper_info_,
        {base::StringPrintf("%s=%s", kClearAttachAPN, carrier_uuid.c_str())},
        nullptr);
  }

 private:
  HelperInfo helper_info_;
};

std::unique_ptr<ModemHelper> CreateModemHelper(const HelperInfo& helper_info) {
  return std::make_unique<ModemHelperImpl>(helper_info);
}

}  // namespace modemfwd
