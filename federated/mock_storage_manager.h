// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_MOCK_STORAGE_MANAGER_H_
#define FEDERATED_MOCK_STORAGE_MANAGER_H_

#include "federated/storage_manager.h"

#include <string>

#include <base/macros.h>
#include <gmock/gmock.h>

namespace federated {

class MockStorageManager : public StorageManager {
 public:
  MockStorageManager() = default;
  MockStorageManager(const MockStorageManager&) = delete;
  MockStorageManager& operator=(const MockStorageManager&) = delete;

  ~MockStorageManager() override = default;

  MOCK_METHOD(bool,
              OnExampleReceived,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              PrepareStreamingForClient,
              (const std::string&),
              (override));
  MOCK_METHOD(bool, GetNextExample, (std::string*), (override));
  MOCK_METHOD(bool, CloseStreaming, (), (override));
};

}  // namespace federated

#endif  // FEDERATED_MOCK_STORAGE_MANAGER_H_
