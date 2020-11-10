// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_CONNECTION_INFO_READER_H_
#define SHILL_MOCK_CONNECTION_INFO_READER_H_

#include <vector>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/connection_info_reader.h"

namespace shill {

class ConnectionInfo;

class MockConnectionInfoReader : public ConnectionInfoReader {
 public:
  MockConnectionInfoReader();
  MockConnectionInfoReader(const MockConnectionInfoReader&) = delete;
  MockConnectionInfoReader& operator=(const MockConnectionInfoReader&) = delete;

  ~MockConnectionInfoReader() override;

  MOCK_METHOD(bool,
              LoadConnectionInfo,
              (std::vector<ConnectionInfo>*),
              (override));
};

}  // namespace shill

#endif  // SHILL_MOCK_CONNECTION_INFO_READER_H_
