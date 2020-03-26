// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/manager.h"

#include <signal.h>
#include <stdint.h>
#include <unistd.h>

#include <utility>
#include <vector>

#include <base/callback_helpers.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/process.h>
#include <brillo/type_name_undecorate.h>
#include <brillo/variant_dictionary.h>
#include <chromeos/dbus/service_constants.h>

#include "lorgnette/daemon.h"
#include "lorgnette/epson_probe.h"
#include "lorgnette/firewall_manager.h"
#include "lorgnette/sane_client.h"

using base::ScopedFD;
using base::StringPrintf;
using std::map;
using std::string;
using std::vector;

namespace lorgnette {

// static
const char Manager::kScanConverterPath[] = "/usr/bin/pnm2png";
const char Manager::kScanImagePath[] = "/usr/bin/scanimage";
const int Manager::kTimeoutAfterKillSeconds = 1;
const char Manager::kMetricScanResult[] = "DocumentScan.ScanResult";
const char Manager::kMetricConverterResult[] = "DocumentScan.ConverterResult";

Manager::Manager(base::Callback<void()> activity_callback,
                 std::unique_ptr<SaneClient> sane_client)
    : org::chromium::lorgnette::ManagerAdaptor(this),
      activity_callback_(activity_callback),
      metrics_library_(new MetricsLibrary),
      sane_client_(std::move(sane_client)) {}

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
  if (!sane_client_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No connection to SANE");
    return false;
  }

  firewall_manager_->RequestScannerPortAccess();
  base::ScopedClosureRunner release_ports(
      base::BindOnce([](FirewallManager* fm) { fm->ReleaseAllPortsAccess(); },
                     firewall_manager_.get()));

  ScannerInfo scanners;
  if (!sane_client_->ListDevices(error, &scanners)) {
    return false;
  }
  activity_callback_.Run();

  epson_probe::ProbeForScanners(firewall_manager_.get(), &scanners);

  *scanner_list = scanners;
  return true;
}

bool Manager::ScanImage(brillo::ErrorPtr* error,
                        const string& device_name,
                        const base::ScopedFD& outfd,
                        const brillo::VariantDictionary& scan_properties) {
  if (!sane_client_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No connection to SANE");
    return false;
  }

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

}  // namespace lorgnette
