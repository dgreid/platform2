// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/manager.h"

#include <signal.h>
#include <stdint.h>

#include <utility>
#include <vector>

#include <base/callback_helpers.h>
#include <base/files/file.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/process.h>
#include <brillo/type_name_undecorate.h>
#include <chromeos/dbus/service_constants.h>

#include "lorgnette/daemon.h"
#include "lorgnette/epson_probe.h"
#include "lorgnette/firewall_manager.h"
#include "lorgnette/sane_client.h"

using std::string;

namespace lorgnette {

namespace {

// Checks that the scan parameters in |params| are supported by our scanning
// and PNG conversion logic.
bool ValidateParams(brillo::ErrorPtr* error, const ScanParameters& params) {
  if (params.depth != 1 && params.depth != 8) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kManagerServiceError,
                               "Invalid scan bit depth %d", params.depth);
    return false;
  }

  if (params.depth == 1 && params.format != kGrayscale) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Cannot have bit depth of 1 with non-grayscale scan");
    return false;
  }

  if (params.lines < 0) {
    brillo::Error::AddTo(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Cannot handle scanning of files with unknown lengths");
    return false;
  }
  return true;
}

}  // namespace

// static
const char Manager::kScanConverterPath[] = "/usr/bin/pnm2png";
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

  std::unique_ptr<SaneDevice> device =
      sane_client_->ConnectToDevice(error, device_name);
  if (!device)
    return false;

  uint32_t resolution = 0;
  string scan_mode;
  if (!ExtractScanOptions(error, scan_properties, &resolution, &scan_mode))
    return false;

  if (resolution != 0 && !device->SetScanResolution(error, resolution))
    return false;

  if (scan_mode != "" && !device->SetScanMode(error, scan_mode))
    return false;

  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    brillo::Error::AddTo(error, FROM_HERE,
                           brillo::errors::dbus::kDomain,
                           kManagerServiceError,
                           "Unable to create process pipe");
    return false;
  }

  base::ScopedFD pipe_out(pipe_fds[0]);
  base::File pipe_in(pipe_fds[1]);

  brillo::ProcessImpl convert_process;
  convert_process.AddArg(kScanConverterPath);
  convert_process.BindFd(pipe_out.release(), STDIN_FILENO);
  convert_process.BindFd(dup(outfd.get()), STDOUT_FILENO);
  if (!convert_process.Start()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Unable to start converter process");
    return false;
  }
  // Explicitly kill and reap the converter if we exit early, since we may fail
  // to successfully reap the process as we exit this scope.
  base::ScopedClosureRunner kill_convert_process(base::BindOnce(
      [](brillo::ProcessImpl* proc) {
        proc->Kill(SIGKILL, kTimeoutAfterKillSeconds);
      },
      &convert_process));

  // Automatically report a scan failure if we exit early. This will be
  // cancelled once scanning has succeeded.
  base::ScopedClosureRunner report_scan_failure(base::BindOnce(
      [](MetricsLibraryInterface* metrics_library) {
        metrics_library->SendEnumToUMA(kMetricScanResult, kBooleanMetricFailure,
                                       kBooleanMetricMax);
      },
      metrics_library_.get()));

  if (!device->StartScan(error))
    return false;

  ScanParameters params;
  if (!device->GetScanParameters(error, &params))
    return false;

  if (!ValidateParams(error, params)) {
    return false;
  }

  int width = params.pixels_per_line;
  int height = params.lines;
  std::string header;
  if (params.format == kGrayscale && params.depth == 1) {
    header = base::StringPrintf("P4\n%d %d\n", width, height);
  } else if (params.format == kGrayscale) {
    header = base::StringPrintf("P5\n%d %d\n255\n", width, height);
  } else if (params.format == kRGB) {
    header = base::StringPrintf("P6\n%d %d\n255\n", width, height);
  }

  int written = pipe_in.WriteAtCurrentPos(header.c_str(), header.size());
  if (written != header.size()) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Failed to write image header to pipe: %d", errno);
    return false;
  }

  const int buffer_length = 4 * 1024;
  std::vector<uint8_t> image_buffer(buffer_length, '\0');
  size_t read = 0;

  bool result = device->ReadScanData(error, image_buffer.data(),
                                     image_buffer.size(), &read);
  while (result && read > 0) {
    int ret = pipe_in.WriteAtCurrentPos(
        reinterpret_cast<char*>(image_buffer.data()), read);
    if (ret < 0) {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
          "Failed to write image data to pipe: %d", errno);
      return false;
    }

    result = device->ReadScanData(error, image_buffer.data(),
                                  image_buffer.size(), &read);
  }

  (void)report_scan_failure.Release();
  metrics_library_->SendEnumToUMA(
      kMetricScanResult, result ? kBooleanMetricSuccess : kBooleanMetricFailure,
      kBooleanMetricMax);
  if (!result) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Scanning image failed.");
    return false;
  }

  // Clear the ScopedClosureRunner since we're about to wait for the convert
  // process to terminate.
  (void)kill_convert_process.Release();
  pipe_in.Close();
  int converter_result = convert_process.Wait();
  metrics_library_->SendEnumToUMA(
      kMetricConverterResult,
      converter_result == 0 ? kBooleanMetricSuccess : kBooleanMetricFailure,
      kBooleanMetricMax);
  if (converter_result != 0) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Image converter process failed with result %d", converter_result);
    return false;
  }

  LOG(INFO) << __func__ << ": completed image scan and conversion.";

  if (!activity_callback_.is_null())
    activity_callback_.Run();
  return true;
}

// static
bool Manager::ExtractScanOptions(
    brillo::ErrorPtr* error,
    const brillo::VariantDictionary& scan_properties,
    uint32_t* resolution_out,
    string* mode_out) {
  uint32_t resolution;
  string mode;
  for (const auto& property : scan_properties) {
    const string& property_name = property.first;
    const auto& property_value = property.second;
    if (property_name == kScanPropertyMode &&
        property_value.IsTypeCompatible<string>()) {
      mode = property_value.Get<string>();
      if (mode != kScanPropertyModeColor && mode != kScanPropertyModeGray &&
          mode != kScanPropertyModeLineart) {
        brillo::Error::AddToPrintf(
            error, FROM_HERE, brillo::errors::dbus::kDomain,
            kManagerServiceError, "Invalid mode parameter %s", mode.c_str());
        return false;
      }
    } else if (property_name == kScanPropertyResolution &&
               property_value.IsTypeCompatible<uint32_t>()) {
      resolution = property_value.Get<unsigned int>();
    } else {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
          "Invalid scan parameter %s of type %s", property_name.c_str(),
          property_value.GetUndecoratedTypeName().c_str());
      return false;
    }
  }
  if (resolution_out)
    *resolution_out = resolution;
  if (mode_out)
    *mode_out = mode;
  return true;
}

}  // namespace lorgnette
