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

std::unique_ptr<SaneDevice> SaneClientImpl::ConnectToDeviceInternal(
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
  if (options_.count(kResolution) != 0) {
    int index = options_.at(kResolution).GetIndex();
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
  }

  if (options_.count(kSource) != 0) {
    int index = options_.at(kSource).GetIndex();
    const SANE_Option_Descriptor* descriptor =
        sane_get_option_descriptor(handle_, index);
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
  }

  if (options_.count(kScanMode) != 0) {
    int index = options_.at(kScanMode).GetIndex();
    const SANE_Option_Descriptor* descriptor =
        sane_get_option_descriptor(handle_, index);
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
  }

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

  SaneOption& option = options_.at(kResolution);
  if (!option.SetInt(resolution)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Failed to set SaneOption");
    return false;
  }
  return UpdateDeviceOption(error, &option);
}

bool SaneDeviceImpl::GetDocumentSource(brillo::ErrorPtr* error,
                                       std::string* source_name_out) {
  if (!source_name_out) {
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

  SaneOption& option = options_.at(kSource);
  base::Optional<std::string> source_name = option.GetString();
  if (!source_name.has_value()) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Source is not a string option");
    return false;
  }

  *source_name_out = source_name.value();
  return true;
}

bool SaneDeviceImpl::SetDocumentSource(brillo::ErrorPtr* error,
                                       const std::string& source_name) {
  if (options_.count(kSource) == 0) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "No source option found.");
    return false;
  }

  SaneOption& option = options_.at(kSource);
  if (!option.SetString(source_name)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Failed to set SaneOption");
    return false;
  }
  return UpdateDeviceOption(error, &option);
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

  SaneOption& option = options_.at(kScanMode);
  if (!option.SetString(mode_string)) {
    brillo::Error::AddTo(error, FROM_HERE, brillo::errors::dbus::kDomain,
                         kManagerServiceError, "Failed to set SaneOption");
    return false;
  }

  return UpdateDeviceOption(error, &option);
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
  std::vector<uint32_t> values;
  if (opt.constraint_type == SANE_CONSTRAINT_WORD_LIST) {
    int num_values = opt.constraint.word_list[0];
    for (int i = 1; i <= num_values; i++) {
      SANE_Word w = opt.constraint.word_list[i];
      int value = opt.type == SANE_TYPE_FIXED ? SANE_UNFIX(w) : w;
      values.push_back(value);
    }
  } else if (opt.constraint_type == SANE_CONSTRAINT_RANGE) {
    const SANE_Range* range = opt.constraint.range;
    for (int i = range->min; i <= range->max; i += range->quant) {
      values.push_back(i);
    }
  } else {
    brillo::Error::AddToPrintf(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Invalid option constraint type %d", opt.constraint_type);
    return base::nullopt;
  }

  return values;
}

SaneOption::SaneOption(const SANE_Option_Descriptor& opt, int index) {
  name_ = opt.name;
  index_ = index;
  type_ = opt.type;
  if (type_ == SANE_TYPE_STRING) {
    // opt.size is the maximum size of the string option, including the null
    // terminator (which is mandatory).
    string_data_.resize(opt.size);
  }
}

bool SaneOption::SetInt(int i) {
  switch (type_) {
    case SANE_TYPE_INT:
      int_data_.i = i;
      return true;
    case SANE_TYPE_FIXED:
      int_data_.f = SANE_FIX(static_cast<double>(i));
      return true;
    default:
      return false;
  }
}

bool SaneOption::SetString(const std::string& s) {
  if (type_ != SANE_TYPE_STRING) {
    return false;
  }

  size_t size_with_null = s.size() + 1;
  if (size_with_null > string_data_.size()) {
    LOG(ERROR) << "String size " << size_with_null
               << " exceeds maximum option size " << string_data_.size();
    return false;
  }

  memcpy(string_data_.data(), s.c_str(), size_with_null);
  return true;
}

base::Optional<std::string> SaneOption::GetString() const {
  if (type_ != SANE_TYPE_STRING)
    return base::nullopt;

  return std::string(string_data_.data());
}

void* SaneOption::GetPointer() {
  if (type_ == SANE_TYPE_STRING)
    return string_data_.data();
  else if (type_ == SANE_TYPE_INT)
    return &int_data_.i;
  else if (type_ == SANE_TYPE_FIXED)
    return &int_data_.f;
  else
    return nullptr;
}

int SaneOption::GetIndex() const {
  return index_;
}

std::string SaneOption::GetName() const {
  return name_;
}

std::string SaneOption::DisplayValue() const {
  switch (type_) {
    case SANE_TYPE_INT:
      return std::to_string(int_data_.i);
    case SANE_TYPE_FIXED:
      return std::to_string(static_cast<int>(SANE_UNFIX(int_data_.f)));
    case SANE_TYPE_STRING:
      return GetString().value();
    default:
      return "[invalid]";
  }
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

    base::Optional<ScanOption> option_name;
    if ((opt->type == SANE_TYPE_INT || opt->type == SANE_TYPE_FIXED) &&
        opt->size == sizeof(SANE_Int) && opt->unit == SANE_UNIT_DPI &&
        strcmp(opt->name, SANE_NAME_SCAN_RESOLUTION) == 0) {
      option_name = kResolution;
    } else if ((opt->type == SANE_TYPE_STRING) &&
               strcmp(opt->name, SANE_NAME_SCAN_MODE) == 0) {
      option_name = kScanMode;
    } else if ((opt->type == SANE_TYPE_STRING) &&
               strcmp(opt->name, SANE_NAME_SCAN_SOURCE) == 0) {
      option_name = kSource;
    }

    if (option_name.has_value()) {
      SaneOption sane_option(*opt, i);
      SANE_Status status = sane_control_option(
          handle_, i, SANE_ACTION_GET_VALUE, sane_option.GetPointer(), NULL);
      if (status != SANE_STATUS_GOOD) {
        brillo::Error::AddToPrintf(
            error, FROM_HERE, brillo::errors::dbus::kDomain,
            kManagerServiceError, "Unable to read option value %d for device",
            i);
        return false;
      }
      options_.insert({option_name.value(), std::move(sane_option)});
    }
  }

  return true;
}

bool SaneDeviceImpl::UpdateDeviceOption(brillo::ErrorPtr* error,
                                        SaneOption* option) {
  SANE_Int result_flags;
  SANE_Status status =
      sane_control_option(handle_, option->GetIndex(), SANE_ACTION_SET_VALUE,
                          option->GetPointer(), &result_flags);
  if (status != SANE_STATUS_GOOD) {
    brillo::Error::AddTo(
        error, FROM_HERE, brillo::errors::dbus::kDomain, kManagerServiceError,
        "Unable to set " + option->GetName() + " to " + option->DisplayValue() +
            " : " + sane_strstatus(status));
    // Reload options, to bring local value and device value back in sync.
    LoadOptions(error);
    return false;
  }

  if (result_flags & SANE_INFO_RELOAD_OPTIONS) {
    return LoadOptions(error);
  }

  return true;
}

}  // namespace lorgnette
