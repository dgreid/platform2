// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hermes/context.h"

namespace hermes {

// static
Context* Context::context_ = nullptr;

// static
void Context::Initialize(const scoped_refptr<dbus::Bus>& bus,
                         lpa::core::Lpa* lpa,
                         Executor* executor) {
  CHECK(!context_);
  context_ = new Context(bus, lpa, executor);
}

Context::Context(const scoped_refptr<dbus::Bus>& bus,
                 lpa::core::Lpa* lpa,
                 Executor* executor)
    : bus_(bus), lpa_(lpa), executor_(executor) {}

}  // namespace hermes
