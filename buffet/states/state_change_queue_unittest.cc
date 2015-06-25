// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include "buffet/states/state_change_queue.h"

#include <gtest/gtest.h>

#include "buffet/commands/unittest_utils.h"

namespace buffet {

class StateChangeQueueTest : public ::testing::Test {
 public:
  void SetUp() override {
    queue_.reset(new StateChangeQueue(100));
  }

  void TearDown() override {
    queue_.reset();
  }

  std::unique_ptr<StateChangeQueue> queue_;
};

TEST_F(StateChangeQueueTest, Empty) {
  EXPECT_TRUE(queue_->IsEmpty());
  EXPECT_EQ(0, queue_->GetLastStateChangeId());
  EXPECT_TRUE(queue_->GetAndClearRecordedStateChanges().empty());
}

TEST_F(StateChangeQueueTest, UpdateOne) {
  StateChange change{
    base::Time::Now(),
    native_types::Object{{"prop.name", unittests::make_int_prop_value(23)}}
  };
  ASSERT_TRUE(queue_->NotifyPropertiesUpdated(change.timestamp,
                                              change.changed_properties));
  EXPECT_FALSE(queue_->IsEmpty());
  EXPECT_EQ(1, queue_->GetLastStateChangeId());
  auto changes = queue_->GetAndClearRecordedStateChanges();
  EXPECT_EQ(1, queue_->GetLastStateChangeId());
  ASSERT_EQ(1, changes.size());
  EXPECT_EQ(change.timestamp, changes.front().timestamp);
  EXPECT_EQ(change.changed_properties, changes.front().changed_properties);
  EXPECT_TRUE(queue_->IsEmpty());
  EXPECT_TRUE(queue_->GetAndClearRecordedStateChanges().empty());
}

TEST_F(StateChangeQueueTest, UpdateMany) {
  StateChange change1{
    base::Time::Now(),
    native_types::Object{{"prop.name1", unittests::make_int_prop_value(23)}}
  };
  ASSERT_TRUE(queue_->NotifyPropertiesUpdated(change1.timestamp,
                                              change1.changed_properties));
  StateChange change2{
    base::Time::Now(),
    native_types::Object{
      {"prop.name1", unittests::make_int_prop_value(17)},
      {"prop.name2", unittests::make_double_prop_value(1.0)},
      {"prop.name3", unittests::make_bool_prop_value(false)},
    }
  };
  ASSERT_TRUE(queue_->NotifyPropertiesUpdated(change2.timestamp,
                                              change2.changed_properties));
  EXPECT_EQ(2, queue_->GetLastStateChangeId());
  EXPECT_FALSE(queue_->IsEmpty());
  auto changes = queue_->GetAndClearRecordedStateChanges();
  ASSERT_EQ(2, changes.size());
  EXPECT_EQ(change1.timestamp, changes[0].timestamp);
  EXPECT_EQ(change1.changed_properties, changes[0].changed_properties);
  EXPECT_EQ(change2.timestamp, changes[1].timestamp);
  EXPECT_EQ(change2.changed_properties, changes[1].changed_properties);
  EXPECT_TRUE(queue_->IsEmpty());
  EXPECT_TRUE(queue_->GetAndClearRecordedStateChanges().empty());
}

TEST_F(StateChangeQueueTest, GroupByTimestamp) {
  base::Time timestamp = base::Time::Now();
  base::TimeDelta time_delta = base::TimeDelta::FromMinutes(1);

  ASSERT_TRUE(queue_->NotifyPropertiesUpdated(
      timestamp,
      native_types::Object{{"prop.name1", unittests::make_int_prop_value(1)}}));

  ASSERT_TRUE(queue_->NotifyPropertiesUpdated(
      timestamp,
      native_types::Object{{"prop.name2", unittests::make_int_prop_value(2)}}));

  ASSERT_TRUE(queue_->NotifyPropertiesUpdated(
      timestamp,
      native_types::Object{{"prop.name1", unittests::make_int_prop_value(3)}}));

  ASSERT_TRUE(queue_->NotifyPropertiesUpdated(
      timestamp + time_delta,
      native_types::Object{{"prop.name1", unittests::make_int_prop_value(4)}}));

  auto changes = queue_->GetAndClearRecordedStateChanges();
  EXPECT_EQ(4, queue_->GetLastStateChangeId());
  ASSERT_EQ(2, changes.size());

  native_types::Object expected1{
    {"prop.name1", unittests::make_int_prop_value(3)},
    {"prop.name2", unittests::make_int_prop_value(2)},
  };
  native_types::Object expected2{
    {"prop.name1", unittests::make_int_prop_value(4)},
  };
  EXPECT_EQ(timestamp, changes[0].timestamp);
  EXPECT_EQ(expected1, changes[0].changed_properties);
  EXPECT_EQ(timestamp + time_delta, changes[1].timestamp);
  EXPECT_EQ(expected2, changes[1].changed_properties);
}

TEST_F(StateChangeQueueTest, MaxQueueSize) {
  queue_.reset(new StateChangeQueue(2));
  base::Time start_time = base::Time::Now();
  base::TimeDelta time_delta1 = base::TimeDelta::FromMinutes(1);
  base::TimeDelta time_delta2 = base::TimeDelta::FromMinutes(3);

  ASSERT_TRUE(queue_->NotifyPropertiesUpdated(
      start_time,
      native_types::Object{
        {"prop.name1", unittests::make_int_prop_value(1)},
        {"prop.name2", unittests::make_int_prop_value(2)},
      }));

  ASSERT_TRUE(queue_->NotifyPropertiesUpdated(
      start_time + time_delta1,
      native_types::Object{
        {"prop.name1", unittests::make_int_prop_value(3)},
        {"prop.name3", unittests::make_int_prop_value(4)},
      }));

  ASSERT_TRUE(queue_->NotifyPropertiesUpdated(
      start_time + time_delta2,
      native_types::Object{
        {"prop.name10", unittests::make_int_prop_value(10)},
        {"prop.name11", unittests::make_int_prop_value(11)},
      }));

  EXPECT_EQ(3, queue_->GetLastStateChangeId());
  auto changes = queue_->GetAndClearRecordedStateChanges();
  ASSERT_EQ(2, changes.size());

  native_types::Object expected1{
    {"prop.name1", unittests::make_int_prop_value(3)},
    {"prop.name2", unittests::make_int_prop_value(2)},
    {"prop.name3", unittests::make_int_prop_value(4)},
  };
  EXPECT_EQ(start_time + time_delta1, changes[0].timestamp);
  EXPECT_EQ(expected1, changes[0].changed_properties);

  native_types::Object expected2{
    {"prop.name10", unittests::make_int_prop_value(10)},
    {"prop.name11", unittests::make_int_prop_value(11)},
  };
  EXPECT_EQ(start_time + time_delta2, changes[1].timestamp);
  EXPECT_EQ(expected2, changes[1].changed_properties);
}

}  // namespace buffet
