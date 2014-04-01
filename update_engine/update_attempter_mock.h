// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_PLATFORM_UPDATE_ENGINE_UPDATE_ATTEMPTER_MOCK_H_
#define CHROMEOS_PLATFORM_UPDATE_ENGINE_UPDATE_ATTEMPTER_MOCK_H_

#include "update_engine/update_attempter.h"

#include <gmock/gmock.h>

namespace chromeos_update_engine {

class UpdateAttempterMock : public UpdateAttempter {
 public:
  using UpdateAttempter::UpdateAttempter;

  MOCK_METHOD5(Update, void(const std::string& app_version,
                            const std::string& omaha_url,
                            bool obey_proxies,
                            bool interactive,
                            bool is_test));

  MOCK_METHOD5(GetStatus, bool(int64_t* last_checked_time,
                               double* progress,
                               std::string* current_operation,
                               std::string* new_version,
                               int64_t* new_size));

  MOCK_METHOD1(GetBootTimeAtUpdate, bool(base::Time* out_boot_time));
};

}  // namespace chromeos_update_engine

#endif  // CHROMEOS_PLATFORM_UPDATE_ENGINE_UPDATE_ATTEMPTER_MOCK_H_
