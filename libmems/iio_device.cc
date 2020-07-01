// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libmems/iio_device.h"

#include <stdlib.h>

#include <base/strings/string_number_conversions.h>

#include "libmems/iio_channel.h"

namespace libmems {

IioDevice::~IioDevice() = default;

bool IioDevice::IsSingleSensor() const {
  return ReadStringAttribute("location").has_value();
}

// static
base::Optional<int> IioDevice::GetIdAfterPrefix(const char* id_str,
                                                const char* prefix) {
  size_t id_len = strlen(id_str);
  size_t prefix_len = strlen(prefix);
  if (id_len <= prefix_len || strncmp(id_str, prefix, prefix_len) != 0) {
    return base::nullopt;
  }

  int value = 0;
  bool success = base::StringToInt(std::string(id_str + prefix_len), &value);
  if (success)
    return value;

  return base::nullopt;
}

std::vector<IioChannel*> IioDevice::GetAllChannels() {
  std::vector<IioChannel*> channels;
  for (const auto& channel_data : channels_)
    channels.push_back(channel_data.chn.get());

  return channels;
}

IioChannel* IioDevice::GetChannel(int32_t index) {
  if (index < 0 || index >= channels_.size())
    return nullptr;

  return channels_[index].chn.get();
}

IioChannel* IioDevice::GetChannel(const std::string& name) {
  for (size_t i = 0; i < channels_.size(); ++i) {
    if (channels_[i].chn_id == name)
      return channels_[i].chn.get();
  }

  return nullptr;
}

}  // namespace libmems
