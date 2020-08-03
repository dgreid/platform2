// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bluetooth/dispatcher/impersonation_object_manager_interface.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/test/task_environment.h>
#include <base/run_loop.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_manager.h>
#include <dbus/mock_object_proxy.h>
#include <brillo/dbus/exported_property_set.h>
#include <brillo/dbus/mock_dbus_object.h>
#include <brillo/dbus/mock_exported_object_manager.h>
#include <dbus/property.h>
#include <gtest/gtest.h>

#include "bluetooth/dispatcher/client_manager.h"
#include "bluetooth/dispatcher/mock_dbus_connection_factory.h"
#include "bluetooth/dispatcher/test_helper.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Mock;
using ::testing::Return;
using ::testing::SaveArg;

namespace bluetooth {

namespace {

constexpr char kTestInterfaceName1[] = "org.example.Interface1";
constexpr char kTestInterfaceName2[] = "org.example.Interface2";
constexpr char kTestObjectPath1[] = "/org/example/Object1";
constexpr char kTestObjectPath2[] = "/org/example/Object2";
constexpr char kTestObjectManagerPath[] = "/";
constexpr char kTestServiceName1[] = "org.example.Service1";
constexpr char kTestServiceName2[] = "org.example.Service2";
constexpr char kTestMethodName1[] = "Method1";
constexpr char kTestMethodName2[] = "Method2";

constexpr char kStringPropertyName[] = "SomeString";
constexpr char kIntPropertyName[] = "SomeInt";
constexpr char kBoolPropertyName[] = "SomeBool";
constexpr char kMergeStringPropertyName[] = "SomeStringForMerging";

constexpr char kTestMethodCallString[] = "The Method Call";
constexpr char kTestResponseString[] = "The Response";

constexpr char kTestSender[] = ":1.1";

constexpr int kTestSerial = 10;

constexpr int kTestIntPropertyValue = 7;
constexpr char kTestStringPropertyValue[] = "some property value";
constexpr bool kTestBoolPropertyValue = true;

}  // namespace

class TestInterfaceHandler : public InterfaceHandler {
 public:
  TestInterfaceHandler() {
    property_factory_map_[kStringPropertyName] =
        std::make_unique<PropertyFactory<std::string>>();
    property_factory_map_[kIntPropertyName] =
        std::make_unique<PropertyFactory<int>>();
    property_factory_map_[kBoolPropertyName] =
        std::make_unique<PropertyFactory<bool>>();
    property_factory_map_[kMergeStringPropertyName] =
        std::make_unique<PropertyFactory<std::string>>(
            MergingRule::CONCATENATION);

    method_forwardings_[kTestMethodName1] = ForwardingRule::FORWARD_DEFAULT;
    method_forwardings_[kTestMethodName2] = ForwardingRule::FORWARD_ALL;
  }

  explicit TestInterfaceHandler(ObjectExportRule object_export_rule)
      : TestInterfaceHandler() {
    object_export_rule_ = object_export_rule;
  }

  const PropertyFactoryMap& GetPropertyFactoryMap() const override {
    return property_factory_map_;
  }

  const std::map<std::string, ForwardingRule>& GetMethodForwardings()
      const override {
    return method_forwardings_;
  }

  ObjectExportRule GetObjectExportRule() const override {
    return object_export_rule_;
  }

 private:
  InterfaceHandler::PropertyFactoryMap property_factory_map_;
  std::map<std::string, ForwardingRule> method_forwardings_;
  ObjectExportRule object_export_rule_ = ObjectExportRule::ANY_SERVICE;
};

class ImpersonationObjectManagerInterfaceTest : public ::testing::Test {
 public:
  void SetUp() override {
    bus_ = new dbus::MockBus(dbus::Bus::Options());
    auto dbus_connection_factory =
        std::make_unique<MockDBusConnectionFactory>();
    dbus_connection_factory_ = dbus_connection_factory.get();
    client_manager_ = std::make_unique<ClientManager>(
        bus_, std::move(dbus_connection_factory));
    EXPECT_CALL(*bus_, GetDBusTaskRunner())
        .WillRepeatedly(
            Return(task_environment_.GetMainThreadTaskRunner().get()));
    EXPECT_CALL(*bus_, AssertOnOriginThread()).Times(AnyNumber());
    EXPECT_CALL(*bus_, AssertOnDBusThread()).Times(AnyNumber());
    EXPECT_CALL(*bus_, Connect()).WillRepeatedly(Return(false));
    EXPECT_CALL(*bus_, HasDBusThread()).WillRepeatedly(Return(false));

    dbus::ObjectPath object_manager_path(kTestObjectManagerPath);

    object_manager_object_proxy1_ = new dbus::MockObjectProxy(
        bus_.get(), kTestServiceName1, object_manager_path);
    EXPECT_CALL(*bus_, GetObjectProxy(kTestServiceName1, object_manager_path))
        .WillOnce(Return(object_manager_object_proxy1_.get()));
    object_manager_object_proxy2_ = new dbus::MockObjectProxy(
        bus_.get(), kTestServiceName2, object_manager_path);
    EXPECT_CALL(*bus_, GetObjectProxy(kTestServiceName2, object_manager_path))
        .WillOnce(Return(object_manager_object_proxy2_.get()));

    object_manager1_ = new dbus::MockObjectManager(
        bus_.get(), kTestServiceName1, object_manager_path);
    object_manager2_ = new dbus::MockObjectManager(
        bus_.get(), kTestServiceName2, object_manager_path);
    // Force MessageLoop to run pending tasks as effect of instantiating
    // MockObjectManager. Needed to avoid memory leaks because pending tasks
    // are unowned pointers that will only self destruct after being run.
    base::RunLoop().RunUntilIdle();
    auto exported_object_manager =
        std::make_unique<brillo::dbus_utils::MockExportedObjectManager>(
            bus_, object_manager_path);
    exported_object_manager_ = exported_object_manager.get();
    EXPECT_CALL(*exported_object_manager_, RegisterAsync(_)).Times(1);
    exported_object_manager_wrapper_ =
        std::make_unique<ExportedObjectManagerWrapper>(
            bus_, std::move(exported_object_manager));
    object_proxy_ = new dbus::MockObjectProxy(
        bus_.get(), kTestServiceName1, dbus::ObjectPath(kTestObjectPath1));
  }

  void StubHandlePropertiesSet(
      dbus::MethodCall* method_call,
      int timeout_ms,
      dbus::ObjectProxy::ResponseCallback* callback,
      dbus::ObjectProxy::ErrorCallback* error_callback) {
    StubHandleMethod(dbus::kPropertiesInterface, dbus::kPropertiesSet,
                     kTestMethodCallString, kTestResponseString,
                     /* error_name */ "", /* error_message */ "", method_call,
                     timeout_ms, callback, error_callback);
  }

  void StubHandleTestMethod1(dbus::MethodCall* method_call,
                             int timeout_ms,
                             dbus::ObjectProxy::ResponseCallback* callback,
                             dbus::ObjectProxy::ErrorCallback* error_callback) {
    StubHandleMethod(kTestInterfaceName1, kTestMethodName1,
                     kTestMethodCallString, kTestResponseString,
                     /* error_name */ "", /* error_message */ "", method_call,
                     timeout_ms, callback, error_callback);
  }

  void StubHandleTestMethod2(dbus::MethodCall* method_call,
                             int timeout_ms,
                             dbus::ObjectProxy::ResponseCallback* callback,
                             dbus::ObjectProxy::ErrorCallback* error_callback) {
    StubHandleMethod(kTestInterfaceName1, kTestMethodName2,
                     kTestMethodCallString, kTestResponseString,
                     /* error_name */ "", /* error_message */ "", method_call,
                     timeout_ms, callback, error_callback);
  }

 protected:
  // Expects that |exported_object| exports the standard methods:
  // Get/Set/GetAll/PropertiesChanged.
  // Optionally the Set handler will be assigned if |set_method_handler| is
  // provided.
  void ExpectExportPropertiesMethods(
      dbus::MockExportedObject* exported_object,
      dbus::ExportedObject::MethodCallCallback* set_method_handler = nullptr) {
    EXPECT_CALL(*exported_object,
                ExportMethodAndBlock(dbus::kPropertiesInterface,
                                     dbus::kPropertiesGet, _))
        .WillOnce(Return(true));

    EXPECT_CALL(*exported_object,
                ExportMethodAndBlock(dbus::kPropertiesInterface,
                                     dbus::kPropertiesGetAll, _))
        .WillOnce(Return(true));

    if (set_method_handler == nullptr)
      set_method_handler = &dummy_method_handler_;
    EXPECT_CALL(*exported_object,
                ExportMethodAndBlock(dbus::kPropertiesInterface,
                                     dbus::kPropertiesSet, _))
        .WillOnce(DoAll(SaveArg<2>(set_method_handler), Return(true)));
  }

  void TestMethodForwarding(
      const std::string& interface_name,
      const std::string& method_name,
      const dbus::ObjectPath object_path,
      scoped_refptr<dbus::MockBus> forwarding_bus,
      dbus::ExportedObject::MethodCallCallback* tested_method_handler,
      void (ImpersonationObjectManagerInterfaceTest::*stub_method_handler)(
          dbus::MethodCall*,
          int,
          dbus::ObjectProxy::ResponseCallback*,
          dbus::ObjectProxy::ErrorCallback*)) {
    scoped_refptr<dbus::MockObjectProxy> object_proxy1 =
        new dbus::MockObjectProxy(forwarding_bus.get(), kTestServiceName1,
                                  object_path);
    EXPECT_CALL(*forwarding_bus, GetObjectProxy(kTestServiceName1, object_path))
        .WillOnce(Return(object_proxy1.get()));
    EXPECT_CALL(*forwarding_bus, Connect()).WillRepeatedly(Return(true));
    dbus::MethodCall method_call(interface_name, method_name);
    method_call.SetPath(object_path);
    method_call.SetSender(kTestSender);
    method_call.SetSerial(kTestSerial);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(kTestMethodCallString);
    EXPECT_CALL(*object_proxy1, DoCallMethodWithErrorCallback(_, _, _, _))
        .WillOnce(Invoke(this, stub_method_handler));
    std::unique_ptr<dbus::Response> saved_response;
    tested_method_handler->Run(
        &method_call, base::Bind(
                          [](std::unique_ptr<dbus::Response>* saved_response,
                             std::unique_ptr<dbus::Response> response) {
                            *saved_response = std::move(response);
                          },
                          &saved_response));
    EXPECT_TRUE(saved_response.get() != nullptr);
    std::string saved_response_string;
    dbus::MessageReader reader(saved_response.get());
    reader.PopString(&saved_response_string);
    // Check that the response is the forwarded response of the stub method
    // handler.
    EXPECT_EQ(kTestSender, saved_response->GetDestination());
    EXPECT_EQ(kTestSerial, saved_response->GetReplySerial());
    EXPECT_EQ(kTestResponseString, saved_response_string);
  }

  void TestMethodForwardingMultiService(
      const std::string& interface_name,
      const std::string& method_name,
      const dbus::ObjectPath object_path,
      scoped_refptr<dbus::MockBus> forwarding_bus,
      dbus::ExportedObject::MethodCallCallback* tested_method_handler,
      void (ImpersonationObjectManagerInterfaceTest::*stub_method_handler)(
          dbus::MethodCall*,
          int,
          dbus::ObjectProxy::ResponseCallback*,
          dbus::ObjectProxy::ErrorCallback*)) {
    scoped_refptr<dbus::MockObjectProxy> object_proxy1 =
        new dbus::MockObjectProxy(forwarding_bus.get(), kTestServiceName1,
                                  object_path);
    scoped_refptr<dbus::MockObjectProxy> object_proxy2 =
        new dbus::MockObjectProxy(forwarding_bus.get(), kTestServiceName2,
                                  object_path);
    EXPECT_CALL(*forwarding_bus, GetObjectProxy(kTestServiceName1, object_path))
        .WillOnce(Return(object_proxy1.get()));
    EXPECT_CALL(*forwarding_bus, GetObjectProxy(kTestServiceName2, object_path))
        .WillOnce(Return(object_proxy2.get()));
    EXPECT_CALL(*forwarding_bus, Connect()).WillRepeatedly(Return(true));
    dbus::MethodCall method_call(interface_name, method_name);
    method_call.SetPath(object_path);
    method_call.SetSender(kTestSender);
    method_call.SetSerial(kTestSerial);
    dbus::MessageWriter writer(&method_call);
    writer.AppendString(kTestMethodCallString);

    // Check that the object proxies of both services receive method forwarding.
    EXPECT_CALL(*object_proxy1, DoCallMethodWithErrorCallback(_, _, _, _))
        .WillOnce(Invoke(this, stub_method_handler));
    EXPECT_CALL(*object_proxy2, DoCallMethodWithErrorCallback(_, _, _, _))
        .WillOnce(Invoke(this, stub_method_handler));

    std::unique_ptr<dbus::Response> saved_response;
    tested_method_handler->Run(
        &method_call, base::Bind(
                          [](std::unique_ptr<dbus::Response>* saved_response,
                             std::unique_ptr<dbus::Response> response) {
                            *saved_response = std::move(response);
                          },
                          &saved_response));
    EXPECT_TRUE(saved_response.get() != nullptr);
    std::string saved_response_string;
    dbus::MessageReader reader(saved_response.get());
    reader.PopString(&saved_response_string);
    // Check that the response is the forwarded response of the stub method
    // handler.
    EXPECT_EQ(kTestSender, saved_response->GetDestination());
    EXPECT_EQ(kTestSerial, saved_response->GetReplySerial());
    EXPECT_EQ(kTestResponseString, saved_response_string);
  }

  // Expects that |exported_object| exports the test methods.
  // Optionally the method handlers will be assigned if |method1_handler| or
  // |method2_handler| is not null.
  void ExpectExportTestMethods(
      dbus::MockExportedObject* exported_object,
      const std::string& interface_name,
      dbus::ExportedObject::MethodCallCallback* method1_handler = nullptr,
      dbus::ExportedObject::MethodCallCallback* method2_handler = nullptr) {
    if (method1_handler == nullptr)
      method1_handler = &dummy_method_handler_;
    EXPECT_CALL(*exported_object,
                ExportMethodAndBlock(interface_name, kTestMethodName1, _))
        .WillOnce(DoAll(SaveArg<2>(method1_handler), Return(true)));

    if (method2_handler == nullptr)
      method2_handler = &dummy_method_handler_;
    EXPECT_CALL(*exported_object,
                ExportMethodAndBlock(interface_name, kTestMethodName2, _))
        .WillOnce(DoAll(SaveArg<2>(method2_handler), Return(true)));
  }

  void ExpectUnexportTestMethods(dbus::MockExportedObject* exported_object,
                                 const std::string& interface_name) {
    EXPECT_CALL(*exported_object,
                UnexportMethodAndBlock(interface_name, kTestMethodName1))
        .WillOnce(Return(true));
    EXPECT_CALL(*exported_object,
                UnexportMethodAndBlock(interface_name, kTestMethodName2))
        .WillOnce(Return(true));
  }

  template <typename T>
  void SetPropertyValue(dbus::PropertyBase* property_base, T value) {
    dbus::Property<T>* property =
        static_cast<dbus::Property<T>*>(property_base);
    property->SetAndBlock(value);
    property->ReplaceValueWithSetValue();
    property->set_valid(true);
  }

  template <typename T>
  void ExpectExportedPropertyEquals(
      T value,
      brillo::dbus_utils::ExportedPropertyBase* exported_property_base) {
    ASSERT_TRUE(exported_property_base != nullptr);
    brillo::dbus_utils::ExportedProperty<T>* exported_property =
        static_cast<brillo::dbus_utils::ExportedProperty<T>*>(
            exported_property_base);
    EXPECT_EQ(value, exported_property->value());
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::ThreadingMode::MAIN_THREAD_ONLY};
  scoped_refptr<dbus::MockBus> bus_;
  scoped_refptr<dbus::MockObjectProxy> object_manager_object_proxy1_;
  scoped_refptr<dbus::MockObjectProxy> object_manager_object_proxy2_;
  scoped_refptr<dbus::MockObjectProxy> object_proxy_;
  scoped_refptr<dbus::MockObjectManager> object_manager1_;
  scoped_refptr<dbus::MockObjectManager> object_manager2_;
  std::unique_ptr<ExportedObjectManagerWrapper>
      exported_object_manager_wrapper_;
  brillo::dbus_utils::MockExportedObjectManager* exported_object_manager_;
  std::unique_ptr<ClientManager> client_manager_;
  MockDBusConnectionFactory* dbus_connection_factory_;
  dbus::ExportedObject::MethodCallCallback dummy_method_handler_;
};

TEST_F(ImpersonationObjectManagerInterfaceTest, SingleInterface) {
  dbus::ObjectPath object_path1(kTestObjectPath1);
  dbus::ObjectPath object_path2(kTestObjectPath2);

  std::map<std::string, std::unique_ptr<ExportedObject>> exported_objects;
  auto impersonation_om_interface =
      std::make_unique<ImpersonationObjectManagerInterface>(
          bus_, exported_object_manager_wrapper_.get(),
          std::make_unique<TestInterfaceHandler>(), kTestInterfaceName1,
          client_manager_.get());

  EXPECT_CALL(*object_manager1_, RegisterInterface(kTestInterfaceName1, _));
  impersonation_om_interface->RegisterToObjectManager(object_manager1_.get(),
                                                      kTestServiceName1);

  scoped_refptr<dbus::MockExportedObject> exported_object1 =
      new dbus::MockExportedObject(bus_.get(), object_path1);
  EXPECT_CALL(*bus_, GetExportedObject(object_path1))
      .WillOnce(Return(exported_object1.get()));
  scoped_refptr<dbus::MockExportedObject> exported_object2 =
      new dbus::MockExportedObject(bus_.get(), object_path2);
  EXPECT_CALL(*bus_, GetExportedObject(object_path2))
      .WillOnce(Return(exported_object2.get()));

  // D-Bus properties methods should be exported.
  ExpectExportPropertiesMethods(exported_object1.get());
  // CreateProperties called for an object.
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, dbus::kPropertiesInterface, _))
      .Times(1);
  std::unique_ptr<dbus::PropertySet> dbus_property_set1(
      impersonation_om_interface->CreateProperties(
          kTestServiceName1, object_proxy_.get(), object_path1,
          kTestInterfaceName1));
  PropertySet* property_set1 =
      static_cast<PropertySet*>(dbus_property_set1.get());
  // The properties should all be registered.
  ASSERT_NE(nullptr, property_set1->GetProperty(kStringPropertyName));
  ASSERT_NE(nullptr, property_set1->GetProperty(kIntPropertyName));
  ASSERT_NE(nullptr, property_set1->GetProperty(kBoolPropertyName));

  // D-Bus properties methods should be exported.
  ExpectExportPropertiesMethods(exported_object2.get());
  // CreateProperties called for another object.
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path2, dbus::kPropertiesInterface, _))
      .Times(1);
  std::unique_ptr<dbus::PropertySet> dbus_property_set2(
      impersonation_om_interface->CreateProperties(
          kTestServiceName1, object_proxy_.get(), object_path2,
          kTestInterfaceName1));
  PropertySet* property_set2 =
      static_cast<PropertySet*>(dbus_property_set2.get());

  // The properties should all be registered.
  ASSERT_NE(nullptr, property_set2->GetProperty(kStringPropertyName));
  ASSERT_NE(nullptr, property_set2->GetProperty(kIntPropertyName));
  ASSERT_NE(nullptr, property_set2->GetProperty(kBoolPropertyName));

  // ObjectAdded events
  ExpectExportTestMethods(exported_object1.get(), kTestInterfaceName1);
  ExpectExportTestMethods(exported_object2.get(), kTestInterfaceName1);
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, kTestInterfaceName1, _))
      .Times(1);
  impersonation_om_interface->ObjectAdded(kTestServiceName1, object_path1,
                                          kTestInterfaceName1);
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path2, kTestInterfaceName1, _))
      .Times(1);
  impersonation_om_interface->ObjectAdded(kTestServiceName1, object_path2,
                                          kTestInterfaceName1);

  // ObjectRemoved events
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path1, dbus::kPropertiesInterface))
      .Times(1);
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path1, kTestInterfaceName1))
      .Times(1);
  ExpectUnexportTestMethods(exported_object1.get(), kTestInterfaceName1);
  EXPECT_CALL(*exported_object1, Unregister()).Times(1);
  impersonation_om_interface->ObjectRemoved(kTestServiceName1, object_path1,
                                            kTestInterfaceName1);
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path2, dbus::kPropertiesInterface))
      .Times(1);
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path2, kTestInterfaceName1))
      .Times(1);
  ExpectUnexportTestMethods(exported_object2.get(), kTestInterfaceName1);
  EXPECT_CALL(*exported_object2, Unregister()).Times(1);
  impersonation_om_interface->ObjectRemoved(kTestServiceName1, object_path2,
                                            kTestInterfaceName1);
}

TEST_F(ImpersonationObjectManagerInterfaceTest, MultipleInterfaces) {
  dbus::ObjectPath object_path(kTestObjectPath1);

  scoped_refptr<dbus::MockExportedObject> exported_object =
      new dbus::MockExportedObject(bus_.get(), object_path);
  EXPECT_CALL(*bus_, GetExportedObject(object_path))
      .WillOnce(Return(exported_object.get()));

  std::map<std::string, std::unique_ptr<ExportedObject>> exported_objects;
  auto impersonation_om_interface1 =
      std::make_unique<ImpersonationObjectManagerInterface>(
          bus_, exported_object_manager_wrapper_.get(),
          std::make_unique<TestInterfaceHandler>(), kTestInterfaceName1,
          client_manager_.get());
  auto impersonation_om_interface2 =
      std::make_unique<ImpersonationObjectManagerInterface>(
          bus_, exported_object_manager_wrapper_.get(),
          std::make_unique<TestInterfaceHandler>(), kTestInterfaceName2,
          client_manager_.get());

  EXPECT_CALL(*object_manager1_, RegisterInterface(kTestInterfaceName1, _));
  EXPECT_CALL(*object_manager1_, RegisterInterface(kTestInterfaceName2, _));
  impersonation_om_interface1->RegisterToObjectManager(object_manager1_.get(),
                                                       kTestServiceName1);
  impersonation_om_interface2->RegisterToObjectManager(object_manager1_.get(),
                                                       kTestServiceName1);

  // D-Bus properties methods should be exported.
  ExpectExportPropertiesMethods(exported_object.get());
  // CreateProperties called for an object.
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path, dbus::kPropertiesInterface, _))
      .Times(1);
  std::unique_ptr<dbus::PropertySet> dbus_property_set1(
      impersonation_om_interface1->CreateProperties(
          kTestServiceName1, object_proxy_.get(), object_path,
          kTestInterfaceName1));
  PropertySet* property_set1 =
      static_cast<PropertySet*>(dbus_property_set1.get());

  std::unique_ptr<dbus::PropertySet> dbus_property_set2(
      impersonation_om_interface2->CreateProperties(
          kTestServiceName1, object_proxy_.get(), object_path,
          kTestInterfaceName2));
  PropertySet* property_set2 =
      static_cast<PropertySet*>(dbus_property_set2.get());

  // The properties should all be registered.
  ASSERT_NE(nullptr, property_set1->GetProperty(kStringPropertyName));
  ASSERT_NE(nullptr, property_set1->GetProperty(kIntPropertyName));
  ASSERT_NE(nullptr, property_set1->GetProperty(kBoolPropertyName));
  ASSERT_NE(nullptr, property_set2->GetProperty(kStringPropertyName));
  ASSERT_NE(nullptr, property_set2->GetProperty(kIntPropertyName));
  ASSERT_NE(nullptr, property_set2->GetProperty(kBoolPropertyName));

  // ObjectAdded events
  ExpectExportTestMethods(exported_object.get(), kTestInterfaceName1);
  ExpectExportTestMethods(exported_object.get(), kTestInterfaceName2);
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path, kTestInterfaceName1, _))
      .Times(1);
  impersonation_om_interface1->ObjectAdded(kTestServiceName1, object_path,
                                           kTestInterfaceName1);
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path, kTestInterfaceName2, _))
      .Times(1);
  impersonation_om_interface2->ObjectAdded(kTestServiceName1, object_path,
                                           kTestInterfaceName2);

  // ObjectRemoved events
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path, kTestInterfaceName1))
      .Times(1);
  // Exported object shouldn't be unregistered until the last interface is
  // removed.
  ExpectUnexportTestMethods(exported_object.get(), kTestInterfaceName1);
  EXPECT_CALL(*exported_object, Unregister()).Times(0);
  impersonation_om_interface1->ObjectRemoved(kTestServiceName1, object_path,
                                             kTestInterfaceName1);

  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path, dbus::kPropertiesInterface))
      .Times(1);
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path, kTestInterfaceName2))
      .Times(1);
  // Now that the last interface has been removed, exported object should be
  // unregistered.
  ExpectUnexportTestMethods(exported_object.get(), kTestInterfaceName2);
  EXPECT_CALL(*exported_object, Unregister()).Times(1);
  impersonation_om_interface2->ObjectRemoved(kTestServiceName1, object_path,
                                             kTestInterfaceName2);

  // Make sure that the Unregister actually happens on ObjectRemoved above and
  // not due to its automatic deletion when this test case finishes.
  Mock::VerifyAndClearExpectations(exported_object.get());
}

TEST_F(ImpersonationObjectManagerInterfaceTest, UnexpectedEvents) {
  dbus::ObjectPath object_path(kTestObjectPath1);

  scoped_refptr<dbus::MockExportedObject> exported_object =
      new dbus::MockExportedObject(bus_.get(), object_path);
  EXPECT_CALL(*bus_, GetExportedObject(object_path))
      .WillOnce(Return(exported_object.get()));

  std::map<std::string, std::unique_ptr<ExportedObject>> exported_objects;
  auto impersonation_om_interface =
      std::make_unique<ImpersonationObjectManagerInterface>(
          bus_, exported_object_manager_wrapper_.get(),
          std::make_unique<TestInterfaceHandler>(), kTestInterfaceName1,
          client_manager_.get());

  impersonation_om_interface->RegisterToObjectManager(object_manager1_.get(),
                                                      kTestServiceName1);

  // ObjectAdded event happens before CreateProperties. This shouldn't happen.
  // Make sure we only ignore the event and don't crash if this happens.
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path, dbus::kPropertiesInterface, _))
      .Times(1);
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path, kTestInterfaceName1, _))
      .Times(0);
  impersonation_om_interface->ObjectAdded(kTestServiceName1, object_path,
                                          kTestInterfaceName1);

  // ObjectRemoved event happens before CreateProperties. This shouldn't happen.
  // Make sure we only ignore the event and don't crash if this happens.
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path, dbus::kPropertiesInterface))
      .Times(0);
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path, kTestInterfaceName1))
      .Times(0);
  EXPECT_CALL(*exported_object, Unregister()).Times(0);
  impersonation_om_interface->ObjectRemoved(kTestServiceName1, object_path,
                                            kTestInterfaceName1);

  // D-Bus properties methods should be exported.
  ExpectExportPropertiesMethods(exported_object.get());
  // CreateProperties called for an object.
  std::unique_ptr<dbus::PropertySet> dbus_property_set(
      impersonation_om_interface->CreateProperties(
          kTestServiceName1, object_proxy_.get(), object_path,
          kTestInterfaceName1));
  PropertySet* property_set =
      static_cast<PropertySet*>(dbus_property_set.get());

  // The properties should all be registered.
  ASSERT_NE(nullptr, property_set->GetProperty(kStringPropertyName));
  ASSERT_NE(nullptr, property_set->GetProperty(kIntPropertyName));
  ASSERT_NE(nullptr, property_set->GetProperty(kBoolPropertyName));

  // ObjectRemoved event happens before ObjectAdded. This shouldn't happen.
  // Make sure we still handle this gracefully.
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path, dbus::kPropertiesInterface))
      .Times(1);
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path, kTestInterfaceName1))
      .Times(0);
  EXPECT_CALL(*exported_object, Unregister()).Times(1);
  impersonation_om_interface->ObjectRemoved(kTestServiceName1, object_path,
                                            kTestInterfaceName1);

  // Make sure that the Unregister actually happens on ObjectRemoved above and
  // not due to its automatic deletion when this test case finishes.
  Mock::VerifyAndClearExpectations(exported_object.get());
}

TEST_F(ImpersonationObjectManagerInterfaceTest, PropertiesHandler) {
  dbus::ObjectPath object_path1(kTestObjectPath1);

  scoped_refptr<dbus::MockExportedObject> exported_object1 =
      new dbus::MockExportedObject(bus_.get(), object_path1);
  EXPECT_CALL(*bus_, GetExportedObject(object_path1))
      .WillOnce(Return(exported_object1.get()));

  auto impersonation_om_interface =
      std::make_unique<ImpersonationObjectManagerInterface>(
          bus_, exported_object_manager_wrapper_.get(),
          std::make_unique<TestInterfaceHandler>(), kTestInterfaceName1,
          client_manager_.get());

  EXPECT_CALL(*object_manager1_, RegisterInterface(kTestInterfaceName1, _));
  impersonation_om_interface->RegisterToObjectManager(object_manager1_.get(),
                                                      kTestServiceName1);

  dbus::ExportedObject::MethodCallCallback set_method_handler;

  // D-Bus properties methods should be exported.
  ExpectExportPropertiesMethods(exported_object1.get(), &set_method_handler);
  // CreateProperties called for another object.
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, dbus::kPropertiesInterface, _))
      .Times(1);
  std::unique_ptr<dbus::PropertySet> dbus_property_set1(
      impersonation_om_interface->CreateProperties(
          kTestServiceName1, object_proxy_.get(), object_path1,
          kTestInterfaceName1));
  PropertySet* property_set1 =
      static_cast<PropertySet*>(dbus_property_set1.get());
  ASSERT_FALSE(set_method_handler.is_null());

  // The properties should all be registered.
  ASSERT_TRUE(property_set1->GetProperty(kStringPropertyName) != nullptr);
  ASSERT_TRUE(property_set1->GetProperty(kIntPropertyName) != nullptr);
  ASSERT_TRUE(property_set1->GetProperty(kBoolPropertyName) != nullptr);

  // Test that Properties.Set handler should forward the message to the source
  // service and forward the response back to the caller.
  TestMethodForwarding(
      dbus::kPropertiesInterface, dbus::kPropertiesSet, object_path1, bus_,
      &set_method_handler,
      &ImpersonationObjectManagerInterfaceTest::StubHandlePropertiesSet);

  // ObjectRemoved events
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path1, dbus::kPropertiesInterface))
      .Times(1);
  EXPECT_CALL(*exported_object1, Unregister()).Times(1);
  impersonation_om_interface->ObjectRemoved(kTestServiceName1, object_path1,
                                            kTestInterfaceName1);
}

TEST_F(ImpersonationObjectManagerInterfaceTest, MethodHandler) {
  dbus::ObjectPath object_path1(kTestObjectPath1);

  scoped_refptr<dbus::MockExportedObject> exported_object1 =
      new dbus::MockExportedObject(bus_.get(), object_path1);
  EXPECT_CALL(*bus_, GetExportedObject(object_path1))
      .WillOnce(Return(exported_object1.get()));

  auto impersonation_om_interface =
      std::make_unique<ImpersonationObjectManagerInterface>(
          bus_, exported_object_manager_wrapper_.get(),
          std::make_unique<TestInterfaceHandler>(), kTestInterfaceName1,
          client_manager_.get());

  EXPECT_CALL(*object_manager1_, RegisterInterface(kTestInterfaceName1, _));
  impersonation_om_interface->RegisterToObjectManager(object_manager1_.get(),
                                                      kTestServiceName1);

  // D-Bus properties methods should be exported.
  ExpectExportPropertiesMethods(exported_object1.get());
  // CreateProperties called for another object.
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, dbus::kPropertiesInterface, _))
      .Times(1);
  std::unique_ptr<dbus::PropertySet> dbus_property_set1(
      impersonation_om_interface->CreateProperties(
          kTestServiceName1, object_proxy_.get(), object_path1,
          kTestInterfaceName1));

  // Method forwarding
  dbus::ExportedObject::MethodCallCallback method1_handler;
  dbus::ExportedObject::MethodCallCallback method2_handler;
  ExpectExportTestMethods(exported_object1.get(), kTestInterfaceName1,
                          &method1_handler, &method2_handler);
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, kTestInterfaceName1, _))
      .Times(1);
  impersonation_om_interface->ObjectAdded(kTestServiceName1, object_path1,
                                          kTestInterfaceName1);
  scoped_refptr<dbus::MockBus> client_bus =
      new dbus::MockBus(dbus::Bus::Options());
  EXPECT_CALL(*dbus_connection_factory_, GetNewBus())
      .WillOnce(Return(client_bus));
  // Test that method call should be forwarding to the source service via
  // |client_bus|.
  TestMethodForwarding(
      kTestInterfaceName1, kTestMethodName1, object_path1, client_bus,
      &method1_handler,
      &ImpersonationObjectManagerInterfaceTest::StubHandleTestMethod1);
  TestMethodForwarding(
      kTestInterfaceName1, kTestMethodName2, object_path1, client_bus,
      &method2_handler,
      &ImpersonationObjectManagerInterfaceTest::StubHandleTestMethod2);

  // ObjectRemoved events
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path1, dbus::kPropertiesInterface))
      .Times(1);
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path1, kTestInterfaceName1))
      .Times(1);
  ExpectUnexportTestMethods(exported_object1.get(), kTestInterfaceName1);
  EXPECT_CALL(*exported_object1, Unregister()).Times(1);
  impersonation_om_interface->ObjectRemoved(kTestServiceName1, object_path1,
                                            kTestInterfaceName1);
}

TEST_F(ImpersonationObjectManagerInterfaceTest, MultiService) {
  dbus::ObjectPath object_path1(kTestObjectPath1);

  std::map<std::string, std::unique_ptr<ExportedObject>> exported_objects;
  auto impersonation_om_interface =
      std::make_unique<ImpersonationObjectManagerInterface>(
          bus_, exported_object_manager_wrapper_.get(),
          std::make_unique<TestInterfaceHandler>(), kTestInterfaceName1,
          client_manager_.get());

  impersonation_om_interface->RegisterToObjectManager(object_manager1_.get(),
                                                      kTestServiceName1);
  impersonation_om_interface->RegisterToObjectManager(object_manager2_.get(),
                                                      kTestServiceName2);

  scoped_refptr<dbus::MockExportedObject> exported_object1 =
      new dbus::MockExportedObject(bus_.get(), object_path1);
  EXPECT_CALL(*bus_, GetExportedObject(object_path1))
      .WillOnce(Return(exported_object1.get()));

  // D-Bus properties methods should be exported.
  ExpectExportPropertiesMethods(exported_object1.get());
  // CreateProperties called for an object.
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, dbus::kPropertiesInterface, _))
      .Times(1);
  std::unique_ptr<dbus::PropertySet> dbus_property_set1(
      impersonation_om_interface->CreateProperties(
          kTestServiceName1, object_proxy_.get(), object_path1,
          kTestInterfaceName1));
  PropertySet* property_set1 =
      static_cast<PropertySet*>(dbus_property_set1.get());
  // The properties should all be registered.
  ASSERT_NE(nullptr, property_set1->GetProperty(kStringPropertyName));
  ASSERT_NE(nullptr, property_set1->GetProperty(kIntPropertyName));
  ASSERT_NE(nullptr, property_set1->GetProperty(kBoolPropertyName));
  EXPECT_CALL(*object_manager1_,
              GetProperties(object_path1, kTestInterfaceName1))
      .WillRepeatedly(Return(property_set1));
  SetPropertyValue<std::string>(property_set1->GetProperty(kStringPropertyName),
                                kTestStringPropertyValue);
  SetPropertyValue<int>(property_set1->GetProperty(kIntPropertyName),
                        kTestIntPropertyValue);
  // Trigger property changed event and check that the exported properties are
  // updated.
  property_set1->NotifyPropertyChanged(kStringPropertyName);
  property_set1->NotifyPropertyChanged(kIntPropertyName);
  property_set1->NotifyPropertyChanged(kBoolPropertyName);
  ExportedInterface* interface =
      exported_object_manager_wrapper_->GetExportedInterface(
          object_path1, kTestInterfaceName1);
  ExpectExportedPropertyEquals<std::string>(
      kTestStringPropertyValue,
      interface->GetRegisteredExportedProperty(kStringPropertyName));
  ExpectExportedPropertyEquals<int>(
      kTestIntPropertyValue,
      interface->GetRegisteredExportedProperty(kIntPropertyName));
  EXPECT_EQ(nullptr,
            interface->GetRegisteredExportedProperty(kBoolPropertyName));

  // Another service also triggers CreateProperties for the same object.
  // This shouldn't trigger another object export since it's already exported
  // when the first service triggered CreateProperties.
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, dbus::kPropertiesInterface, _))
      .Times(0);
  std::unique_ptr<dbus::PropertySet> dbus_property_set2(
      impersonation_om_interface->CreateProperties(
          kTestServiceName2, object_proxy_.get(), object_path1,
          kTestInterfaceName1));
  PropertySet* property_set2 =
      static_cast<PropertySet*>(dbus_property_set2.get());
  // The properties should all be registered.
  ASSERT_NE(nullptr, property_set2->GetProperty(kStringPropertyName));
  ASSERT_NE(nullptr, property_set2->GetProperty(kIntPropertyName));
  ASSERT_NE(nullptr, property_set2->GetProperty(kBoolPropertyName));
  EXPECT_CALL(*object_manager2_,
              GetProperties(object_path1, kTestInterfaceName1))
      .WillRepeatedly(Return(property_set2));
  SetPropertyValue<int>(property_set2->GetProperty(kIntPropertyName),
                        kTestIntPropertyValue + 1);
  SetPropertyValue<bool>(property_set2->GetProperty(kBoolPropertyName),
                         kTestBoolPropertyValue);

  // ObjectAdded events
  dbus::ExportedObject::MethodCallCallback method1_handler;
  dbus::ExportedObject::MethodCallCallback method2_handler;
  ExpectExportTestMethods(exported_object1.get(), kTestInterfaceName1,
                          &method1_handler, &method2_handler);
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, kTestInterfaceName1, _))
      .Times(1);
  impersonation_om_interface->ObjectAdded(kTestServiceName1, object_path1,
                                          kTestInterfaceName1);
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, kTestInterfaceName1, _))
      .Times(0);
  impersonation_om_interface->ObjectAdded(kTestServiceName2, object_path1,
                                          kTestInterfaceName1);

  // Method forwarding
  scoped_refptr<dbus::MockBus> client_bus =
      new dbus::MockBus(dbus::Bus::Options());
  EXPECT_CALL(*client_bus, AssertOnDBusThread()).Times(AnyNumber());
  EXPECT_CALL(*dbus_connection_factory_, GetNewBus())
      .WillOnce(Return(client_bus));

  // Method1's rule is FORWARD_DEFAULT, so test that only Service1 receives
  // method forwarding.
  TestMethodForwarding(
      kTestInterfaceName1, kTestMethodName1, object_path1, client_bus,
      &method1_handler,
      &ImpersonationObjectManagerInterfaceTest::StubHandleTestMethod1);
  // Method2's rule is FORWARD_ALL, so test that all services receive method
  // forwarding.
  TestMethodForwardingMultiService(
      kTestInterfaceName1, kTestMethodName2, object_path1, client_bus,
      &method2_handler,
      &ImpersonationObjectManagerInterfaceTest::StubHandleTestMethod2);

  // ObjectRemoved events
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path1, dbus::kPropertiesInterface))
      .Times(1);
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path1, kTestInterfaceName1))
      .Times(1);
  impersonation_om_interface->ObjectRemoved(kTestServiceName1, object_path1,
                                            kTestInterfaceName1);
  // Service1 removed the object, so now the properties of this object should
  // be updated based on the properties of Service2.
  EXPECT_EQ(nullptr,
            interface->GetRegisteredExportedProperty(kStringPropertyName));
  ExpectExportedPropertyEquals<int>(
      kTestIntPropertyValue + 1,
      interface->GetRegisteredExportedProperty(kIntPropertyName));
  ExpectExportedPropertyEquals<bool>(
      kTestBoolPropertyValue,
      interface->GetRegisteredExportedProperty(kBoolPropertyName));

  // The last service removes the object, the corrseponding exported object
  // should be unregistered.
  ExpectUnexportTestMethods(exported_object1.get(), kTestInterfaceName1);
  EXPECT_CALL(*exported_object1, Unregister()).Times(1);
  impersonation_om_interface->ObjectRemoved(kTestServiceName2, object_path1,
                                            kTestInterfaceName1);
}

TEST_F(ImpersonationObjectManagerInterfaceTest, MultiServiceMerging) {
  dbus::ObjectPath object_path1(kTestObjectPath1);

  std::map<std::string, std::unique_ptr<ExportedObject>> exported_objects;
  auto impersonation_om_interface =
      std::make_unique<ImpersonationObjectManagerInterface>(
          bus_, exported_object_manager_wrapper_.get(),
          std::make_unique<TestInterfaceHandler>(), kTestInterfaceName1,
          client_manager_.get());

  impersonation_om_interface->RegisterToObjectManager(object_manager1_.get(),
                                                      kTestServiceName1);
  impersonation_om_interface->RegisterToObjectManager(object_manager2_.get(),
                                                      kTestServiceName2);

  scoped_refptr<dbus::MockExportedObject> exported_object1 =
      new dbus::MockExportedObject(bus_.get(), object_path1);
  EXPECT_CALL(*bus_, GetExportedObject(object_path1))
      .WillOnce(Return(exported_object1.get()));

  // D-Bus properties methods should be exported.
  ExpectExportPropertiesMethods(exported_object1.get());
  // CreateProperties called for an object.
  std::unique_ptr<dbus::PropertySet> dbus_property_set1(
      impersonation_om_interface->CreateProperties(
          kTestServiceName1, object_proxy_.get(), object_path1,
          kTestInterfaceName1));

  PropertySet* property_set1 =
      static_cast<PropertySet*>(dbus_property_set1.get());
  // The properties should all be registered.
  ASSERT_NE(nullptr, property_set1->GetProperty(kMergeStringPropertyName));
  EXPECT_CALL(*object_manager1_,
              GetProperties(object_path1, kTestInterfaceName1))
      .WillRepeatedly(Return(property_set1));
  SetPropertyValue<std::string>(
      property_set1->GetProperty(kMergeStringPropertyName), "string1");
  // Trigger property changed event and check that the exported properties are
  // updated.
  property_set1->NotifyPropertyChanged(kMergeStringPropertyName);
  ExportedInterface* interface =
      exported_object_manager_wrapper_->GetExportedInterface(
          object_path1, kTestInterfaceName1);
  ExpectExportedPropertyEquals<std::string>(
      "string1",
      interface->GetRegisteredExportedProperty(kMergeStringPropertyName));

  // Now the second service creates the same property.
  std::unique_ptr<dbus::PropertySet> dbus_property_set2(
      impersonation_om_interface->CreateProperties(
          kTestServiceName2, object_proxy_.get(), object_path1,
          kTestInterfaceName1));
  PropertySet* property_set2 =
      static_cast<PropertySet*>(dbus_property_set2.get());
  // The properties should all be registered.
  ASSERT_NE(nullptr, property_set2->GetProperty(kMergeStringPropertyName));
  EXPECT_CALL(*object_manager2_,
              GetProperties(object_path1, kTestInterfaceName1))
      .WillRepeatedly(Return(property_set2));
  SetPropertyValue<std::string>(
      property_set2->GetProperty(kMergeStringPropertyName), "string2");

  // Trigger property changed event and check that the exported properties are
  // updated.
  property_set2->NotifyPropertyChanged(kMergeStringPropertyName);
  ExpectExportedPropertyEquals<std::string>(
      "string1 string2",
      interface->GetRegisteredExportedProperty(kMergeStringPropertyName));

  // ObjectAdded events
  dbus::ExportedObject::MethodCallCallback method1_handler;
  dbus::ExportedObject::MethodCallCallback method2_handler;
  ExpectExportTestMethods(exported_object1.get(), kTestInterfaceName1,
                          &method1_handler, &method2_handler);
  impersonation_om_interface->ObjectAdded(kTestServiceName1, object_path1,
                                          kTestInterfaceName1);
  impersonation_om_interface->ObjectAdded(kTestServiceName2, object_path1,
                                          kTestInterfaceName1);
  // ObjectRemoved events
  impersonation_om_interface->ObjectRemoved(kTestServiceName1, object_path1,
                                            kTestInterfaceName1);
  // Service1 removed the object, so now the property of this object should
  // be updated based on the property of Service2.
  ExpectExportedPropertyEquals<std::string>(
      "string2",
      interface->GetRegisteredExportedProperty(kMergeStringPropertyName));

  // The last service removes the object, the corrseponding exported object
  // should be unregistered and the exported property removed.
  ExpectUnexportTestMethods(exported_object1.get(), kTestInterfaceName1);
  impersonation_om_interface->ObjectRemoved(kTestServiceName2, object_path1,
                                            kTestInterfaceName1);
  EXPECT_EQ(nullptr,
            interface->GetRegisteredExportedProperty(kMergeStringPropertyName));
}

TEST_F(ImpersonationObjectManagerInterfaceTest,
       MultiServiceWithObjectExportRuleAllServices) {
  dbus::ObjectPath object_path1(kTestObjectPath1);

  std::map<std::string, std::unique_ptr<ExportedObject>> exported_objects;
  auto impersonation_om_interface =
      std::make_unique<ImpersonationObjectManagerInterface>(
          bus_, exported_object_manager_wrapper_.get(),
          std::make_unique<TestInterfaceHandler>(
              ObjectExportRule::ALL_SERVICES),
          kTestInterfaceName1, client_manager_.get());

  impersonation_om_interface->RegisterToObjectManager(object_manager1_.get(),
                                                      kTestServiceName1);
  impersonation_om_interface->RegisterToObjectManager(object_manager2_.get(),
                                                      kTestServiceName2);

  scoped_refptr<dbus::MockExportedObject> exported_object1 =
      new dbus::MockExportedObject(bus_.get(), object_path1);
  EXPECT_CALL(*bus_, GetExportedObject(object_path1))
      .WillOnce(Return(exported_object1.get()));

  // D-Bus properties methods should be exported.
  ExpectExportPropertiesMethods(exported_object1.get());
  // CreateProperties called for an object.
  // This shouldn't trigger object export until the second service also exports
  // the same object.
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, dbus::kPropertiesInterface, _))
      .Times(0);
  std::unique_ptr<dbus::PropertySet> dbus_property_set1(
      impersonation_om_interface->CreateProperties(
          kTestServiceName1, object_proxy_.get(), object_path1,
          kTestInterfaceName1));
  PropertySet* property_set1 =
      static_cast<PropertySet*>(dbus_property_set1.get());
  // The properties should all be registered.
  ASSERT_NE(nullptr, property_set1->GetProperty(kStringPropertyName));
  ASSERT_NE(nullptr, property_set1->GetProperty(kIntPropertyName));
  ASSERT_NE(nullptr, property_set1->GetProperty(kBoolPropertyName));
  EXPECT_CALL(*object_manager1_,
              GetProperties(object_path1, kTestInterfaceName1))
      .WillRepeatedly(Return(property_set1));
  SetPropertyValue<std::string>(property_set1->GetProperty(kStringPropertyName),
                                kTestStringPropertyValue);
  SetPropertyValue<int>(property_set1->GetProperty(kIntPropertyName),
                        kTestIntPropertyValue);
  // Trigger property changed events, they should be ignored.
  property_set1->NotifyPropertyChanged(kStringPropertyName);
  property_set1->NotifyPropertyChanged(kIntPropertyName);
  property_set1->NotifyPropertyChanged(kBoolPropertyName);
  // ObjectAdded event before the other service exports the object. This should
  // be ignored.
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, kTestInterfaceName1, _))
      .Times(0);
  impersonation_om_interface->ObjectAdded(kTestServiceName2, object_path1,
                                          kTestInterfaceName1);

  // Another service also triggers CreateProperties for the same object.
  // This should trigger an object export.
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, dbus::kPropertiesInterface, _))
      .Times(1);
  std::unique_ptr<dbus::PropertySet> dbus_property_set2(
      impersonation_om_interface->CreateProperties(
          kTestServiceName2, object_proxy_.get(), object_path1,
          kTestInterfaceName1));
  // The value of the properties should reflect those of Service1.
  ExportedInterface* interface =
      exported_object_manager_wrapper_->GetExportedInterface(
          object_path1, kTestInterfaceName1);
  ExpectExportedPropertyEquals<std::string>(
      kTestStringPropertyValue,
      interface->GetRegisteredExportedProperty(kStringPropertyName));
  ExpectExportedPropertyEquals<int>(
      kTestIntPropertyValue,
      interface->GetRegisteredExportedProperty(kIntPropertyName));
  EXPECT_EQ(nullptr,
            interface->GetRegisteredExportedProperty(kBoolPropertyName));

  PropertySet* property_set2 =
      static_cast<PropertySet*>(dbus_property_set2.get());
  // The properties should all be registered.
  ASSERT_NE(nullptr, property_set2->GetProperty(kStringPropertyName));
  ASSERT_NE(nullptr, property_set2->GetProperty(kIntPropertyName));
  ASSERT_NE(nullptr, property_set2->GetProperty(kBoolPropertyName));
  EXPECT_CALL(*object_manager2_,
              GetProperties(object_path1, kTestInterfaceName1))
      .WillRepeatedly(Return(property_set2));
  SetPropertyValue<int>(property_set2->GetProperty(kIntPropertyName),
                        kTestIntPropertyValue + 1);
  SetPropertyValue<bool>(property_set2->GetProperty(kBoolPropertyName),
                         kTestBoolPropertyValue);

  // ObjectAdded event for Service2.
  dbus::ExportedObject::MethodCallCallback method1_handler;
  dbus::ExportedObject::MethodCallCallback method2_handler;
  ExpectExportTestMethods(exported_object1.get(), kTestInterfaceName1,
                          &method1_handler, &method2_handler);
  EXPECT_CALL(*exported_object_manager_,
              ClaimInterface(object_path1, kTestInterfaceName1, _))
      .Times(1);
  impersonation_om_interface->ObjectAdded(kTestServiceName2, object_path1,
                                          kTestInterfaceName1);

  // ObjectRemoved events
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path1, dbus::kPropertiesInterface))
      .Times(1);
  EXPECT_CALL(*exported_object_manager_,
              ReleaseInterface(object_path1, kTestInterfaceName1))
      .Times(1);
  // Service1 removed the object, the corresponding exported object should be
  // unregistered.
  ExpectUnexportTestMethods(exported_object1.get(), kTestInterfaceName1);
  EXPECT_CALL(*exported_object1, Unregister()).Times(1);
  impersonation_om_interface->ObjectRemoved(kTestServiceName1, object_path1,
                                            kTestInterfaceName1);
  EXPECT_EQ(nullptr, exported_object_manager_wrapper_->GetExportedInterface(
                         object_path1, kTestInterfaceName1));

  // The last service removes the object, there should be no other object
  // unregistration.
  EXPECT_CALL(*exported_object1, Unregister()).Times(0);
  impersonation_om_interface->ObjectRemoved(kTestServiceName2, object_path1,
                                            kTestInterfaceName1);
}

}  // namespace bluetooth
