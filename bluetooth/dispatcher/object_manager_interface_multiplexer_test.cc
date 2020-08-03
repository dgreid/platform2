// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <base/bind.h>
#include <base/test/task_environment.h>
#include <base/run_loop.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_manager.h>
#include <dbus/mock_object_proxy.h>
#include <dbus/property.h>
#include <gtest/gtest.h>

#include "bluetooth/dispatcher/mock_object_manager_interface_multiplexer.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Mock;
using ::testing::Return;

namespace bluetooth {

namespace {

constexpr char kTestServiceName1[] = "org.example.Service1";
constexpr char kTestServiceName2[] = "org.example.Service2";
constexpr char kTestInterfaceName[] = "org.example.Interface";
constexpr char kTestRootServicePath[] = "/org/example/Root";

}  // namespace

class ObjectManagerInterfaceMultiplexerTest : public ::testing::Test {
 public:
  void SetUp() override {
    bus_ = new dbus::MockBus(dbus::Bus::Options());
    EXPECT_CALL(*bus_, GetDBusTaskRunner())
        .WillRepeatedly(
            Return(task_environment_.GetMainThreadTaskRunner().get()));
    EXPECT_CALL(*bus_, AssertOnOriginThread()).Times(AnyNumber());
    EXPECT_CALL(*bus_, AssertOnDBusThread()).Times(AnyNumber());
    // For this test purposes it's okay to mock dbus::Bus::Connect() to return
    // false. This will make MockObjectManager fail its initialization but we
    // don't care about it in this test.
    EXPECT_CALL(*bus_, Connect()).WillRepeatedly(Return(false));

    dbus::ObjectPath root_service_path(kTestRootServicePath);
    object_proxy1_ = new dbus::MockObjectProxy(bus_.get(), kTestServiceName1,
                                               root_service_path);
    object_proxy2_ = new dbus::MockObjectProxy(bus_.get(), kTestServiceName2,
                                               root_service_path);
    EXPECT_CALL(*bus_, GetObjectProxy(kTestServiceName1, root_service_path))
        .WillOnce(Return(object_proxy1_.get()));
    EXPECT_CALL(*bus_, GetObjectProxy(kTestServiceName2, root_service_path))
        .WillOnce(Return(object_proxy2_.get()));
    object_manager1_ = new dbus::MockObjectManager(
        bus_.get(), kTestServiceName1, root_service_path);
    object_manager2_ = new dbus::MockObjectManager(
        bus_.get(), kTestServiceName2, root_service_path);
    // Force MessageLoop to run all pending tasks as an effect of instantiating
    // MockObjectManager. This is needed to avoid memory leak as pending tasks
    // hold pointers.
    base::RunLoop().RunUntilIdle();

    interface_multiplexer_ =
        std::make_unique<MockObjectManagerInterfaceMultiplexer>(
            kTestInterfaceName);
    interface_multiplexer_->RegisterToObjectManager(object_manager1_.get(),
                                                    kTestServiceName1);
    interface_multiplexer_->RegisterToObjectManager(object_manager2_.get(),
                                                    kTestServiceName2);
  }

 protected:
  ForwardingObjectManagerInterface* GetForwardingInterface(
      const std::string& service_name) {
    return interface_multiplexer_->object_manager_interfaces_
        .find(service_name)
        ->second.get();
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> object_proxy1_;
  scoped_refptr<dbus::MockObjectProxy> object_proxy2_;
  scoped_refptr<dbus::MockObjectManager> object_manager1_;
  scoped_refptr<dbus::MockObjectManager> object_manager2_;
  std::unique_ptr<MockObjectManagerInterfaceMultiplexer> interface_multiplexer_;
};

TEST_F(ObjectManagerInterfaceMultiplexerTest, Default) {}

// Tests that CreateProperties is forwarded to a multiplexer with the correct
// additional service name parameter.
TEST_F(ObjectManagerInterfaceMultiplexerTest, CreateProperties) {
  // Service 1
  scoped_refptr<dbus::MockObjectProxy> object_proxy1 =
      new dbus::MockObjectProxy(bus_.get(), kTestServiceName1,
                                dbus::ObjectPath(kTestRootServicePath));
  auto expected_property_set1 = std::make_unique<dbus::PropertySet>(
      object_proxy1.get(), kTestInterfaceName,
      base::Bind([](const std::string& property_name) {}));
  EXPECT_CALL(*interface_multiplexer_,
              CreateProperties(kTestServiceName1, object_proxy1.get(),
                               dbus::ObjectPath(kTestRootServicePath),
                               kTestInterfaceName))
      .WillOnce(Return(expected_property_set1.get()));
  dbus::PropertySet* property_set1 =
      GetForwardingInterface(kTestServiceName1)
          ->CreateProperties(object_proxy1.get(),
                             dbus::ObjectPath(kTestRootServicePath),
                             kTestInterfaceName);
  EXPECT_EQ(expected_property_set1.get(), property_set1);

  // Service 2
  scoped_refptr<dbus::MockObjectProxy> object_proxy2 =
      new dbus::MockObjectProxy(bus_.get(), kTestServiceName2,
                                dbus::ObjectPath(kTestRootServicePath));
  auto expected_property_set2 = std::make_unique<dbus::PropertySet>(
      object_proxy2.get(), kTestInterfaceName,
      base::Bind([](const std::string& property_name) {}));
  EXPECT_CALL(*interface_multiplexer_,
              CreateProperties(kTestServiceName2, object_proxy2.get(),
                               dbus::ObjectPath(kTestRootServicePath),
                               kTestInterfaceName))
      .WillOnce(Return(expected_property_set2.get()));
  dbus::PropertySet* property_set2 =
      GetForwardingInterface(kTestServiceName2)
          ->CreateProperties(object_proxy2.get(),
                             dbus::ObjectPath(kTestRootServicePath),
                             kTestInterfaceName);
  EXPECT_EQ(expected_property_set2.get(), property_set2);
}

// Tests that ObjectAdded is forwarded to a multiplexer with the correct
// additional service name parameter.
TEST_F(ObjectManagerInterfaceMultiplexerTest, ObjectAdded) {
  // Service 1
  EXPECT_CALL(
      *interface_multiplexer_,
      ObjectAdded(kTestServiceName1, dbus::ObjectPath(kTestRootServicePath),
                  kTestInterfaceName))
      .Times(1);
  GetForwardingInterface(kTestServiceName1)
      ->ObjectAdded(dbus::ObjectPath(kTestRootServicePath), kTestInterfaceName);

  // Service 2
  EXPECT_CALL(
      *interface_multiplexer_,
      ObjectAdded(kTestServiceName2, dbus::ObjectPath(kTestRootServicePath),
                  kTestInterfaceName))
      .Times(1);
  GetForwardingInterface(kTestServiceName2)
      ->ObjectAdded(dbus::ObjectPath(kTestRootServicePath), kTestInterfaceName);
}

// Tests that ObjectRemoved is forwarded to a multiplexer with the correct
// additional service name parameter.
TEST_F(ObjectManagerInterfaceMultiplexerTest, ObjectRemoved) {
  // Service 1
  EXPECT_CALL(
      *interface_multiplexer_,
      ObjectRemoved(kTestServiceName1, dbus::ObjectPath(kTestRootServicePath),
                    kTestInterfaceName))
      .Times(1);
  GetForwardingInterface(kTestServiceName1)
      ->ObjectRemoved(dbus::ObjectPath(kTestRootServicePath),
                      kTestInterfaceName);

  // Service 2
  EXPECT_CALL(
      *interface_multiplexer_,
      ObjectRemoved(kTestServiceName2, dbus::ObjectPath(kTestRootServicePath),
                    kTestInterfaceName))
      .Times(1);
  GetForwardingInterface(kTestServiceName2)
      ->ObjectRemoved(dbus::ObjectPath(kTestRootServicePath),
                      kTestInterfaceName);
}

}  // namespace bluetooth
