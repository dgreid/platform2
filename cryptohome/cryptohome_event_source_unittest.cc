// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Unit tests for CryptohomeEventSource.

#include "cryptohome/cryptohome_event_source.h"

#include <memory>
#include <utility>

#include <base/logging.h>
#include <gtest/gtest.h>

#include "cryptohome/vault_keyset.h"

namespace cryptohome {

class EventDestructorWatcher {
 public:
  virtual void NotifyDestroy(CryptohomeEventBase* event) = 0;
};

class MyEvent : public CryptohomeEventBase {
 public:
  MyEvent(EventDestructorWatcher* watcher, int id)
      : destructor_watcher_(watcher), id_(id) {}
  ~MyEvent() override {
    if (destructor_watcher_) {
      destructor_watcher_->NotifyDestroy(this);
    }
  }

  int get_id() { return id_; }

  const char* GetEventName() const override { return "MyEvent"; }

 private:
  EventDestructorWatcher* destructor_watcher_;
  int id_;
};

class EventSink : public CryptohomeEventSourceSink,
                  public EventDestructorWatcher {
 public:
  EventSink() = default;
  ~EventSink() override = default;

  void NotifyEvent(CryptohomeEventBase* event) override {
    completed_events_.push_back(static_cast<MyEvent*>(event)->get_id());
  }

  void NotifyDestroy(CryptohomeEventBase* event) override {
    destroyed_events_.push_back(static_cast<MyEvent*>(event)->get_id());
  }

  std::vector<int> completed_events_;
  std::vector<int> destroyed_events_;
};

TEST(CryptohomeEventSourceTest, TestEventSink) {
  EventSink event_sink;
  CryptohomeEventSource event_source;
  event_source.Reset(&event_sink, NULL);

  static const int kEventCount = 4096;
  for (int i = 0; i < kEventCount; i++) {
    event_source.AddEvent(std::make_unique<MyEvent>(&event_sink, i));
  }

  EXPECT_TRUE(event_source.EventsPending());
  event_source.HandleDispatch();
  EXPECT_FALSE(event_source.EventsPending());
  EXPECT_EQ(kEventCount, event_sink.completed_events_.size());
  EXPECT_EQ(kEventCount, event_sink.destroyed_events_.size());
}

TEST(CryptohomeEventSourceTest, TestEventSinkNoClear) {
  EventSink event_sink;

  static const int kEventCount = 4096;
  // Scope the event source
  {
    CryptohomeEventSource event_source;
    event_source.Reset(&event_sink, NULL);

    for (int i = 0; i < kEventCount; i++) {
      event_source.AddEvent(std::make_unique<MyEvent>(&event_sink, i));
    }

    EXPECT_TRUE(event_source.EventsPending());
  }

  EXPECT_EQ(0, event_sink.completed_events_.size());
  EXPECT_EQ(kEventCount, event_sink.destroyed_events_.size());
}

}  // namespace cryptohome
