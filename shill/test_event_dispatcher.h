// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_TEST_EVENT_DISPATCHER_H_
#define SHILL_TEST_EVENT_DISPATCHER_H_

#include <base/macros.h>
#include <base/test/task_environment.h>
#include <brillo/message_loops/base_message_loop.h>

#include "shill/event_dispatcher.h"

namespace shill {

// Event dispatcher with message loop for testing.
class EventDispatcherForTest : public EventDispatcher {
 public:
  EventDispatcherForTest() { chromeos_message_loop_.SetAsCurrent(); }
  ~EventDispatcherForTest() override = default;

 private:
  // Message loop for testing.
  base::SingleThreadTaskExecutor task_executor_{base::MessagePumpType::IO};
  // The chromeos wrapper for the main message loop.
  brillo::BaseMessageLoop chromeos_message_loop_{task_executor_.task_runner()};

  DISALLOW_COPY_AND_ASSIGN(EventDispatcherForTest);
};

}  // namespace shill

#endif  // SHILL_TEST_EVENT_DISPATCHER_H_
