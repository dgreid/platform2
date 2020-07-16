// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <base/bind.h>
#include <base/memory/ref_counted.h>
#include <base/synchronization/waitable_event.h>
#include <base/task_runner.h>
#include <base/threading/thread.h>
#include <gtest/gtest.h>

#include "chaps/dbus/scoped_bus.h"

namespace {

void CreateScopedBus(base::WaitableEvent* completion_event,
                     chaps::ScopedBus* out_bus) {
  *out_bus = chaps::ScopedBus(dbus::Bus::Options());
  completion_event->Signal();
}

}  // namespace

namespace chaps {

TEST(TestScopedBus, SameThread) {
  ScopedBus bus((dbus::Bus::Options()));
  scoped_refptr<dbus::Bus> bus_ptr = bus.get();

  { ScopedBus bus2(std::move(bus)); }

  EXPECT_TRUE(bus_ptr->shutdown_completed());
}

TEST(TestScopedBus, DifferentThread) {
  base::Thread::Options options(base::MessagePumpType::IO, 0);
  base::Thread bus_thread("bus_thread");
  bus_thread.StartWithOptions(options);

  ScopedBus bus;
  base::WaitableEvent completion_event(
      base::WaitableEvent::ResetPolicy::AUTOMATIC,
      base::WaitableEvent::InitialState::NOT_SIGNALED);
  bus_thread.task_runner()->PostTask(
      FROM_HERE, base::Bind(&CreateScopedBus, &completion_event, &bus));
  completion_event.Wait();

  scoped_refptr<dbus::Bus> bus_ptr = bus.get();

  { ScopedBus bus2(std::move(bus)); }

  EXPECT_TRUE(bus_ptr->shutdown_completed());
}

}  // namespace chaps
