// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "buffet/notification/notification_parser.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "buffet/commands/unittest_utils.h"

using testing::Invoke;
using testing::_;

namespace buffet {

using unittests::CreateDictionaryValue;

class MockNotificationDelegate : public NotificationDelegate {
 public:
  MOCK_METHOD1(OnConnected, void(const std::string&));
  MOCK_METHOD0(OnDisconnected, void());
  MOCK_METHOD0(OnPermanentFailure, void());
  MOCK_METHOD1(OnCommandCreated, void(const base::DictionaryValue& command));
};

class NotificationParserTest : public ::testing::Test {
 protected:
  testing::StrictMock<MockNotificationDelegate> delegate_;
};

TEST_F(NotificationParserTest, CommandCreated) {
  auto json = CreateDictionaryValue(R"({
    "kind": "clouddevices#notification",
    "type": "COMMAND_CREATED",
    "deviceId": "device_id",
    "command": {
      "kind": "clouddevices#command",
      "deviceId": "device_id",
      "state": "queued",
      "name": "storage.list",
      "parameters": {
        "path": "/somepath1"
      },
      "expirationTimeMs": "1406036174811",
      "id": "command_id",
      "creationTimeMs": "1403444174811"
    },
    "commandId": "command_id"
  })");

  base::DictionaryValue command_instance;
  auto on_command = [&command_instance](const base::DictionaryValue& command) {
    command_instance.MergeDictionary(&command);
  };

  EXPECT_CALL(delegate_, OnCommandCreated(_)).WillOnce(Invoke(on_command));
  EXPECT_TRUE(ParseNotificationJson(*json, &delegate_));

  const char expected_json[] = R"({
      "kind": "clouddevices#command",
      "deviceId": "device_id",
      "state": "queued",
      "name": "storage.list",
      "parameters": {
        "path": "/somepath1"
      },
      "expirationTimeMs": "1406036174811",
      "id": "command_id",
      "creationTimeMs": "1403444174811"
    })";
  EXPECT_JSON_EQ(expected_json, command_instance);
}

TEST_F(NotificationParserTest, Failure_NoKind) {
  auto json = CreateDictionaryValue(R"({
    "type": "COMMAND_CREATED",
    "deviceId": "device_id",
    "command": {
      "kind": "clouddevices#command",
      "deviceId": "device_id",
      "state": "queued",
      "name": "storage.list",
      "parameters": {
        "path": "/somepath1"
      },
      "expirationTimeMs": "1406036174811",
      "id": "command_id",
      "creationTimeMs": "1403444174811"
    },
    "commandId": "command_id"
  })");

  EXPECT_FALSE(ParseNotificationJson(*json, &delegate_));
}

TEST_F(NotificationParserTest, Failure_NoType) {
  auto json = CreateDictionaryValue(R"({
    "kind": "clouddevices#notification",
    "deviceId": "device_id",
    "command": {
      "kind": "clouddevices#command",
      "deviceId": "device_id",
      "state": "queued",
      "name": "storage.list",
      "parameters": {
        "path": "/somepath1"
      },
      "expirationTimeMs": "1406036174811",
      "id": "command_id",
      "creationTimeMs": "1403444174811"
    },
    "commandId": "command_id"
  })");

  EXPECT_FALSE(ParseNotificationJson(*json, &delegate_));
}

TEST_F(NotificationParserTest, IgnoredNotificationType) {
  auto json = CreateDictionaryValue(R"({
    "kind": "clouddevices#notification",
    "type": "COMMAND_EXPIRED",
    "deviceId": "device_id",
    "command": {
      "kind": "clouddevices#command",
      "deviceId": "device_id",
      "state": "queued",
      "name": "storage.list",
      "parameters": {
        "path": "/somepath1"
      },
      "expirationTimeMs": "1406036174811",
      "id": "command_id",
      "creationTimeMs": "1403444174811"
    },
    "commandId": "command_id"
  })");

  EXPECT_TRUE(ParseNotificationJson(*json, &delegate_));
}

}  // namespace buffet
