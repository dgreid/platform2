// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_SERVICE_H_
#define CRYPTOHOME_MOCK_SERVICE_H_

#include "cryptohome/service_distributed.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <gmock/gmock.h>

namespace cryptohome {

class MockService : public ServiceDistributed {
 public:
  MockService() = default;  // For convenience in unit tests.
  ~MockService() override = default;

  MOCK_METHOD(gboolean,
              Mount,
              (const gchar*,
               const gchar*,
               gboolean,
               gboolean,
               gint*,
               gboolean*,
               GError**),
              (override));
  MOCK_METHOD(gboolean, Unmount, (gboolean*, GError**), (override));
  MOCK_METHOD(bool,
              GetMountPointForUser,
              (const std::string&, base::FilePath*),
              (override));
  MOCK_METHOD(bool, IsOwner, (const std::string&), (override));
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_SERVICE_H_
