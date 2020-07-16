// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_DBUS_SCOPED_BUS_H_
#define CHAPS_DBUS_SCOPED_BUS_H_

#include <base/memory/ref_counted.h>
#include <base/single_thread_task_runner.h>
#include <dbus/bus.h>

namespace chaps {

// After we have an ObjectProxy, there is a reference cycle between the
// Bus and the ObjectProxy. This can be resolved by shutting down the bus
// before we get rid of it. We may need to shut down the bus on another
// thread to satisfy threading restrictions.
class ScopedBus {
 public:
  ScopedBus();
  // Substitute for a normal dbus::Bus constructor.
  explicit ScopedBus(const dbus::Bus::Options& options);
  // Move constructor.
  ScopedBus(ScopedBus&& other);

  virtual ~ScopedBus();

  // Move assignment.
  ScopedBus& operator=(ScopedBus&& other);

  // ScopedBus should be a drop-in replacement for dbus::Bus so
  // this overload is provided.
  dbus::Bus* operator->() const { return bus_.get(); }

  scoped_refptr<dbus::Bus> get() { return bus_; }

 private:
  scoped_refptr<dbus::Bus> bus_;
  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  DISALLOW_COPY_AND_ASSIGN(ScopedBus);
};

}  // namespace chaps

#endif  // CHAPS_DBUS_SCOPED_BUS_H_
