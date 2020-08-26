// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/sane_client_impl.h"

#include <base/logging.h>
#include <chromeos/dbus/service_constants.h>
#include <sane/saneopts.h>

#include "lorgnette/dbus_adaptors/org.chromium.lorgnette.Manager.h"
#include "lorgnette/guess_source.h"

namespace lorgnette {

namespace {

DocumentSource CreateDocumentSource(const std::string& name) {
  DocumentSource source;
  source.set_name(name);
  base::Optional<SourceType> type = GuessSourceType(name);
  if (type.has_value()) {
    source.set_type(type.value());
  }
  return source;
}

}  // namespace

// static
std::unique_ptr<SaneClientImpl> SaneClientImpl::Create() {
  SANE_Status status = sane_init(nullptr, nullptr);
  if (status != SANE_STATUS_GOOD) {
    LOG(ERROR) << "Unable to initialize SANE";
    return nullptr;
  }

  // Cannot use make_unique() with a private constructor.
  return std::unique_ptr<SaneClientImpl>(new SaneClientImpl());
}

SaneClientImpl::~SaneClientImpl() {
  sane_exit();
}

bool SaneClientImpl::ListDevices(brillo::ErrorPtr* error,
                                 std::vector<ScannerInfo>* scanners_out) {
  base::AutoLock auto_lock(lock_);
  const SANE_Device** device_list;
  SANE_Status status = sane_get_devices(&device_list, SANE_FALSE);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Unable to get device list from SANE");
    return false;
  }

  return DeviceListToScannerInfo(device_list, scanners_out);
}

// static
bool SaneClientImpl::DeviceListToScannerInfo(
    const SANE_Device** device_list, std::vector<ScannerInfo>* scanners_out) {
  if (!device_list || !scanners_out) {
    LOG(ERROR) << "'device_list' and 'scanners_out' cannot be NULL";
    return false;
  }

  std::unordered_set<std::string> names;
  std::vector<ScannerInfo> scanners;
  for (int i = 0; device_list[i]; i++) {
    const SANE_Device* dev = device_list[i];
    if (!dev->name || strcmp(dev->name, "") == 0)
      continue;

    if (names.count(dev->name) != 0) {
      LOG(ERROR) << "Duplicate device name: " << dev->name;
      return false;
    }
    names.insert(dev->name);

    ScannerInfo info;
    info.set_name(dev->name);
    info.set_manufacturer(dev->vendor ? dev->vendor : "");
    info.set_model(dev->model ? dev->model : "");
    info.set_type(dev->type ? dev->type : "");
    scanners.push_back(info);
  }
  *scanners_out = scanners;
  return true;
}

SaneClientImpl::SaneClientImpl()
    : open_devices_(std::make_shared<DeviceSet>()) {}

std::unique_ptr<SaneDevice> SaneClientImpl::ConnectToDevice(
    brillo::ErrorPtr* error, const std::string& device_name) {
  base::AutoLock auto_lock(lock_);
  SANE_Handle handle;
  SANE_Status status = sane_open(device_name.c_str(), &handle);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kManagerServiceError,
                               "Unable to open device '%s': %s",
                               device_name.c_str(), sane_strstatus(status));
    return nullptr;
  }

  {
    base::AutoLock auto_lock(open_devices_->first);
    if (open_devices_->second.count(device_name) != 0) {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
          "Device '%s' is currently in-use", device_name.c_str());
      return nullptr;
    }
    open_devices_->second.insert(device_name);
  }

  // Cannot use make_unique() with a private constructor.
  auto device = std::unique_ptr<SaneDeviceImpl>(
      new SaneDeviceImpl(handle, device_name, open_devices_));
  device->LoadOptions(error);
  return device;
}

SaneDeviceImpl::~SaneDeviceImpl() {
  if (handle_) {
    // If a scan is running, this will call sane_cancel() first.
    sane_close(handle_);
  }
  base::AutoLock auto_lock(open_devices_->first);
  open_devices_->second.erase(name_);
}

bool SaneDeviceImpl::GetValidOptionValues(brillo::ErrorPtr* error,
                                          ValidOptionValues* values_out) {
  if (!handle_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No scanner connected");
    return false;
  }

  if (!values_out) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "'values_out' pointer cannot be null");
    return false;
  }

  ValidOptionValues values;
  int index = options_[kResolution].index;
  const SANE_Option_Descriptor* descriptor =
      sane_get_option_descriptor(handle_, index);
  if (!descriptor) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Unable to get resolution option at index %d", index);
    return false;
  }

  base::Optional<std::vector<uint32_t>> resolutions =
      GetValidIntOptionValues(error, *descriptor);
  if (!resolutions.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Failed to get valid values for resolution setting");
    return false;
  }
  values.resolutions = std::move(resolutions.value());

  index = options_[kSource].index;
  descriptor = sane_get_option_descriptor(handle_, index);
  if (!descriptor) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Unable to get source option at index %d", index);
    return false;
  }

  base::Optional<std::vector<std::string>> source_names =
      GetValidStringOptionValues(error, *descriptor);
  if (!source_names.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Failed to get valid values for sources setting");
    return false;
  }

  for (const std::string& source_name : source_names.value()) {
    values.sources.push_back(CreateDocumentSource(source_name));
  }

  index = options_[kScanMode].index;
  descriptor = sane_get_option_descriptor(handle_, index);
  if (!descriptor) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Unable to get scan mode option at index %d", index);
    return false;
  }

  base::Optional<std::vector<std::string>> color_modes =
      GetValidStringOptionValues(error, *descriptor);
  if (!color_modes.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Failed to get valid values for scan modes setting");
    return false;
  }
  values.color_modes = std::move(color_modes.value());

  *values_out = values;
  return true;
}

bool SaneDeviceImpl::SetScanResolution(brillo::ErrorPtr* error,
                                       int resolution) {
  if (options_.count(kResolution) == 0) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No resolution option found.");
    return false;
  }

  SaneOption option = options_[kResolution];
  option.SetInt(resolution);

  bool should_reload = false;
  SANE_Status status = SetOption(&option, &should_reload);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kManagerServiceError,
                               "Unable to set resolution to %d: %s", resolution,
                               sane_strstatus(status));
    return false;
  }

  if (should_reload) {
    LoadOptions(error);
  } else {
    // If not reloading the option values, we should update the stored option.
    options_[kResolution] = option;
  }

  return true;
}

bool SaneDeviceImpl::GetDocumentSource(brillo::ErrorPtr* error,
                                       DocumentSource* source_out) {
  if (!source_out) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "source_out argument cannot be null");
    return false;
  }

  if (options_.count(kSource) == 0) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No source option found.");
    return false;
  }

  std::string source_name = options_[kSource].value.s;
  *source_out = CreateDocumentSource(source_name);
  return true;
}

bool SaneDeviceImpl::SetDocumentSource(brillo::ErrorPtr* error,
                                       const DocumentSource& source) {
  if (options_.count(kSource) == 0) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No source option found.");
    return false;
  }

  SaneOption option = options_[kSource];
  if (!option.SetString(source.name())) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Failed to set SaneOption");
  }

  bool should_reload = false;
  SANE_Status status = SetOption(&option, &should_reload);
  if (should_reload)
    LoadOptions(error);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kManagerServiceError,
                               "Unable to set document source to %s: %s",
                               source.name().c_str(), sane_strstatus(status));
    return false;
  }

  if (should_reload) {
    LoadOptions(error);
  } else {
    // If not reloading the option values, we should update the stored option.
    options_[kSource] = option;
  }

  return true;
}

bool SaneDeviceImpl::SetColorMode(brillo::ErrorPtr* error,
                                  ColorMode color_mode) {
  std::string mode_string = "";
  switch (color_mode) {
    case MODE_LINEART:
      mode_string = kScanPropertyModeLineart;
      break;
    case MODE_GRAYSCALE:
      mode_string = kScanPropertyModeGray;
      break;
    case MODE_COLOR:
      mode_string = kScanPropertyModeColor;
      break;
    default:
      brillo::Error::AddToPrintf(
          error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
          "Invalid color mode: %s", ColorMode_Name(color_mode).c_str());
      return false;
  }

  if (options_.count(kScanMode) == 0) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No scan mode option found.");
    return false;
  }

  SaneOption option = options_[kScanMode];
  option.SetString(mode_string);

  bool should_reload = false;
  SANE_Status status = SetOption(&option, &should_reload);
  if (should_reload)
    LoadOptions(error);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(error, FROM_HERE, brillo::errors::dbus::kDomain,
                               kManagerServiceError,
                               "Unable to set scan mode to %s: %s",
                               mode_string.c_str(), sane_strstatus(status));
    return false;
  }

  if (should_reload) {
    LoadOptions(error);
  } else {
    // If not reloading the option values, we should update the stored option.
    options_[kScanMode] = option;
  }

  return true;
}

SANE_Status SaneDeviceImpl::StartScan(brillo::ErrorPtr* error) {
  if (scan_running_ && !reached_eof_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Scan is already in progress");
    return SANE_STATUS_DEVICE_BUSY;
  }

  SANE_Status status = sane_start(handle_);
  if (status == SANE_STATUS_GOOD) {
    scan_running_ = true;
  }

  return status;
}

bool SaneDeviceImpl::GetScanParameters(brillo::ErrorPtr* error,
                                       ScanParameters* parameters) {
  if (!handle_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No scanner connected");
    return false;
  }

  if (!parameters) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "'parameters' pointer cannot be null");
    return false;
  }

  SANE_Parameters params;
  SANE_Status status = sane_get_parameters(handle_, &params);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Failed to read scan parameters: %s", sane_strstatus(status));
    return false;
  }

  switch (params.format) {
    case SANE_FRAME_GRAY:
      parameters->format = kGrayscale;
      break;
    case SANE_FRAME_RGB:
      parameters->format = kRGB;
      break;
    default:
      brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                           kManagerServiceError,
                           "Unsupported scan frame format");
      return false;
  }

  parameters->bytes_per_line = params.bytes_per_line;
  parameters->pixels_per_line = params.pixels_per_line;
  parameters->lines = params.lines;
  parameters->depth = params.depth;
  return true;
}

bool SaneDeviceImpl::ReadScanData(brillo::ErrorPtr* error,
                                  uint8_t* buf,
                                  size_t count,
                                  size_t* read_out) {
  if (!handle_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No scanner connected");
    return false;
  }

  if (!scan_running_) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No scan in progress");
    return false;
  }

  if (!buf || !read_out) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "'buf' and 'read' pointers cannot be null");
    return false;
  }
  SANE_Int read = 0;
  SANE_Status status = sane_read(handle_, buf, count, &read);
  switch (status) {
    case SANE_STATUS_GOOD:
      *read_out = read;
      return true;
    case SANE_STATUS_EOF:
      *read_out = 0;
      reached_eof_ = true;
      return true;
    default:
      brillo::Error::AddToPrintf(
          error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
          "sane_read() failed: %s", sane_strstatus(status));
      return false;
  }
}

// static
base::Optional<std::vector<std::string>>
SaneDeviceImpl::GetValidStringOptionValues(brillo::ErrorPtr* error,
                                           const SANE_Option_Descriptor& opt) {
  if (opt.constraint_type != SANE_CONSTRAINT_STRING_LIST) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Invalid option constraint type %d", opt.constraint_type);
    return base::nullopt;
  }

  std::vector<std::string> values;
  for (int i = 0; opt.constraint.string_list[i]; i++) {
    values.push_back(opt.constraint.string_list[i]);
  }

  return values;
}

// static
base::Optional<std::vector<uint32_t>> SaneDeviceImpl::GetValidIntOptionValues(
    brillo::ErrorPtr* error, const SANE_Option_Descriptor& opt) {
  if (opt.constraint_type != SANE_CONSTRAINT_WORD_LIST) {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Invalid option constraint type %d", opt.constraint_type);
    return base::nullopt;
  }

  std::vector<uint32_t> values;
  int num_values = opt.constraint.word_list[0];
  for (int i = 1; i <= num_values; i++) {
    SANE_Word w = opt.constraint.word_list[i];
    int value = opt.type == SANE_TYPE_FIXED ? SANE_UNFIX(w) : w;
    values.push_back(value);
  }

  return values;
}

bool SaneDeviceImpl::SaneOption::SetInt(int i) {
  switch (type) {
    case SANE_TYPE_INT:
      value.i = i;
      return true;
    case SANE_TYPE_FIXED:
      value.f = SANE_FIX(static_cast<double>(i));
      return true;
    default:
      return false;
  }
}

bool SaneDeviceImpl::SaneOption::SetString(const std::string& s) {
  if (type != SANE_TYPE_STRING) {
    return false;
  }

  size_t size_with_null = s.size() + 1;
  if (size_with_null > string_data.size()) {
    LOG(ERROR) << "String size " << size_with_null
               << " exceeds maximum option size " << string_data.size();
    return false;
  }

  memcpy(string_data.data(), s.c_str(), size_with_null);
  return true;
}

SaneDeviceImpl::SaneDeviceImpl(SANE_Handle handle,
                               const std::string& name,
                               std::shared_ptr<DeviceSet> open_devices)
    : handle_(handle),
      name_(name),
      open_devices_(open_devices),
      scan_running_(false),
      reached_eof_(false) {}

bool SaneDeviceImpl::LoadOptions(brillo::ErrorPtr* error) {
  // First we get option descriptor 0, which contains the total count of
  // options. We don't strictly need the descriptor, but it's "Good form" to
  // do so according to 'scanimage'.
  const SANE_Option_Descriptor* desc = sane_get_option_descriptor(handle_, 0);
  if (!desc) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Unable to get option count for device");
    return false;
  }

  SANE_Int num_options = 0;
  SANE_Status status = sane_control_option(handle_, 0, SANE_ACTION_GET_VALUE,
                                           &num_options, nullptr);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError,
                         "Unable to get option count for device");
    return false;
  }

  options_.clear();
  // Start at 1, since we've already checked option 0 above.
  for (int i = 1; i < num_options; i++) {
    const SANE_Option_Descriptor* opt = sane_get_option_descriptor(handle_, i);
    if (!opt) {
      brillo::Error::AddToPrintf(
          error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
          "Unable to get option %d for device", i);
      return false;
    }

    // A pointer to the memory used to store the current setting's value.
    // If this is non-null, we will read the detected option's value and store
    // it to this address.
    void* value = nullptr;
    if ((opt->type == SANE_TYPE_INT || opt->type == SANE_TYPE_FIXED) &&
        opt->size == sizeof(SANE_Int) && opt->unit == SANE_UNIT_DPI &&
        strcmp(opt->name, SANE_NAME_SCAN_RESOLUTION) == 0) {
      options_[kResolution].index = i;
      options_[kResolution].type = opt->type;
      if (opt->type == SANE_TYPE_INT) {
        value = &options_[kResolution].value.i;
      } else {  // type is SANE_TYPE_FIXED
        value = &options_[kResolution].value.f;
      }
    } else if ((opt->type == SANE_TYPE_STRING) &&
               strcmp(opt->name, SANE_NAME_SCAN_MODE) == 0) {
      SaneOption& sane_opt = options_[kScanMode];
      sane_opt.index = i;
      sane_opt.type = opt->type;
      // opt->size is the maximum size of the string option, including the null
      // terminator (which is mandatory).
      sane_opt.string_data.resize(opt->size);
      sane_opt.value.s = sane_opt.string_data.data();
      value = sane_opt.string_data.data();
    } else if ((opt->type == SANE_TYPE_STRING) &&
               strcmp(opt->name, SANE_NAME_SCAN_SOURCE) == 0) {
      SaneOption& sane_opt = options_[kSource];
      sane_opt.index = i;
      sane_opt.type = opt->type;
      sane_opt.string_data.resize(opt->size);
      sane_opt.value.s = sane_opt.string_data.data();
      value = sane_opt.string_data.data();
    }

    if (value) {
      SANE_Status status =
          sane_control_option(handle_, i, SANE_ACTION_GET_VALUE, value, NULL);
      if (status != SANE_STATUS_GOOD) {
        brillo::Error::AddToPrintf(
            error, FROM_HERE, brillo::errors::dbus::kDomain,
            kManagerServiceError, "Unable to read option value %d for device",
            i);
        return false;
      }
    }
  }

  return true;
}

SANE_Status SaneDeviceImpl::SetOption(SaneOption* option, bool* should_reload) {
  void* value;
  switch (option->type) {
    case SANE_TYPE_INT:
      value = &option->value.i;
      break;
    case SANE_TYPE_FIXED:
      value = &option->value.f;
      break;
    case SANE_TYPE_STRING:
      // Do not use '&' here, since SANE_String is already a pointer type.
      value = option->value.s;
      break;
    default:
      return SANE_STATUS_UNSUPPORTED;
  }

  SANE_Int result_flags;
  SANE_Status status = sane_control_option(
      handle_, option->index, SANE_ACTION_SET_VALUE, value, &result_flags);
  if (status != SANE_STATUS_GOOD) {
    return status;
  }

  if (result_flags & SANE_INFO_RELOAD_OPTIONS) {
    *should_reload = true;
  }

  return status;
}

}  // namespace lorgnette
