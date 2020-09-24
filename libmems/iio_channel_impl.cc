// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/strings/stringprintf.h>

#include "libmems/common_types.h"
#include "libmems/iio_channel_impl.h"
#include "libmems/iio_device.h"

namespace libmems {

IioChannelImpl::IioChannelImpl(iio_channel* channel) : channel_(channel) {
  CHECK(channel_);
}

const char* IioChannelImpl::GetId() const {
  return iio_channel_get_id(channel_);
}

bool IioChannelImpl::IsEnabled() const {
  return iio_channel_is_enabled(channel_);
}

void IioChannelImpl::SetEnabled(bool en) {
  if (en)
    iio_channel_enable(channel_);
  else
    iio_channel_disable(channel_);
}

bool IioChannelImpl::SetScanElementsEnabled(bool en) {
  if (!iio_channel_is_scan_element(channel_))
    return true;

  std::string en_attrib_name = base::StringPrintf(
      "scan_elements/%s_%s_en", iio_channel_is_output(channel_) ? "out" : "in",
      GetId());
  int error = iio_channel_attr_write_bool(channel_, en_attrib_name.c_str(), en);
  if (error) {
    LOG(WARNING) << "could not write to " << en_attrib_name
                 << ", error: " << error;
    return false;
  }

  return true;
}

base::Optional<std::string> IioChannelImpl::ReadStringAttribute(
    const std::string& name) const {
  char data[kReadAttrBufferSize] = {0};
  ssize_t len =
      iio_channel_attr_read(channel_, name.c_str(), data, sizeof(data));
  if (len < 0) {
    LOG(WARNING) << "Attempting to read attribute " << name
                 << " failed: " << len;
    return base::nullopt;
  }
  return std::string(data, len);
}

base::Optional<int64_t> IioChannelImpl::ReadNumberAttribute(
    const std::string& name) const {
  long long val = 0;  // NOLINT(runtime/int)
  int error = iio_channel_attr_read_longlong(channel_, name.c_str(), &val);
  if (error) {
    LOG(WARNING) << "Attempting to read attribute " << name
                 << " failed: " << error;
    return base::nullopt;
  }
  return val;
}

base::Optional<int64_t> IioChannelImpl::Convert(const uint8_t* src) const {
  const iio_data_format* format = iio_channel_get_data_format(channel_);
  if (!format) {
    LOG(WARNING) << "Cannot find format of channel: " << GetId();
    return base::nullopt;
  }

  size_t len = format->length;
  if (len == 0)
    return 0;

  int64_t value = 0;
  iio_channel_convert(channel_, &value, src);

  if (format->is_signed && len < CHAR_BIT * sizeof(int64_t)) {
    int64_t mask = 1LL << (len - 1);

    if (mask & value) {
      // Doing sign extension
      value |= (~0LL) << len;
    }
  }

  return value;
}

base::Optional<double> IioChannelImpl::ReadDoubleAttribute(
    const std::string& name) const {
  double val = 0;
  int error = iio_channel_attr_read_double(channel_, name.c_str(), &val);
  if (error) {
    LOG(WARNING) << "Attempting to read attribute " << name
                 << " failed: " << error;
    return base::nullopt;
  }
  return val;
}

bool IioChannelImpl::WriteStringAttribute(const std::string& name,
                                          const std::string& value) {
  int error = iio_channel_attr_write_raw(
      channel_, name.size() > 0 ? name.c_str() : nullptr, value.data(),
      value.size());
  if (error) {
    LOG(WARNING) << "Attempting to write attribute " << name
                 << " failed: " << error;
    return false;
  }
  return true;
}

bool IioChannelImpl::WriteNumberAttribute(const std::string& name,
                                          int64_t value) {
  int error = iio_channel_attr_write_longlong(channel_, name.c_str(), value);
  if (error) {
    LOG(WARNING) << "Attempting to write attribute " << name
                 << " failed: " << error;
    return false;
  }
  return true;
}

bool IioChannelImpl::WriteDoubleAttribute(const std::string& name,
                                          double value) {
  int error = iio_channel_attr_write_double(channel_, name.c_str(), value);
  if (error) {
    LOG(WARNING) << "Attempting to write attribute " << name
                 << " failed: " << error;
    return false;
  }
  return true;
}

base::Optional<uint64_t> IioChannelImpl::Length() const {
  const iio_data_format* format = iio_channel_get_data_format(channel_);
  if (!format) {
    LOG(WARNING) << "Cannot find format of channel: " << GetId();
    return base::nullopt;
  }

  return format->length;
}

}  // namespace libmems
