// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mist/event_dispatcher.h"

#include <sys/epoll.h>

#include <vector>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <gtest/gtest.h>

namespace mist {

class EventDispatcherTest : public testing::Test {
 protected:
  void TearDown() override {
    // Close all file descriptors opened by CreatePollFileDescriptor().
    for (int file_descriptor : file_descriptors_) {
      close(file_descriptor);
    }
    file_descriptors_.clear();
  }

  // Creates and returns an epoll file descriptor that can be watched by
  // EventDispatcher::StartWatchingFileDescriptor().
  int CreatePollFileDescriptor() {
    int file_descriptor = epoll_create1(0);
    EXPECT_NE(-1, file_descriptor);
    file_descriptors_.push_back(file_descriptor);
    return file_descriptor;
  }

  EventDispatcher dispatcher_;
  std::vector<int> file_descriptors_;
};

TEST_F(EventDispatcherTest, StartAndStopWatchingFileDescriptor) {
  int file_descriptor = CreatePollFileDescriptor();

  // StopWatchingFileDescriptor on a file descriptor, which is not being
  // watched, should fail.
  EXPECT_FALSE(dispatcher_.StopWatchingFileDescriptor(file_descriptor));

  EXPECT_TRUE(dispatcher_.StartWatchingFileDescriptor(
      file_descriptor, EventDispatcher::Mode::READ, base::DoNothing()));

  // StartWatchingFileDescriptor on the same file descriptor should be ok.
  EXPECT_TRUE(dispatcher_.StartWatchingFileDescriptor(
      file_descriptor, EventDispatcher::Mode::READ, base::DoNothing()));
  EXPECT_TRUE(dispatcher_.StartWatchingFileDescriptor(
      file_descriptor, EventDispatcher::Mode::WRITE, base::DoNothing()));
  EXPECT_TRUE(dispatcher_.StartWatchingFileDescriptor(
      file_descriptor, EventDispatcher::Mode::READ_WRITE, base::DoNothing()));
  EXPECT_TRUE(dispatcher_.StopWatchingFileDescriptor(file_descriptor));
  EXPECT_FALSE(dispatcher_.StopWatchingFileDescriptor(file_descriptor));
}

TEST_F(EventDispatcherTest, StopWatchingAllFileDescriptors) {
  int file_descriptor1 = CreatePollFileDescriptor();
  int file_descriptor2 = CreatePollFileDescriptor();

  EXPECT_TRUE(dispatcher_.StartWatchingFileDescriptor(
      file_descriptor1, EventDispatcher::Mode::READ, base::DoNothing()));
  EXPECT_TRUE(dispatcher_.StartWatchingFileDescriptor(
      file_descriptor2, EventDispatcher::Mode::READ, base::DoNothing()));
  dispatcher_.StopWatchingAllFileDescriptors();
  EXPECT_FALSE(dispatcher_.StopWatchingFileDescriptor(file_descriptor1));
  EXPECT_FALSE(dispatcher_.StopWatchingFileDescriptor(file_descriptor2));
}

}  // namespace mist
