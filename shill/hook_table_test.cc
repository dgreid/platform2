// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/hook_table.h"

#include <memory>

#include <base/bind.h>

#include "shill/error.h"
#include "shill/test_event_dispatcher.h"
#include "shill/testing.h"

using base::Bind;
using base::Closure;
using base::Unretained;
using ::testing::_;

namespace shill {

namespace {

const char kName[] = "test";
const char kName1[] = "test1";
const char kName2[] = "test2";
const char kName3[] = "test3";

}  // namespace

class HookTableTest : public testing::Test {
 public:
  MOCK_METHOD(void, StartAction, ());
  MOCK_METHOD(void, StartAction2, ());
  MOCK_METHOD(void, DoneAction, (const Error&));

 protected:
  HookTableTest() : hook_table_(&event_dispatcher_) {}

  ResultCallback* GetDoneCallback() { return &hook_table_.done_callback_; }

  EventDispatcherForTest event_dispatcher_;
  HookTable hook_table_;
};

TEST_F(HookTableTest, ActionCompletes) {
  EXPECT_CALL(*this, StartAction());
  EXPECT_CALL(*this, DoneAction(IsSuccess()));
  Closure start_callback = Bind(&HookTableTest::StartAction, Unretained(this));
  ResultCallback done_callback =
      Bind(&HookTableTest::DoneAction, Unretained(this));
  hook_table_.Add(kName, start_callback);
  hook_table_.Run(0, done_callback);
  hook_table_.ActionComplete(kName);

  // Ensure that the timeout callback got cancelled.  If it did not get
  // cancelled, done_callback will be run twice and make this test fail.
  event_dispatcher_.DispatchPendingEvents();
}

ACTION_P2(CompleteAction, hook_table, name) {
  hook_table->ActionComplete(name);
}

ACTION_P2(CompleteActionAndRemoveAction, hook_table, name) {
  hook_table->ActionComplete(name);
  hook_table->Remove(name);
}

TEST_F(HookTableTest, ActionCompletesAndRemovesActionInDoneCallback) {
  EXPECT_CALL(*this, StartAction())
      .WillOnce(CompleteActionAndRemoveAction(&hook_table_, kName));
  EXPECT_CALL(*this, StartAction2())
      .WillOnce(CompleteAction(&hook_table_, kName2));
  EXPECT_CALL(*this, DoneAction(IsSuccess()));
  Closure start_callback = Bind(&HookTableTest::StartAction, Unretained(this));
  Closure start2_callback =
      Bind(&HookTableTest::StartAction2, Unretained(this));
  ResultCallback done_callback =
      Bind(&HookTableTest::DoneAction, Unretained(this));
  hook_table_.Add(kName, start_callback);
  hook_table_.Add(kName2, start2_callback);
  hook_table_.Run(0, done_callback);

  // Ensure that the timeout callback got cancelled.  If it did not get
  // cancelled, done_callback will be run twice and make this test fail.
  event_dispatcher_.DispatchPendingEvents();
}

TEST_F(HookTableTest, ActionCompletesInline) {
  // StartAction completes immediately before HookTable::Run() returns.
  EXPECT_CALL(*this, StartAction())
      .WillOnce(CompleteAction(&hook_table_, kName));
  EXPECT_CALL(*this, DoneAction(IsSuccess()));
  Closure start_callback = Bind(&HookTableTest::StartAction, Unretained(this));
  ResultCallback done_callback =
      Bind(&HookTableTest::DoneAction, Unretained(this));
  hook_table_.Add(kName, start_callback);
  hook_table_.Run(0, done_callback);

  // Ensure that the timeout callback got cancelled.  If it did not get
  // cancelled, done_callback will be run twice and make this test fail.
  event_dispatcher_.DispatchPendingEvents();
}

TEST_F(HookTableTest, ActionTimesOut) {
  const int kTimeout = 1;
  EXPECT_CALL(*this, StartAction());
  EXPECT_CALL(*this, DoneAction(IsFailure()));

  Closure start_callback = Bind(&HookTableTest::StartAction, Unretained(this));
  ResultCallback done_callback =
      Bind(&HookTableTest::DoneAction, Unretained(this));

  hook_table_.Add(kName, start_callback);
  hook_table_.Run(kTimeout, done_callback);

  // Cause the event dispatcher to exit after kTimeout + 1 ms.
  event_dispatcher_.PostDelayedTask(
      FROM_HERE,
      base::Bind(&EventDispatcherForTest::QuitDispatchForever,
                 // event_dispatcher_ will not be deleted before RunLoop quits.
                 base::Unretained(&event_dispatcher_)),
      kTimeout + 1);
  event_dispatcher_.DispatchForever();
  EXPECT_TRUE(GetDoneCallback()->is_null());
}

TEST_F(HookTableTest, MultipleActionsAllSucceed) {
  Closure pending_callback;
  const int kTimeout = 10;
  EXPECT_CALL(*this, StartAction()).Times(2);

  // StartAction2 completes immediately before HookTable::Run() returns.
  EXPECT_CALL(*this, StartAction2())
      .WillOnce(CompleteAction(&hook_table_, kName1));
  EXPECT_CALL(*this, DoneAction(IsSuccess()));

  Closure start_callback = Bind(&HookTableTest::StartAction, Unretained(this));
  Closure start2_callback =
      Bind(&HookTableTest::StartAction2, Unretained(this));
  ResultCallback done_callback =
      Bind(&HookTableTest::DoneAction, Unretained(this));

  hook_table_.Add(kName1, start2_callback);
  hook_table_.Add(kName2, start_callback);
  hook_table_.Add(kName3, start_callback);
  hook_table_.Run(kTimeout, done_callback);
  hook_table_.ActionComplete(kName2);
  hook_table_.ActionComplete(kName3);
}

TEST_F(HookTableTest, MultipleActionsAndOneTimesOut) {
  Closure pending_callback;
  const int kTimeout = 1;
  EXPECT_CALL(*this, StartAction()).Times(3);
  EXPECT_CALL(*this, DoneAction(IsFailure()));

  Closure start_callback = Bind(&HookTableTest::StartAction, Unretained(this));
  ResultCallback done_callback =
      Bind(&HookTableTest::DoneAction, Unretained(this));

  hook_table_.Add(kName1, start_callback);
  hook_table_.Add(kName2, start_callback);
  hook_table_.Add(kName3, start_callback);
  hook_table_.Run(kTimeout, done_callback);
  hook_table_.ActionComplete(kName1);
  hook_table_.ActionComplete(kName3);
  // Cause the event dispatcher to exit after kTimeout + 1 ms.
  event_dispatcher_.PostDelayedTask(
      FROM_HERE,
      base::Bind(&EventDispatcherForTest::QuitDispatchForever,
                 // event_dispatcher_ will not be deleted before RunLoop quits.
                 base::Unretained(&event_dispatcher_)),
      kTimeout + 1);
  event_dispatcher_.DispatchForever();
}

TEST_F(HookTableTest, AddActionsWithSameName) {
  EXPECT_CALL(*this, StartAction()).Times(0);
  EXPECT_CALL(*this, StartAction2());
  EXPECT_CALL(*this, DoneAction(IsSuccess()));
  Closure start_callback = Bind(&HookTableTest::StartAction, Unretained(this));
  Closure start2_callback =
      Bind(&HookTableTest::StartAction2, Unretained(this));
  ResultCallback done_callback =
      Bind(&HookTableTest::DoneAction, Unretained(this));
  hook_table_.Add(kName, start_callback);

  // Adding an action with the same name kName.  New callbacks should replace
  // old ones.
  hook_table_.Add(kName, start2_callback);
  hook_table_.Run(0, done_callback);
  hook_table_.ActionComplete(kName);

  // Ensure that the timeout callback got cancelled.  If it did not get
  // cancelled, done_callback will be run twice and make this test fail.
  event_dispatcher_.DispatchPendingEvents();
}

TEST_F(HookTableTest, RemoveAction) {
  EXPECT_CALL(*this, StartAction()).Times(0);
  EXPECT_CALL(*this, DoneAction(IsSuccess()));
  Closure start_callback = Bind(&HookTableTest::StartAction, Unretained(this));
  ResultCallback done_callback =
      Bind(&HookTableTest::DoneAction, Unretained(this));
  hook_table_.Add(kName, start_callback);
  hook_table_.Remove(kName);
  hook_table_.Run(0, done_callback);
}

TEST_F(HookTableTest, ActionCompleteFollowedByRemove) {
  EXPECT_CALL(*this, StartAction()).Times(0);
  Closure start_callback = Bind(&HookTableTest::StartAction, Unretained(this));
  hook_table_.Add(kName, start_callback);
  hook_table_.ActionComplete(kName);
  hook_table_.Remove(kName);
}

TEST_F(HookTableTest, IsEmpty) {
  EXPECT_TRUE(hook_table_.IsEmpty());
  hook_table_.Add(kName, Closure());
  EXPECT_FALSE(hook_table_.IsEmpty());
  hook_table_.Remove(kName);
  EXPECT_TRUE(hook_table_.IsEmpty());
}

class SomeClass : public base::RefCounted<SomeClass> {
 public:
  SomeClass() = default;
  void StartAction() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(SomeClass);
};

// This test verifies that a class that removes itself from a hook table upon
// destruction does not crash if the hook table is destroyed first.
TEST_F(HookTableTest, RefcountedObject) {
  auto ht = std::make_unique<HookTable>(&event_dispatcher_);
  {
    scoped_refptr<SomeClass> ref_counted_object = new SomeClass();
    Closure start_callback = Bind(&SomeClass::StartAction, ref_counted_object);
    ht->Add(kName, start_callback);
  }
}

TEST_F(HookTableTest, ActionAddedBeforePreviousActionCompletes) {
  EXPECT_CALL(*this, StartAction());
  EXPECT_CALL(*this, StartAction2()).Times(0);
  EXPECT_CALL(*this, DoneAction(IsSuccess()));
  Closure start_callback = Bind(&HookTableTest::StartAction, Unretained(this));
  Closure start2_callback =
      Bind(&HookTableTest::StartAction2, Unretained(this));
  ResultCallback done_callback =
      Bind(&HookTableTest::DoneAction, Unretained(this));
  hook_table_.Add(kName, start_callback);
  hook_table_.Run(0, done_callback);

  // An action with the same name is added before the previous actions complete.
  // It should not be run.
  hook_table_.Add(kName, start2_callback);
  hook_table_.ActionComplete(kName);
}

}  // namespace shill
