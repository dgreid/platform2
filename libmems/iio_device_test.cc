// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "libmems/common_types.h"
#include "libmems/test_fakes.h"

namespace libmems {

namespace {

constexpr char kFakeChannelName1[] = "fake_channel1";
constexpr char kFakeChannelName2[] = "fake_channel2";

constexpr char kFakeDeviceName[] = "iio:device0";
constexpr int kFakeDeviceId = 0;

constexpr char kTrigger0Attr[] = "trigger0";
constexpr char kTrigger1Attr[] = "trigger1";
constexpr char kTrigger12Attr[] = "trigger12";

constexpr char kDevice0Attr[] = "iio:device0";
constexpr char kDevice1Attr[] = "iio:device1";
constexpr char kDevice12Attr[] = "iio:device12";

class IioDeviceTest : public fakes::FakeIioDevice, public ::testing::Test {
 public:
  static base::Optional<int> GetIdAfterPrefix(const char* id_str,
                                              const char* prefix) {
    return IioDevice::GetIdAfterPrefix(id_str, prefix);
  }

  IioDeviceTest()
      : fakes::FakeIioDevice(nullptr, kFakeDeviceName, kFakeDeviceId),
        ::testing::Test() {}

 protected:
  void SetUp() override {
    auto channel1 =
        std::make_unique<fakes::FakeIioChannel>(kFakeChannelName1, false);
    channel1_ = channel1.get();
    AddChannel(std::move(channel1));

    auto channel2 =
        std::make_unique<fakes::FakeIioChannel>(kFakeChannelName2, false);
    channel2_ = channel2.get();
    AddChannel(std::move(channel2));
  }

  fakes::FakeIioChannel* channel1_;
  fakes::FakeIioChannel* channel2_;
};

TEST_F(IioDeviceTest, GetIdAfterPrefixTest) {
  EXPECT_EQ(GetIdAfterPrefix(kTrigger0Attr, kTriggerIdPrefix), 0);
  EXPECT_EQ(GetIdAfterPrefix(kTrigger1Attr, kTriggerIdPrefix), 1);
  EXPECT_EQ(GetIdAfterPrefix(kTrigger12Attr, kTriggerIdPrefix), 12);

  EXPECT_EQ(GetIdAfterPrefix(kDevice0Attr, kDeviceIdPrefix), 0);
  EXPECT_EQ(GetIdAfterPrefix(kDevice1Attr, kDeviceIdPrefix), 1);
  EXPECT_EQ(GetIdAfterPrefix(kDevice12Attr, kDeviceIdPrefix), 12);
}

TEST_F(IioDeviceTest, GetAllChannels) {
  auto channels = GetAllChannels();
  EXPECT_EQ(channels.size(), 2);
  EXPECT_EQ(channels[0], channel1_);
  EXPECT_EQ(channels[1], channel2_);
}

TEST_F(IioDeviceTest, GetChannelByIndex) {
  EXPECT_EQ(GetChannel(0), channel1_);
  EXPECT_EQ(GetChannel(1), channel2_);
}

TEST_F(IioDeviceTest, GetChannelByName) {
  EXPECT_EQ(GetChannel(kFakeChannelName1), channel1_);
  EXPECT_EQ(GetChannel(kFakeChannelName2), channel2_);
}

}  // namespace

}  // namespace libmems
