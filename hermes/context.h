// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_CONTEXT_H_
#define HERMES_CONTEXT_H_

#include <dbus/bus.h>
#include <google-lpa/lpa/core/lpa.h>

namespace hermes {

class Executor;

// Top-level context singleton for access to common context like google-lpa Lpa
// instance and D-Bus bus.
//
// This should be the sole implicit dependency for classes in Hermes.
class Context {
 public:
  // Initializes Context singleton. Must only be invoked once, and must be
  // invoked prior to clients calling Get().
  static void Initialize(const scoped_refptr<dbus::Bus>& bus,
                         lpa::core::Lpa* lpa,
                         Executor* executor);
  // Returns initialized Context singleton. Initialize() must have been invoked
  // prior to calls to this.
  static Context* Get() {
    CHECK(context_);
    return context_;
  }

  const scoped_refptr<dbus::Bus>& bus() { return bus_; }
  lpa::core::Lpa* lpa() { return lpa_; }
  Executor* executor() { return executor_; }

 private:
  Context(const scoped_refptr<dbus::Bus>& bus,
          lpa::core::Lpa* lpa,
          Executor* executor);

  static Context* context_;

  scoped_refptr<dbus::Bus> bus_;
  lpa::core::Lpa* lpa_;
  Executor* executor_;

  DISALLOW_COPY_AND_ASSIGN(Context);
};

}  // namespace hermes

#endif  // HERMES_CONTEXT_H_
