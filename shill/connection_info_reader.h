// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_CONNECTION_INFO_READER_H_
#define SHILL_CONNECTION_INFO_READER_H_

#include <string>
#include <vector>

#include <base/macros.h>
#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>

#include "shill/connection_info.h"

namespace shill {

class ConnectionInfoReader {
 public:
  ConnectionInfoReader();
  ConnectionInfoReader(const ConnectionInfoReader&) = delete;
  ConnectionInfoReader& operator=(const ConnectionInfoReader&) = delete;

  virtual ~ConnectionInfoReader();

  // Returns the file path (/proc/net/ip_conntrack by default) from where
  // IP connection tracking information are read. Overloadded by unit tests
  // to return a different file path.
  virtual base::FilePath GetConnectionInfoFilePath() const;

  // Loads IP connection tracking information from the file path returned by
  // GetConnectionInfoFilePath(). Existing entries in |info_list| are always
  // discarded. Returns true on success.
  virtual bool LoadConnectionInfo(std::vector<ConnectionInfo>* info_list);

 private:
  FRIEND_TEST(ConnectionInfoReaderTest, ParseConnectionInfo);
  FRIEND_TEST(ConnectionInfoReaderTest, ParseIPAddress);
  FRIEND_TEST(ConnectionInfoReaderTest, ParsePort);
  FRIEND_TEST(ConnectionInfoReaderTest, ParseProtocol);
  FRIEND_TEST(ConnectionInfoReaderTest, ParseTimeToExpireSeconds);

  bool ParseConnectionInfo(const std::string& input, ConnectionInfo* info);
  bool ParseProtocol(const std::string& input, int* protocol);
  bool ParseTimeToExpireSeconds(const std::string& input,
                                int64_t* time_to_expire_seconds);
  bool ParseIsUnreplied(const std::string& input, bool* is_unreplied);
  bool ParseIPAddress(const std::string& input,
                      IPAddress* ip_address,
                      bool* is_source);
  bool ParsePort(const std::string& input, uint16_t* port, bool* is_source);
};

}  // namespace shill

#endif  // SHILL_CONNECTION_INFO_READER_H_
