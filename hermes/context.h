// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HERMES_CONTEXT_H_
#define HERMES_CONTEXT_H_

#include <dbus/bus.h>
#include <google-lpa/lpa/core/lpa.h>

#include "hermes/adaptor_factory_interface.h"
#include "hermes/modem_control_interface.h"

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
                         Executor* executor,
                         AdaptorFactoryInterface* adaptor_factory,
                         ModemControlInterface* modem_control);
  // Returns initialized Context singleton. Initialize() must have been invoked
  // prior to calls to this.
  static Context* Get() {
    CHECK(context_);
    return context_;
  }

  const scoped_refptr<dbus::Bus>& bus() { return bus_; }
  lpa::core::Lpa* lpa() { return lpa_; }
  Executor* executor() { return executor_; }
  AdaptorFactoryInterface* adaptor_factory() { return adaptor_factory_; }
  ModemControlInterface* modem_control() { return modem_control_; }

 private:
  Context(const scoped_refptr<dbus::Bus>& bus,
          lpa::core::Lpa* lpa,
          Executor* executor,
          AdaptorFactoryInterface* adaptor_factory,
          ModemControlInterface* modem_control);

  static Context* context_;

  scoped_refptr<dbus::Bus> bus_;
  lpa::core::Lpa* lpa_;
  Executor* executor_;
  AdaptorFactoryInterface* adaptor_factory_;
  ModemControlInterface* modem_control_;

  DISALLOW_COPY_AND_ASSIGN(Context);
};

}  // namespace hermes

#endif  // HERMES_CONTEXT_H_
