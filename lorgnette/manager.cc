// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/manager.h"

#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/process.h>
#include <brillo/type_name_undecorate.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>

#include "lorgnette/daemon.h"
#include "lorgnette/epson_probe.h"
#include "lorgnette/firewall_manager.h"

using base::ScopedFD;
using base::StringPrintf;
using std::map;
using std::string;
using std::vector;

namespace lorgnette {

// static
const char Manager::kScanConverterPath[] = "/usr/bin/pnm2png";
const char Manager::kScanImageFormattedDeviceListCmd[] =
    "--formatted-device-list=%d%%%v%%%m%%%t%n";
const char Manager::kScanImagePath[] = "/usr/bin/scanimage";
const int Manager::kTimeoutAfterKillSeconds = 1;
const char Manager::kMetricScanResult[] = "DocumentScan.ScanResult";
const char Manager::kMetricConverterResult[] = "DocumentScan.ConverterResult";

Manager::Manager(base::Callback<void()> activity_callback)
    : org::chromium::lorgnette::ManagerAdaptor(this),
      activity_callback_(activity_callback),
      metrics_library_(new MetricsLibrary) {}

Manager::~Manager() {}

void Manager::RegisterAsync(
    brillo::dbus_utils::ExportedObjectManager* object_manager,
    brillo::dbus_utils::AsyncEventSequencer* sequencer) {
  CHECK(!dbus_object_) << "Already registered";
  scoped_refptr<dbus::Bus> bus =
      object_manager ? object_manager->GetBus() : nullptr;
  dbus_object_.reset(new brillo::dbus_utils::DBusObject(
        object_manager, bus, dbus::ObjectPath(kManagerServicePath)));
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(
      sequencer->GetHandler("Manager.RegisterAsync() failed.", true));
  firewall_manager_.reset(new FirewallManager(""));
  firewall_manager_->Init(bus);
}

bool Manager::ListScanners(brillo::ErrorPtr* error,
                           Manager::ScannerInfo* scanner_list) {
  base::FilePath output_path;
  FILE* output_file_handle;
  output_file_handle = base::CreateAndOpenTemporaryFile(&output_path);
  if (!output_file_handle) {
    brillo::Error::AddTo(error, FROM_HERE,
                           brillo::errors::dbus::kDomain,
                           kManagerServiceError,
                           "Unable to create temporary file.");
    return false;
  }

  brillo::ProcessImpl process;
  firewall_manager_->RequestScannerPortAccess();
  RunListScannersProcess(fileno(output_file_handle), &process);
  fclose(output_file_handle);
  string scanner_output_string;
  bool read_status = base::ReadFileToString(output_path,
                                            &scanner_output_string);
  const bool recursive_delete = false;
  base::DeleteFile(output_path, recursive_delete);
  if (!read_status) {
    brillo::Error::AddTo(error, FROM_HERE,
                           brillo::errors::dbus::kDomain,
                           kManagerServiceError,
                           "Unable to read scanner list output file");
    return false;
  }
  activity_callback_.Run();
  *scanner_list = ScannerInfoFromString(scanner_output_string);
  epson_probe::ProbeForScanners(firewall_manager_.get(), scanner_list);

  firewall_manager_->ReleaseAllPortsAccess();
  return true;
}

bool Manager::ScanImage(brillo::ErrorPtr* error,
                        const string& device_name,
                        const base::ScopedFD& outfd,
                        const brillo::VariantDictionary& scan_properties) {
  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    brillo::Error::AddTo(error, FROM_HERE,
                           brillo::errors::dbus::kDomain,
                           kManagerServiceError,
                           "Unable to create process pipe");
    return false;
  }

  ScopedFD pipe_fd_input(pipe_fds[0]);
  ScopedFD pipe_fd_output(pipe_fds[1]);
  brillo::ProcessImpl scan_process;
  brillo::ProcessImpl convert_process;

  // Duplicate |outfd| since we need a file descriptor that the brillo::Process
  // can close after binding to the child.
  RunScanImageProcess(device_name,
                      dup(outfd.get()),
                      &pipe_fd_input,
                      &pipe_fd_output,
                      scan_properties,
                      &scan_process,
                      &convert_process,
                      error);
  activity_callback_.Run();
  return true;
}

// static
void Manager::RunListScannersProcess(int fd, brillo::Process* process) {
  process->AddArg(kScanImagePath);
  process->AddArg(kScanImageFormattedDeviceListCmd);
  process->BindFd(fd, STDOUT_FILENO);
  process->Run();
}

// static
void Manager::RunScanImageProcess(
    const string& device_name,
    int out_fd,
    ScopedFD* pipe_fd_input,
    ScopedFD* pipe_fd_output,
    const brillo::VariantDictionary& scan_properties,
    brillo::Process* scan_process,
    brillo::Process* convert_process,
    brillo::ErrorPtr* error) {
  scan_process->AddArg(kScanImagePath);
  scan_process->AddArg("-d");
  scan_process->AddArg(device_name);

  for (const auto& property : scan_properties) {
    const string& property_name = property.first;
    const auto& property_value = property.second;
    if (property_name == kScanPropertyMode &&
        property_value.IsTypeCompatible<string>()) {
      string mode = property_value.Get<string>();
      if (mode != kScanPropertyModeColor &&
          mode != kScanPropertyModeGray &&
          mode != kScanPropertyModeLineart) {
        brillo::Error::AddToPrintf(error, FROM_HERE,
                                     brillo::errors::dbus::kDomain,
                                     kManagerServiceError,
                                     "Invalid mode parameter %s",
                                     mode.c_str());
        return;
      }
      scan_process->AddArg("--mode");
      scan_process->AddArg(mode);
    } else if (property_name == kScanPropertyResolution &&
               property_value.IsTypeCompatible<uint32_t>()) {
      scan_process->AddArg("--resolution");
      scan_process->AddArg(
          base::NumberToString(property_value.Get<unsigned int>()));
    } else {
      brillo::Error::AddToPrintf(
          error, FROM_HERE,
          brillo::errors::dbus::kDomain, kManagerServiceError,
          "Invalid scan parameter %s of type %s",
          property_name.c_str(),
          property_value.GetUndecoratedTypeName().c_str());
      return;
    }
  }
  scan_process->BindFd(pipe_fd_output->release(), STDOUT_FILENO);

  convert_process->AddArg(kScanConverterPath);
  convert_process->BindFd(pipe_fd_input->release(), STDIN_FILENO);
  convert_process->BindFd(out_fd, STDOUT_FILENO);

  convert_process->Start();
  scan_process->Start();

  int scan_result = scan_process->Wait();
  metrics_library_->SendEnumToUMA(
      kMetricScanResult,
      scan_result == 0 ? kBooleanMetricSuccess : kBooleanMetricFailure,
      kBooleanMetricMax);
  if (scan_result != 0) {
    brillo::Error::AddToPrintf(error, FROM_HERE,
                                 brillo::errors::dbus::kDomain,
                                 kManagerServiceError,
                                 "Scan process exited with result %d",
                                 scan_result);
    // Explicitly kill and reap the converter since we may fail to successfully
    // reap the processes as it exits this scope.
    convert_process->Kill(SIGKILL, kTimeoutAfterKillSeconds);
    return;
  }

  int converter_result = convert_process->Wait();
  metrics_library_->SendEnumToUMA(
      kMetricConverterResult,
      converter_result == 0 ?  kBooleanMetricSuccess : kBooleanMetricFailure,
      kBooleanMetricMax);
  if (converter_result != 0) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Image converter process failed with result %d", converter_result);
    return;
  }

  LOG(INFO) << __func__ << ": completed image scan and conversion.";
}

// static
Manager::ScannerInfo Manager::ScannerInfoFromString(
    const string& scanner_info_string) {
  vector<string> scanner_output_lines =
      base::SplitString(scanner_info_string, "\n", base::KEEP_WHITESPACE,
                        base::SPLIT_WANT_ALL);

  ScannerInfo scanners;
  for (const auto& line : scanner_output_lines) {
    vector<string> scanner_info_parts =
        base::SplitString(line, "%", base::KEEP_WHITESPACE,
                          base::SPLIT_WANT_ALL);
    if (scanner_info_parts.size() != 4) {
      continue;
    }
    map<string, string> scanner_info;
    scanner_info[kScannerPropertyManufacturer] = scanner_info_parts[1];
    scanner_info[kScannerPropertyModel] = scanner_info_parts[2];
    scanner_info[kScannerPropertyType] = scanner_info_parts[3];
    scanners[scanner_info_parts[0]] = scanner_info;
  }

  return scanners;
}

}  // namespace lorgnette
