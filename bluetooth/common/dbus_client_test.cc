// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/common/dbus_client.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/test/task_environment.h>
#include <base/run_loop.h>
#include <base/strings/stringprintf.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;
using ::testing::SaveArg;

namespace bluetooth {

namespace {

constexpr char kTestClientAddress[] = ":1.2";
constexpr char kTestInterfaceName[] = "org.example.Interface";
constexpr char kTestMethodName[] = "SomeMethod";

constexpr char kDBusSystemObjectPath[] = "/org/freedesktop/DBus";
constexpr char kDBusSystemObjectInterface[] = "org.freedesktop.DBus";
constexpr char kDBusSystemObjectAddress[] = "org.freedesktop.DBus";
constexpr char kNameOwnerChangedMember[] = "NameOwnerChanged";

}  // namespace

class DBusClientTest : public ::testing::Test {
 public:
  void SetUp() override {
    bus_ = new dbus::MockBus(dbus::Bus::Options());
    EXPECT_CALL(*bus_, GetOriginTaskRunner())
        .WillRepeatedly(
            Return(task_environment_.GetMainThreadTaskRunner().get()));
    EXPECT_CALL(*bus_, AssertOnDBusThread()).Times(AnyNumber());
  }

  // Will be called when client becomes available. Keeps track the count this
  // happens so we can verify it's called.
  void OnClientUnavailable() { client_unavailable_callback_count_++; }

 protected:
  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  scoped_refptr<dbus::MockBus> bus_;

  int client_unavailable_callback_count_ = 0;
};

TEST_F(DBusClientTest, WatchClientUnavailable) {
  auto dbus_client = std::make_unique<DBusClient>(bus_, kTestClientAddress);

  DBusHandleMessageFunction handle_message;

  EXPECT_CALL(*bus_, AddFilterFunction(_, dbus_client.get()))
      .WillOnce(SaveArg<0>(&handle_message));
  std::string match_rule = base::StringPrintf(
      "type='signal',interface='%s',member='%s',path='%s',sender='%s',"
      "arg0='%s'",
      kDBusSystemObjectInterface, kNameOwnerChangedMember,
      kDBusSystemObjectPath, kDBusSystemObjectAddress, kTestClientAddress);
  EXPECT_CALL(*bus_, AddMatch(match_rule, _)).Times(1);
  dbus_client->WatchClientUnavailable(
      base::Bind(&DBusClientTest::OnClientUnavailable, base::Unretained(this)));

  // Other signals shouldn't trigger dbus_client unavailable callback.
  dbus::Signal signal_other(kTestInterfaceName, kTestMethodName);
  handle_message(nullptr, signal_other.raw_message(), dbus_client.get());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(0, client_unavailable_callback_count_);

  // NameOwnerChanged signal with the right parameters should trigger client
  // unavailable callback.
  dbus::Signal signal_name_owner_changed(kDBusSystemObjectInterface,
                                         kNameOwnerChangedMember);
  signal_name_owner_changed.SetPath(dbus::ObjectPath(kDBusSystemObjectPath));
  signal_name_owner_changed.SetSender(kDBusSystemObjectAddress);
  dbus::MessageWriter writer(&signal_name_owner_changed);
  writer.AppendString(kTestClientAddress);  // 1st arg = address
  writer.AppendString(kTestClientAddress);  // 2nd arg = old owner
  writer.AppendString("");                  // 3rd arg = new owner
  handle_message(nullptr, signal_name_owner_changed.raw_message(),
                 dbus_client.get());
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, client_unavailable_callback_count_);

  EXPECT_CALL(*bus_, RemoveMatch(match_rule, _)).Times(1);
  EXPECT_CALL(*bus_, RemoveFilterFunction(_, dbus_client.get()))
      .WillOnce(SaveArg<0>(&handle_message));
  dbus_client.reset();
}

}  // namespace bluetooth
