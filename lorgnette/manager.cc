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
#include <brillo/type_name_undecorate.h>
#include <chromeos/dbus/service_constants.h>
#include <lorgnette/proto_bindings/lorgnette_service.pb.h>
#include <png.h>

#include "lorgnette/daemon.h"
#include "lorgnette/epson_probe.h"
#include "lorgnette/firewall_manager.h"
#include "lorgnette/sane_client.h"

using std::string;

#define ALIGN_UP(val, align) (((val) + (align)-1) & ~((align)-1))

namespace lorgnette {

namespace {

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

base::Optional<SourceType> GuessSourceType(const std::string& name) {
  std::string lowercase = name;
  for (int i = 0; i < lowercase.size(); i++) {
    lowercase[i] = std::tolower(lowercase[i]);
  }

  if (lowercase == "fb" || lowercase == "flatbed")
    return SOURCE_PLATEN;

  if (lowercase == "adf" || lowercase == "adf front" ||
      lowercase == "automatic document feeder")
    return SOURCE_ADF_SIMPLEX;

  if (lowercase == "adf duplex")
    return SOURCE_ADF_DUPLEX;

  return base::nullopt;
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

}  // namespace

const char Manager::kMetricScanResult[] = "DocumentScan.ScanResult";

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
  scanners.insert(scanners.end(), probed_scanners.begin(),
                  probed_scanners.end());

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

  for (const std::string& source_name : options.sources) {
    base::Optional<SourceType> type = GuessSourceType(source_name);
    if (type.has_value()) {
      DocumentSource* source = capabilities.add_sources();
      source->set_type(type.value());
      source->set_name(source_name);
    } else {
      LOG(INFO) << "Ignoring source '" << source_name << "' of unknown type.";
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
  if (!sane_client_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No connection to SANE");
    return false;
  }

  LOG(INFO) << "Scanning image from device " << device_name;

  std::unique_ptr<SaneDevice> device =
      sane_client_->ConnectToDevice(error, device_name);
  if (!device)
    return false;

  uint32_t resolution = 0;
  string color_mode_string;
  if (!ExtractScanOptions(error, scan_properties, &resolution,
                          &color_mode_string))
    return false;

  LOG(INFO) << "User requested color mode: '" << color_mode_string
            << "' and resolution: " << resolution;

  if (resolution != 0 && !device->SetScanResolution(error, resolution))
    return false;

  if (color_mode_string != "") {
    base::Optional<ColorMode> color_mode =
        ColorModeFromDbusString(color_mode_string);
    if (!color_mode.has_value()) {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
          "Invalid color mode: %s", color_mode_string.c_str());
      return false;
    }

    if (!device->SetColorMode(error, color_mode.value())) {
      return false;
    }
  }

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

  (void)report_scan_failure.Release();
  metrics_library_->SendEnumToUMA(kMetricScanResult, kBooleanMetricSuccess,
                                  kBooleanMetricMax);

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

}  // namespace lorgnette
