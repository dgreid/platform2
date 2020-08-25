// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/manager.h"

#include <setjmp.h>
#include <signal.h>
#include <stdint.h>

#include <algorithm>
#include <cctype>
#include <utility>

#include <base/callback_helpers.h>
#include <base/files/file.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/stringprintf.h>
#include <brillo/type_name_undecorate.h>
#include <chromeos/dbus/service_constants.h>
#include <crypto/random.h>
#include <png.h>
#include <uuid/uuid.h>

#include "lorgnette/daemon.h"
#include "lorgnette/enums.h"
#include "lorgnette/epson_probe.h"
#include "lorgnette/firewall_manager.h"
#include "lorgnette/sane_client.h"

using std::string;

#define ALIGN_UP(val, align) (((val) + (align)-1) & ~((align)-1))

namespace lorgnette {

namespace {

constexpr base::TimeDelta kDefaultProgressSignalInterval =
    base::TimeDelta::FromMilliseconds(20);
constexpr size_t kUUIDStringLength = 37;

std::string SerializeError(const brillo::ErrorPtr& error_ptr) {
  std::string message;
  const brillo::Error* error = error_ptr.get();
  while (error) {
    // Format error string as "domain/code:message".
    if (!message.empty())
      message += ';';
    message +=
        error->GetDomain() + '/' + error->GetCode() + ':' + error->GetMessage();
    error = error->GetInnerError();
  }
  return message;
}

// Checks that the scan parameters in |params| are supported by our scanning
// and PNG conversion logic.
bool ValidateParams(brillo::ErrorPtr* error, const ScanParameters& params) {
  if (params.depth != 1 && params.depth != 8 && params.depth != 16) {
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

  if (params.lines == 0) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Cannot scan an image with 0 lines");
    return false;
  }
  return true;
}

// Wrapper for libpng functions that handles converting the setjmp based
// exceptions into safe, usable error codes. It is used like so:
//   brillo::ErrorPtr* error = [...];
//   png_struct* png = [...];
//   png_info* info = [...];
//   int result = LibpngErrorWrap(error, png_write_info, png, info);
template <typename Fn, typename... Args>
int LibpngErrorWrap(brillo::ErrorPtr* error,
                    const Fn& libpng_function,
                    png_struct* png,
                    Args... args) {
  jmp_buf* buf = png_set_longjmp_fn(png, longjmp, sizeof(jmp_buf));
  if (!buf) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Failed to initialize jmp_buf");
    return -1;
  }
  int result = setjmp(*buf);
  if (result != 0) {
    // |libpng_function| failed and longjmp'ed here.
    return result;
  }
  libpng_function(png, args...);
  // Disable longjmp so that we don't inadvertently longjmp here from another
  // libpng function.
  png_set_longjmp_fn(png, nullptr, sizeof(jmp_buf));
  return 0;
}

// Initializes libpng, sets up |png_out| and |info_out| to be used for writing
// PNG image data to |out_file|, and writes the PNG header to |out_file|.
bool SetupPngHeader(brillo::ErrorPtr* error,
                    const ScanParameters& params,
                    png_struct** png_out,
                    png_info** info_out,
                    const base::ScopedFILE& out_file) {
  if (!png_out || !info_out) {
    return false;
  }

  png_struct* png =
      png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  if (!png) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Could not initialize PNG write struct");
    return false;
  }

  png_info* info = png_create_info_struct(png);
  if (!info) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Could not initialize PNG info struct");
    png_destroy_write_struct(&png, nullptr);
    return false;
  }

  int width = params.pixels_per_line;
  int height = params.lines;
  int color_type =
      params.format == kGrayscale ? PNG_COLOR_TYPE_GRAY : PNG_COLOR_TYPE_RGB;
  png_set_IHDR(png, info, width, height, params.depth, color_type,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
               PNG_FILTER_TYPE_BASE);

  png_init_io(png, out_file.get());
  int ret = LibpngErrorWrap(error, png_write_info, png, info);
  if (ret != 0) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kManagerServiceError,
                               "Writing PNG info failed with result %d", ret);
    png_destroy_write_struct(&png, &info);
    return false;
  }

  // Setup output transformations within libpng so that image data from SANE
  // can be converted to the correct endianness or values for PNG data.
  switch (params.depth) {
    case 1:
      // Inverts black and white pixels, since monocolor data from SANE has an
      // inverted representation when compared to PNG.
      png_set_invert_mono(png);
      break;
    case 16:
      // Transpose byte order, since PNG is big-endian and SANE is endian-native
      // i.e. little-endian.
      png_set_swap(png);
      break;
  }

  *png_out = png;
  *info_out = info;
  return true;
}

// Create a ScopedFILE which refers to a copy of |fd|.
base::ScopedFILE SetupOutputFile(brillo::ErrorPtr* error,
                                 const base::ScopedFD& fd) {
  base::ScopedFILE file;
  // Dup fd since fdclose() on file will also close the contained fd.
  base::ScopedFD fd_copy(dup(fd.get()));
  if (fd_copy.get() < 0) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Could not duplicate output FD");
    return file;
  }

  file = base::ScopedFILE(fdopen(fd_copy.get(), "w"));
  if (!file) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Failed to open outfd");
    return file;
  }
  // Release |fd_copy| since it is owned by |file| now.
  (void)fd_copy.release();
  return file;
}

base::Optional<ColorMode> ColorModeFromDbusString(const std::string& mode) {
  if (mode == kScanPropertyModeColor) {
    return MODE_COLOR;
  } else if (mode == kScanPropertyModeGray) {
    return MODE_GRAYSCALE;
  } else if (mode == kScanPropertyModeLineart) {
    return MODE_LINEART;
  } else {
    return base::nullopt;
  }
}

DocumentScanSaneBackend BackendFromDeviceName(const std::string& device_name) {
  size_t colon_index = device_name.find(":");
  if (colon_index != std::string::npos) {
    return SaneBackendFromString(device_name.substr(0, colon_index));
  } else {
    return SaneBackendFromString(device_name);
  }
}

// Uses |firwewall_manager| to request port access if |device_name| corresponds
// to a SANE backend that needs the access when connecting to a device. The
// caller should keep the returned object alive as long as port access is
// needed.
base::ScopedClosureRunner RequestPortAccessIfNeeded(
    const std::string& device_name, FirewallManager* firewall_manager) {
  if (BackendFromDeviceName(device_name) != kPixma)
    return base::ScopedClosureRunner();

  firewall_manager->RequestScannerPortAccess();
  return base::ScopedClosureRunner(
      base::BindOnce([](FirewallManager* fm) { fm->ReleaseAllPortsAccess(); },
                     firewall_manager));
}

}  // namespace

const char Manager::kMetricScanRequested[] = "DocumentScan.ScanRequested";
const char Manager::kMetricScanSucceeded[] = "DocumentScan.ScanSucceeded";
const char Manager::kMetricScanFailed[] = "DocumentScan.ScanFailed";

Manager::Manager(base::Callback<void()> activity_callback,
                 std::unique_ptr<SaneClient> sane_client)
    : org::chromium::lorgnette::ManagerAdaptor(this),
      activity_callback_(activity_callback),
      metrics_library_(new MetricsLibrary),
      sane_client_(std::move(sane_client)),
      progress_signal_interval_(kDefaultProgressSignalInterval) {
  // Set signal sender to be the real D-Bus call by default.
  status_signal_sender_ = base::BindRepeating(
      [](base::WeakPtr<Manager> manager,
         const ScanStatusChangedSignal& signal) {
        if (manager) {
          manager->SendScanStatusChangedSignal(impl::SerializeProto(signal));
        }
      },
      weak_factory_.GetWeakPtr());
}

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
                           std::vector<uint8_t>* scanner_list_out) {
  if (!sane_client_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No connection to SANE");
    return false;
  }

  firewall_manager_->RequestScannerPortAccess();
  base::ScopedClosureRunner release_ports(
      base::BindOnce([](FirewallManager* fm) { fm->ReleaseAllPortsAccess(); },
                     firewall_manager_.get()));

  std::vector<ScannerInfo> scanners;
  if (!sane_client_->ListDevices(error, &scanners)) {
    return false;
  }
  activity_callback_.Run();

  std::vector<ScannerInfo> probed_scanners =
      epson_probe::ProbeForScanners(firewall_manager_.get());
  activity_callback_.Run();
  for (const ScannerInfo& scanner : probed_scanners) {
    brillo::ErrorPtr error;
    std::unique_ptr<SaneDevice> device =
        sane_client_->ConnectToDevice(&error, scanner.name());
    if (device) {
      scanners.push_back(scanner);
    } else {
      LOG(INFO) << "Got reponse from Epson scanner " << scanner.name()
                << " that isn't usable for scanning.";
    }
  }

  ListScannersResponse response;
  for (ScannerInfo& scanner : scanners) {
    *response.add_scanners() = std::move(scanner);
  }

  std::vector<uint8_t> serialized;
  serialized.resize(response.ByteSizeLong());
  response.SerializeToArray(serialized.data(), serialized.size());

  *scanner_list_out = std::move(serialized);
  return true;
}

bool Manager::GetScannerCapabilities(brillo::ErrorPtr* error,
                                     const std::string& device_name,
                                     std::vector<uint8_t>* capabilities_out) {
  if (!capabilities_out) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "'capabilities_out' must be non-null");
    return false;
  }

  if (!sane_client_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No connection to SANE");
    return false;
  }

  base::ScopedClosureRunner release_ports =
      RequestPortAccessIfNeeded(device_name, firewall_manager_.get());
  std::unique_ptr<SaneDevice> device =
      sane_client_->ConnectToDevice(error, device_name);
  if (!device)
    return false;

  ValidOptionValues options;
  if (!device->GetValidOptionValues(error, &options))
    return false;

  ScannerCapabilities capabilities;
  for (const uint32_t resolution : options.resolutions) {
    capabilities.add_resolutions(resolution);
  }

  for (const DocumentSource& source : options.sources) {
    if (source.type() != SOURCE_UNSPECIFIED) {
      *capabilities.add_sources() = source;
    } else {
      LOG(INFO) << "Ignoring source '" << source.name() << "' of unknown type.";
    }
  }

  for (const std::string& mode : options.color_modes) {
    if (mode == kScanPropertyModeLineart)
      capabilities.add_color_modes(MODE_LINEART);
    else if (mode == kScanPropertyModeGray)
      capabilities.add_color_modes(MODE_GRAYSCALE);
    else if (mode == kScanPropertyModeColor)
      capabilities.add_color_modes(MODE_COLOR);
  }

  std::vector<uint8_t> serialized;
  serialized.resize(capabilities.ByteSizeLong());
  capabilities.SerializeToArray(serialized.data(), serialized.size());

  *capabilities_out = std::move(serialized);
  return true;
}

bool Manager::ScanImage(brillo::ErrorPtr* error,
                        const string& device_name,
                        const base::ScopedFD& outfd,
                        const brillo::VariantDictionary& scan_properties) {
  StartScanRequest request;
  request.set_device_name(device_name);

  uint32_t resolution = 0;
  string color_mode_string;
  if (!ExtractScanOptions(error, scan_properties, &resolution,
                          &color_mode_string))
    return false;

  LOG(INFO) << "User requested color mode: '" << color_mode_string
            << "' and resolution: " << resolution;

  if (resolution != 0)
    request.mutable_settings()->set_resolution(resolution);

  if (color_mode_string != "") {
    base::Optional<ColorMode> color_mode =
        ColorModeFromDbusString(color_mode_string);
    if (!color_mode.has_value()) {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
          "Invalid color mode: %s", color_mode_string.c_str());
      return false;
    }

    request.mutable_settings()->set_color_mode(color_mode.value());
  }

  std::unique_ptr<SaneDevice> device;
  if (!StartScanInternal(error, request, &device)) {
    return false;
  }

  ScanJobState scan_state;
  scan_state.device_name = device_name;
  scan_state.device = std::move(device);

  if (!RunScanLoop(error, &scan_state, outfd, base::nullopt)) {
    ReportScanFailed(device_name);
    return false;
  }
  ReportScanSucceeded(device_name);

  LOG(INFO) << __func__ << ": completed image scan and conversion.";

  if (!activity_callback_.is_null())
    activity_callback_.Run();
  return true;
}

void Manager::StartScan(
    std::unique_ptr<DBusMethodResponse<std::vector<uint8_t>>> method_response,
    const std::vector<uint8_t>& start_scan_request,
    const base::ScopedFD& outfd) {
  StartScanResponse response;
  response.set_state(SCAN_STATE_FAILED);

  StartScanRequest request;
  if (!request.ParseFromArray(start_scan_request.data(),
                              start_scan_request.size())) {
    response.set_failure_reason("Failed to parse StartScanRequest");
    method_response->Return(impl::SerializeProto(response));
    return;
  }

  brillo::ErrorPtr error;
  std::unique_ptr<SaneDevice> device;
  if (!StartScanInternal(&error, request, &device)) {
    response.set_failure_reason(SerializeError(error));
    method_response->Return(impl::SerializeProto(response));
    return;
  }

  // The scan has now been successfully started, notify the client.
  response.set_state(SCAN_STATE_IN_PROGRESS);

  uuid_t uuid_bytes;
  uuid_generate_random(uuid_bytes);
  std::string uuid(kUUIDStringLength, '\0');
  uuid_unparse(uuid_bytes, &uuid[0]);
  // Remove the null terminator from the string.
  uuid.resize(kUUIDStringLength - 1);
  response.set_scan_uuid(uuid);
  method_response->Return(impl::SerializeProto(response));

  ScanJobState scan_state;
  scan_state.device_name = request.device_name();
  scan_state.device = std::move(device);

  if (!RunScanLoop(&error, &scan_state, outfd, uuid)) {
    ReportScanFailed(request.device_name());
    SendFailureSignal(uuid, SerializeError(error));
    return;
  }
  ReportScanSucceeded(request.device_name());

  LOG(INFO) << __func__ << ": completed image scan and conversion.";

  SendStatusSignal(uuid, SCAN_STATE_COMPLETED, 1, 100);
  if (!activity_callback_.is_null())
    activity_callback_.Run();
}

void Manager::SetProgressSignalInterval(base::TimeDelta interval) {
  progress_signal_interval_ = interval;
}

void Manager::SetScanStatusChangedSignalSenderForTest(
    StatusSignalSender sender) {
  status_signal_sender_ = sender;
}

bool Manager::StartScanInternal(brillo::ErrorPtr* error,
                                const StartScanRequest& request,
                                std::unique_ptr<SaneDevice>* device_out) {
  if (!device_out) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "device_out cannot be null");
    return false;
  }

  if (request.device_name() == "") {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "A device name must be provided");
    return false;
  }

  if (!sane_client_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No connection to SANE");
    return false;
  }

  LOG(INFO) << "Scanning image from device " << request.device_name();

  base::ScopedClosureRunner release_ports =
      RequestPortAccessIfNeeded(request.device_name(), firewall_manager_.get());
  std::unique_ptr<SaneDevice> device =
      sane_client_->ConnectToDevice(error, request.device_name());
  if (!device) {
    return false;
  }

  ReportScanRequested(request.device_name());

  const ScanSettings& settings = request.settings();

  if (settings.resolution() != 0) {
    LOG(INFO) << "User requested resolution: " << settings.resolution();
    if (!device->SetScanResolution(error, settings.resolution())) {
      return false;
    }
  }

  if (settings.has_source()) {
    LOG(INFO) << "User requested document source: '" << settings.source().name()
              << "'";
    if (!device->SetDocumentSource(error, settings.source())) {
      return false;
    }
  }

  if (settings.color_mode() != MODE_UNSPECIFIED) {
    LOG(INFO) << "User requested color mode: '"
              << ColorMode_Name(settings.color_mode()) << "'";
    if (!device->SetColorMode(error, settings.color_mode())) {
      return false;
    }
  }

  SANE_Status status = device->StartScan(error);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kManagerServiceError, "Failed to start scan: %s",
                               sane_strstatus(status));
    ReportScanFailed(request.device_name());
    return false;
  }

  *device_out = std::move(device);
  return true;
}

bool Manager::RunScanLoop(brillo::ErrorPtr* error,
                          ScanJobState* scan_state,
                          const base::ScopedFD& outfd,
                          base::Optional<std::string> scan_uuid) {
  SaneDevice* device = scan_state->device.get();
  ScanParameters params;
  if (!device->GetScanParameters(error, &params)) {
    return false;
  }

  if (!ValidateParams(error, params)) {
    return false;
  }

  base::ScopedFILE out_file = SetupOutputFile(error, outfd);
  if (!out_file) {
    return false;
  }

  png_struct* png;
  png_info* info;
  if (!SetupPngHeader(error, params, &png, &info, out_file)) {
    return false;
  }
  base::ScopedClosureRunner cleanup_png(base::BindOnce(
      [](png_struct** png, png_info** info) {
        png_destroy_write_struct(png, info);
      },
      &png, &info));

  // Sanity check to make sure that we're not consuming more data in
  // png_write_row than we have available.
  if (png_get_rowbytes(png, info) > params.bytes_per_line) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "PNG image row requires %zu bytes, but SANE is only providing %d bytes",
        png_get_rowbytes(png, info), params.bytes_per_line);
    return false;
  }

  base::TimeTicks last_progress_sent_time = base::TimeTicks::Now();
  uint32_t last_progress_value = 0;
  size_t rows_written = 0;
  const size_t buffer_length =
      std::max(ALIGN_UP(params.bytes_per_line, 4 * 1024), 1024 * 1024);
  std::vector<uint8_t> image_buffer(buffer_length, '\0');
  // The offset within image_buffer to read to. This will be used within the
  // loop for when we've read a partial image line and need to track data that
  // is saved between loop iterations.
  //
  // We maintain the invariant at the start of each loop iteration that indices
  // [0, buffer_offset) hold previously read data.
  size_t buffer_offset = 0;
  while (true) {
    // Get next chunk of scan data from the device.
    size_t read = 0;
    bool result =
        device->ReadScanData(error, image_buffer.data() + buffer_offset,
                             image_buffer.size() - buffer_offset, &read);
    if (!result) {
      brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                           kManagerServiceError, "Reading scan data failed.");
      return false;
    }

    if (read == 0) {
      break;
    }

    // Write as many lines of the image as we can with the data we've received.
    // Indices [buffer_offset, buffer_offset + read) hold the data we just read.
    size_t bytes_available = buffer_offset + read;
    size_t bytes_converted = 0;
    while (bytes_available - bytes_converted >= params.bytes_per_line) {
      int ret = LibpngErrorWrap(error, png_write_row, png,
                                image_buffer.data() + bytes_converted);
      if (ret != 0) {
        brillo::Error::AddToPrintf(
            error, FROM_HERE, brillo::errors::dbus::kDomain,
            kManagerServiceError, "Writing PNG row failed with result %d", ret);
        return false;
      }
      bytes_converted += params.bytes_per_line;
      rows_written++;
      uint32_t progress = rows_written * 100 / params.lines;
      base::TimeTicks now = base::TimeTicks::Now();
      if (scan_uuid.has_value() && progress != last_progress_value &&
          now - last_progress_sent_time >= progress_signal_interval_) {
        SendStatusSignal(scan_uuid.value(), SCAN_STATE_IN_PROGRESS, 1,
                         progress);
        last_progress_value = progress;
        last_progress_sent_time = now;
      }
    }

    // Shift any unconverted data in image_buffer to the start of image_buffer.
    size_t remaining_bytes = bytes_available - bytes_converted;
    memmove(image_buffer.data(), image_buffer.data() + bytes_converted,
            remaining_bytes);
    buffer_offset = remaining_bytes;
  }

  if (buffer_offset != 0) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Received incomplete scan data, %zu unused bytes remaining",
        buffer_offset);
    return false;
  }

  int ret = LibpngErrorWrap(error, png_write_end, png, info);
  if (ret != 0) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Finalizing PNG write failed with result %d", ret);
    return false;
  }


  return true;
}

// static
bool Manager::ExtractScanOptions(
    brillo::ErrorPtr* error,
    const brillo::VariantDictionary& scan_properties,
    uint32_t* resolution_out,
    string* mode_out) {
  uint32_t resolution = 0;
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
      resolution = property_value.Get<uint32_t>();
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

void Manager::ReportScanRequested(const std::string& device_name) {
  DocumentScanSaneBackend backend = BackendFromDeviceName(device_name);
  metrics_library_->SendEnumToUMA(kMetricScanRequested, backend,
                                  DocumentScanSaneBackend::kMaxValue);
}

void Manager::ReportScanSucceeded(const std::string& device_name) {
  DocumentScanSaneBackend backend = BackendFromDeviceName(device_name);
  metrics_library_->SendEnumToUMA(kMetricScanSucceeded, backend,
                                  DocumentScanSaneBackend::kMaxValue);
}

void Manager::ReportScanFailed(const std::string& device_name) {
  DocumentScanSaneBackend backend = BackendFromDeviceName(device_name);
  metrics_library_->SendEnumToUMA(kMetricScanFailed, backend,
                                  DocumentScanSaneBackend::kMaxValue);
}

void Manager::SendStatusSignal(std::string uuid,
                               ScanState state,
                               int page,
                               int progress) {
  ScanStatusChangedSignal signal;
  signal.set_scan_uuid(uuid);
  signal.set_state(state);
  signal.set_page(page);
  signal.set_progress(progress);
  status_signal_sender_.Run(signal);
}

void Manager::SendFailureSignal(std::string uuid, std::string failure_reason) {
  ScanStatusChangedSignal signal;
  signal.set_scan_uuid(uuid);
  signal.set_state(SCAN_STATE_FAILED);
  signal.set_failure_reason(failure_reason);
  status_signal_sender_.Run(signal);
}

}  // namespace lorgnette
