// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/time/time.h>
#include <base/threading/thread.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/dbus/dbus_method_response.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "libhwsec/task_dispatching_framework.h"

using ResponseSender = ::dbus::ExportedObject::ResponseSender;
using DBusMethodResponse = brillo::dbus_utils::DBusMethodResponse<>;
using ThreadSafeDBusMethodResponse = hwsec::ThreadSafeDBusMethodResponse<>;
using DBusMethodResponseCallback = std::unique_ptr<DBusMethodResponse>;

constexpr base::TimeDelta kTestTimeout = base::TimeDelta::FromSeconds(5);

class DBusMethodResponseWrapperTestBase : public testing::Test {
 public:
  DBusMethodResponseWrapperTestBase()
      : dbus_thread_("dbus_thread"),
        worker_thread_("worker_thread"),
        finished_event_(base::WaitableEvent::ResetPolicy::MANUAL,
                        base::WaitableEvent::InitialState::NOT_SIGNALED) {}

  void SetUp() override {
    dbus_thread_.StartAndWaitForTesting();
    worker_thread_.StartAndWaitForTesting();
    finished_event_.Reset();
  }

  // Create a mock DbusMethodResponse callback which will run |check_function|
  // after it is called.
  DBusMethodResponseCallback CreateMockDBusMethodResponse(
      base::Closure check_function) {
    // Make a fake method call.
    std::shared_ptr<dbus::MethodCall> method_call =
        std::make_shared<dbus::MethodCall>("com.example.Interface",
                                           "MockMethod");
    // Set a value to bypass the checks in dbus libraray.
    method_call->SetSerial(5);

    // TODO(crbug/1094927): Change to base::BindOnce after uprev.
    ResponseSender sender = base::Bind(
        [](std::shared_ptr<dbus::MethodCall> method_call, base::Closure check,
           std::unique_ptr<dbus::Response> response) { check.Run(); },
        method_call, check_function);

    return std::make_unique<DBusMethodResponse>(method_call.get(),
                                                std::move(sender));
  }

  void CheckIfDbusCallbackIsCalledOnDbusThread() {
    EXPECT_EQ(base::PlatformThread::CurrentId(), dbus_thread_.GetThreadId());
    finished_event_.Signal();
  }

  // Create the mocked DbusMethodResponse which is wrapping by
  // |callback_decorator| and pass the callback to |handler| and run on the
  // |thread_of_handler|.
  void CreateCallbackAndCallOnHandler(
      const base::Callback<DBusMethodResponseCallback(
          DBusMethodResponseCallback)> callback_decorator,
      base::Thread* thread_of_handler,
      base::Callback<void(DBusMethodResponseCallback)> handler) {
    auto checker = base::Bind(&DBusMethodResponseWrapperTestBase::
                                  CheckIfDbusCallbackIsCalledOnDbusThread,
                              base::Unretained(this));

    // Create the raw DbusMethodResponse callback with |checker|.
    DBusMethodResponseCallback raw_callback =
        CreateMockDBusMethodResponse(checker);

    // Post-process the DbusMethodResponse callback with custom decorator.
    DBusMethodResponseCallback final_callback =
        callback_decorator.Run(std::move(raw_callback));

    // Run the |handler| on thread |thread_of_handler| with customized dbus
    // callback.
    thread_of_handler->task_runner()->PostTask(
        FROM_HERE,
        base::Bind(handler, base::Passed(std::move(final_callback))));
  }

  // Run CreateCallbackAndCallOnHandler on dbus thread.
  void DoCreateCallbackAndCallOnHandler(
      const base::Callback<DBusMethodResponseCallback(
          DBusMethodResponseCallback)> callback_decorator,
      base::Callback<void(DBusMethodResponseCallback)> handler,
      bool run_handler_on_the_different_thread) {
    base::Thread* target_thread =
        run_handler_on_the_different_thread ? &worker_thread_ : &dbus_thread_;
    dbus_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(
            &DBusMethodResponseWrapperTestBase::CreateCallbackAndCallOnHandler,
            base::Unretained(this), callback_decorator,
            base::Unretained(target_thread), handler));
  }

 protected:
  base::Thread dbus_thread_;
  base::Thread worker_thread_;
  base::WaitableEvent finished_event_;
};

class ThreadSafeDBusMethodResponseTest
    : public DBusMethodResponseWrapperTestBase,
      public ::testing::WithParamInterface<bool> {
 public:
  ThreadSafeDBusMethodResponseTest() : on_worker_thread_(GetParam()) {}

 protected:
  bool on_worker_thread_;
};

INSTANTIATE_TEST_SUITE_P(TestedOnWorkerThread,
                         ThreadSafeDBusMethodResponseTest,
                         ::testing::Values(false, true));

TEST_P(ThreadSafeDBusMethodResponseTest, Return) {
  auto dbus_handler = base::Bind(
      [](DBusMethodResponseCallback callback) { callback->Return(); });
  auto callback_decorator =
      base::Bind(ThreadSafeDBusMethodResponse::MakeThreadSafe);
  DoCreateCallbackAndCallOnHandler(callback_decorator, dbus_handler,
                                   on_worker_thread_);
  EXPECT_TRUE(finished_event_.TimedWait(kTestTimeout));
}

TEST_P(ThreadSafeDBusMethodResponseTest, ReplyWithErrorWithRawPointer) {
  auto dbus_handler = base::Bind([](DBusMethodResponseCallback callback) {
    auto error_ptr = brillo::Error::Create(FROM_HERE, "error_domain",
                                           "error_code", "error_message");
    callback->ReplyWithError(error_ptr.get());
  });
  auto callback_decorator =
      base::Bind(ThreadSafeDBusMethodResponse::MakeThreadSafe);
  DoCreateCallbackAndCallOnHandler(callback_decorator, dbus_handler,
                                   on_worker_thread_);
  EXPECT_TRUE(finished_event_.TimedWait(kTestTimeout));
}

TEST_P(ThreadSafeDBusMethodResponseTest, ReplyWithErrorWithStrings) {
  auto dbus_handler = base::Bind([](DBusMethodResponseCallback callback) {
    callback->ReplyWithError(FROM_HERE, "error_domain", "error_code",
                             "error_message");
  });
  auto callback_decorator =
      base::Bind(ThreadSafeDBusMethodResponse::MakeThreadSafe);
  DoCreateCallbackAndCallOnHandler(callback_decorator, dbus_handler,
                                   on_worker_thread_);
  EXPECT_TRUE(finished_event_.TimedWait(kTestTimeout));
}

TEST_P(ThreadSafeDBusMethodResponseTest, Destruct) {
  auto dbus_handler = base::Bind([](DBusMethodResponseCallback callback) {});
  auto callback_decorator =
      base::Bind(ThreadSafeDBusMethodResponse::MakeThreadSafe);
  DoCreateCallbackAndCallOnHandler(callback_decorator, dbus_handler,
                                   on_worker_thread_);
  EXPECT_TRUE(finished_event_.TimedWait(kTestTimeout));
}
