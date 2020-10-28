// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libmems/iio_device.h"

#include <stdlib.h>

#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>

#include "libmems/common_types.h"
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

bool IioDevice::GetMinMaxFrequency(double* min_freq, double* max_freq) {
  auto available_opt = ReadStringAttribute(kSamplingFrequencyAvailable);
  if (!available_opt.has_value()) {
    LOG(ERROR) << "Failed to read attribute: " << kSamplingFrequencyAvailable;
    return false;
  }

  std::string sampling_frequency_available = available_opt.value();
  // Remove trailing '\0' for parsing
  auto pos = available_opt->find_first_of('\0');
  if (pos != std::string::npos)
    sampling_frequency_available = available_opt->substr(0, pos);

  std::vector<std::string> sampling_frequencies =
      base::SplitString(sampling_frequency_available, " ",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);

  switch (sampling_frequencies.size()) {
    case 0:
      LOG(ERROR) << "Invalid format of " << kSamplingFrequencyAvailable << ": "
                 << sampling_frequency_available;
      return false;

    case 1:
      if (!base::StringToDouble(sampling_frequencies.front(), min_freq) ||
          *min_freq < 0.0 || *min_freq < kFrequencyEpsilon) {
        LOG(ERROR) << "Failed to parse min max sampling_frequency: "
                   << sampling_frequency_available;
        return false;
      }

      *max_freq = *min_freq;
      return true;

    default:
      if (!base::StringToDouble(sampling_frequencies.back(), max_freq) ||
          *max_freq < kFrequencyEpsilon) {
        LOG(ERROR) << "Failed to parse max sampling_frequency: "
                   << sampling_frequency_available;
        return false;
      }

      if (!base::StringToDouble(sampling_frequencies.front(), min_freq) ||
          *min_freq < 0.0) {
        LOG(ERROR) << "Failed to parse the first sampling_frequency: "
                   << sampling_frequency_available;
        return false;
      }

      if (*min_freq == 0.0) {
        if (!base::StringToDouble(sampling_frequencies[1], min_freq) ||
            *min_freq < 0.0 || *max_freq < *min_freq) {
          LOG(ERROR) << "Failed to parse min sampling_frequency: "
                     << sampling_frequency_available;
          return false;
        }
      }

      return true;
  }
}

}  // namespace libmems
