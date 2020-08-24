// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <fuzzer/FuzzedDataProvider.h>
#include <string>
#include <utility>
#include <vector>

#include "cros-disks/mount_options.h"

namespace cros_disks {
namespace {

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  FuzzedDataProvider data_provider(data, size);

  MountOptions mount_options;

  size_t num_allowed_options =
      data_provider.ConsumeIntegralInRange<uint32_t>(0, 50);
  for (size_t i = 0; i < num_allowed_options; ++i) {
    mount_options.AllowOption(data_provider.ConsumeRandomLengthString(100));
  }

  size_t num_allowed_option_prefixes =
      data_provider.ConsumeIntegralInRange<uint32_t>(0, 50);
  for (size_t i = 0; i < num_allowed_option_prefixes; ++i) {
    mount_options.AllowOptionPrefix(
        data_provider.ConsumeRandomLengthString(100));
  }

  size_t num_enforced_options =
      data_provider.ConsumeIntegralInRange<uint32_t>(0, 50);
  for (size_t i = 0; i < num_enforced_options; ++i) {
    mount_options.EnforceOption(data_provider.ConsumeRandomLengthString(100));
  }

  size_t num_options = data_provider.ConsumeIntegralInRange<uint32_t>(0, 1000);
  std::vector<std::string> options;
  options.reserve(num_options);
  for (size_t i = 0; i < num_options; ++i) {
    options.push_back(data_provider.ConsumeRandomLengthString(100));
  }

  bool set_user_and_group_id = data_provider.ConsumeBool();
  std::string default_user_id = data_provider.ConsumeRandomLengthString(100);
  std::string default_group_id = data_provider.ConsumeRandomLengthString(100);

  mount_options.Initialize(options, set_user_and_group_id, default_user_id,
                           default_group_id);

  std::pair<MountOptions::Flags, std::string> flags_and_data =
      mount_options.ToMountFlagsAndData();

  std::string options_string = mount_options.ToString();

  return 0;
}

}  // namespace
}  // namespace cros_disks
