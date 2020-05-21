// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/format_manager.h"

#include <string>
#include <vector>

#include <base/files/file.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/stl_util.h>
#include <base/strings/stringprintf.h>
#include <brillo/process/process.h>

#include "cros-disks/filesystem_label.h"
#include "cros-disks/format_manager_observer_interface.h"
#include "cros-disks/quote.h"

namespace cros_disks {

namespace {

struct FormatOptions {
  std::string label;
};

// Expected locations of an external format program
const char* const kFormatProgramPaths[] = {
    "/usr/sbin/mkfs.",
    "/bin/mkfs.",
    "/sbin/mkfs.",
    "/usr/bin/mkfs.",
};

// Supported file systems
const char* const kSupportedFilesystems[] = {
    "vfat",
    "exfat",
    "ntfs",
};

const char kDefaultLabel[] = "UNTITLED";

FormatErrorType LabelErrorToFormatError(LabelErrorType error_code) {
  switch (error_code) {
    case LabelErrorType::kLabelErrorNone:
      return FORMAT_ERROR_NONE;
    case LabelErrorType::kLabelErrorUnsupportedFilesystem:
      return FORMAT_ERROR_UNSUPPORTED_FILESYSTEM;
    case LabelErrorType::kLabelErrorLongName:
      return FORMAT_ERROR_LONG_NAME;
    case LabelErrorType::kLabelErrorInvalidCharacter:
      return FORMAT_ERROR_INVALID_CHARACTER;
  }
}

// Turns a flat vector of key value pairs into a format options struct. Returns
// true if a valid options struct could be extracted from the vector.
bool ExtractFormatOptions(const std::vector<std::string>& options,
                          FormatOptions* format_options) {
  if (options.size() % 2 == 1) {
    LOG(WARNING) << "Number of options passed in (" << options.size()
                 << ") is not an even number";
    return false;
  }

  for (int i = 0; i < options.size(); i += 2) {
    if (options[i] == kFormatLabelOption) {
      format_options->label = options[i + 1];
    } else {
      LOG(WARNING) << "Unknown format option " << quote(options[i]);
      return false;
    }
  }

  if (format_options->label.empty()) {
    format_options->label = kDefaultLabel;
  }
  return true;
}

std::vector<std::string> CreateFormatArguments(const std::string& filesystem,
                                               const FormatOptions& options) {
  std::vector<std::string> arguments;
  if (filesystem == "vfat") {
    // Allow to create filesystem across the entire device.
    arguments.push_back("-I");
    // FAT type should be predefined, because mkfs autodetection is faulty.
    arguments.push_back("-F");
    arguments.push_back("32");
    arguments.push_back("-n");
    arguments.push_back(options.label);
  } else if (filesystem == "exfat") {
    arguments.push_back("-n");
    arguments.push_back(options.label);
  } else if (filesystem == "ntfs") {
    // --force is used to allow creating a filesystem on devices without a
    // partition table.
    arguments.push_back("--force");
    arguments.push_back("--quick");
    arguments.push_back("--label");
    arguments.push_back(options.label);
  }
  return arguments;
}

// Initialises the process for formatting and starts it.
FormatErrorType StartFormatProcess(const std::string& device_file,
                                   const std::string& format_program,
                                   const std::vector<std::string>& arguments,
                                   SandboxedProcess* process) {
  process->SetNoNewPrivileges();
  process->NewMountNamespace();
  process->NewIpcNamespace();
  process->NewNetworkNamespace();
  process->SetCapabilities(0);
  if (!process->EnterPivotRoot()) {
    LOG(WARNING) << "Could not enter pivot root";
    return FORMAT_ERROR_FORMAT_PROGRAM_FAILED;
  }
  if (!process->SetUpMinimalMounts()) {
    LOG(WARNING) << "Could not set up minimal mounts for jail";
    return FORMAT_ERROR_FORMAT_PROGRAM_FAILED;
  }

  // Open device_file so we can pass only the fd path to the format program.
  base::File dev_file(base::FilePath(device_file), base::File::FLAG_OPEN |
                                                       base::File::FLAG_READ |
                                                       base::File::FLAG_WRITE);
  if (!dev_file.IsValid()) {
    LOG(WARNING) << "Could not open " << device_file << " for formatting";
    return FORMAT_ERROR_FORMAT_PROGRAM_FAILED;
  }
  if (!process->PreserveFile(dev_file)) {
    LOG(WARNING) << "Could not preserve device fd";
    return FORMAT_ERROR_FORMAT_PROGRAM_FAILED;
  }
  process->CloseOpenFds();

  process->AddArgument(format_program);

  for (std::string arg : arguments) {
    process->AddArgument(arg);
  }

  process->AddArgument(
      base::StringPrintf("/dev/fd/%d", dev_file.GetPlatformFile()));
  if (!process->Start()) {
    LOG(WARNING) << "Cannot start process " << quote(format_program)
                 << " to format " << quote(device_file);
    return FORMAT_ERROR_FORMAT_PROGRAM_FAILED;
  }

  return FORMAT_ERROR_NONE;
}

}  // namespace

FormatManager::FormatManager(brillo::ProcessReaper* process_reaper)
    : process_reaper_(process_reaper), weak_ptr_factory_(this) {}

FormatManager::~FormatManager() = default;

FormatErrorType FormatManager::StartFormatting(
    const std::string& device_path,
    const std::string& device_file,
    const std::string& filesystem,
    const std::vector<std::string>& options) {
  // Check if the file system is supported for formatting
  if (!IsFilesystemSupported(filesystem)) {
    LOG(WARNING) << filesystem << " filesystem is not supported for formatting";
    return FORMAT_ERROR_UNSUPPORTED_FILESYSTEM;
  }

  // Localize mkfs on disk
  std::string format_program = GetFormatProgramPath(filesystem);
  if (format_program.empty()) {
    LOG(WARNING) << "Cannot find a format program for filesystem "
                 << quote(filesystem);
    return FORMAT_ERROR_FORMAT_PROGRAM_NOT_FOUND;
  }

  FormatOptions format_options;
  if (!ExtractFormatOptions(options, &format_options)) {
    return FORMAT_ERROR_INVALID_OPTIONS;
  }

  LabelErrorType label_error =
      ValidateVolumeLabel(format_options.label, filesystem);
  if (label_error != LabelErrorType::kLabelErrorNone) {
    return LabelErrorToFormatError(label_error);
  }

  if (base::ContainsKey(format_process_, device_path)) {
    LOG(WARNING) << "Device " << quote(device_path)
                 << " is already being formatted";
    return FORMAT_ERROR_DEVICE_BEING_FORMATTED;
  }

  SandboxedProcess* process = &format_process_[device_path];

  FormatErrorType error = StartFormatProcess(
      device_file, format_program,
      CreateFormatArguments(filesystem, format_options), process);
  if (error == FORMAT_ERROR_NONE) {
    process_reaper_->WatchForChild(
        FROM_HERE, process->pid(),
        base::BindOnce(&FormatManager::OnFormatProcessTerminated,
                       weak_ptr_factory_.GetWeakPtr(), device_path));
  } else {
    format_process_.erase(device_path);
  }
  return error;
}

void FormatManager::OnFormatProcessTerminated(const std::string& device_path,
                                              const siginfo_t& info) {
  format_process_.erase(device_path);
  FormatErrorType error_type = FORMAT_ERROR_UNKNOWN;
  switch (info.si_code) {
    case CLD_EXITED:
      if (info.si_status == 0) {
        error_type = FORMAT_ERROR_NONE;
        LOG(INFO) << "Process " << info.si_pid << " for formatting "
                  << quote(device_path) << " completed successfully";
      } else {
        error_type = FORMAT_ERROR_FORMAT_PROGRAM_FAILED;
        LOG(ERROR) << "Process " << info.si_pid << " for formatting "
                   << quote(device_path) << " exited with a status "
                   << info.si_status;
      }
      break;

    case CLD_DUMPED:
    case CLD_KILLED:
      error_type = FORMAT_ERROR_FORMAT_PROGRAM_FAILED;
      LOG(ERROR) << "Process " << info.si_pid << " for formatting "
                 << quote(device_path) << " killed by a signal "
                 << info.si_status;
      break;

    default:
      break;
  }
  if (observer_)
    observer_->OnFormatCompleted(device_path, error_type);
}

std::string FormatManager::GetFormatProgramPath(
    const std::string& filesystem) const {
  for (const char* program_path : kFormatProgramPaths) {
    std::string path = program_path + filesystem;
    if (base::PathExists(base::FilePath(path)))
      return path;
  }
  return std::string();
}

bool FormatManager::IsFilesystemSupported(const std::string& filesystem) const {
  for (const char* supported_filesystem : kSupportedFilesystems) {
    if (filesystem == supported_filesystem)
      return true;
  }
  return false;
}

}  // namespace cros_disks
