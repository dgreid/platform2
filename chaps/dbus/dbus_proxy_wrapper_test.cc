// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <base/bind.h>
#include <base/memory/ref_counted.h>
#include <base/task_runner.h>
#include <base/threading/thread.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "chaps/dbus/dbus_proxy_wrapper.h"

namespace {

// TODO(ejcaruso): After the libchrome uprev, replace these so that we can
// test the real object proxy construction functions with mocks.
void SuccessCallback(const chaps::OnObjectProxyConstructedCallback& cb) {
  cb.Run(true, chaps::ScopedBus(), nullptr);
}

void FailureCallback(const chaps::OnObjectProxyConstructedCallback& cb) {
  cb.Run(false, chaps::ScopedBus(), nullptr);
}

void TimeoutCallback(const chaps::OnObjectProxyConstructedCallback& cb) {
  // Do nothing. The proxy construction task should time out even if the
  // callback is never called.
}

}  // namespace

namespace chaps {

class TestProxyWrapperConstruction : public ::testing::Test {
 public:
  TestProxyWrapperConstruction() : dbus_thread_("dbus_thread") {}
  ~TestProxyWrapperConstruction() override {}

 protected:
  void SetUp() override {
    base::Thread::Options options(base::MessagePumpType::IO, 0);
    dbus_thread_.StartWithOptions(options);
  }

  scoped_refptr<DBusProxyWrapper> ConstructProxyWrapper(
      base::Callback<void(const OnObjectProxyConstructedCallback&)> func) {
    scoped_refptr<ProxyWrapperConstructionTask> task(
        new ProxyWrapperConstructionTask);
    task->set_construction_callback_for_testing(func);
    return task->ConstructProxyWrapper(dbus_thread_.task_runner());
  }

 private:
  base::Thread dbus_thread_;
};

TEST_F(TestProxyWrapperConstruction, ConstructSuccess) {
  EXPECT_NE(nullptr, ConstructProxyWrapper(base::Bind(&SuccessCallback)));
}

TEST_F(TestProxyWrapperConstruction, ConstructFailure) {
  EXPECT_EQ(nullptr, ConstructProxyWrapper(base::Bind(&FailureCallback)));
}

TEST_F(TestProxyWrapperConstruction, ConstructTimeout) {
  EXPECT_EQ(nullptr, ConstructProxyWrapper(base::Bind(&TimeoutCallback)));
}

}  // namespace chaps
