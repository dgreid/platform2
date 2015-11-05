// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "power_manager/powerd/system/internal_backlight.h"

#include <string>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/format_macros.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <gtest/gtest.h>
#include <linux/fb.h>

#include "power_manager/common/clock.h"

namespace power_manager {
namespace system {

class InternalBacklightTest : public ::testing::Test {
 public:
  InternalBacklightTest() {}
  virtual ~InternalBacklightTest() {}

  virtual void SetUp() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    test_path_ = temp_dir_.path();
  }

  // Create files to make the given directory look like it is a sysfs backlight
  // dir.
  void PopulateBacklightDir(const base::FilePath& path,
                            int64_t brightness,
                            int64_t max_brightness,
                            int64_t actual_brightness) {
    CHECK(base::CreateDirectory(path));

    std::string str = base::StringPrintf("%" PRId64 "\n", brightness);
    ASSERT_EQ(str.size(),
              base::WriteFile(
                  path.Append(InternalBacklight::kBrightnessFilename),
                  str.data(), str.size()));

    str = base::StringPrintf("%" PRId64 "\n", max_brightness);
    ASSERT_EQ(str.size(),
              base::WriteFile(
                  path.Append(InternalBacklight::kMaxBrightnessFilename),
                  str.data(), str.size()));

    if (actual_brightness >= 0) {
      str = base::StringPrintf("%" PRId64 "\n", actual_brightness);
      ASSERT_EQ(str.size(),
                base::WriteFile(
                    path.Append(InternalBacklight::kActualBrightnessFilename),
                    str.data(), str.size()));
    }
  }

  // Reads and returns an int64 value from |path|. -1 is returned on error.
  int64_t ReadFile(const base::FilePath& path) {
    std::string data;
    if (!base::ReadFileToString(path, &data)) {
      LOG(ERROR) << "Unable to read data from " << path.value();
      return -1;
    }
    int64_t value = 0;
    base::TrimWhitespaceASCII(data, base::TRIM_TRAILING, &data);
    if (!base::StringToInt64(data, &value)) {
      LOG(ERROR) << "Unable to parse \"" << value << "\" from " << path.value();
      return -1;
    }
    return value;
  }

  // Returns the value from the "brightness" file in |directory|.
  // -1 is returned on error.
  int64_t ReadBrightness(const base::FilePath& directory) {
    return ReadFile(directory.Append(InternalBacklight::kBrightnessFilename));
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath test_path_;
};

// A basic test of functionality.
TEST_F(InternalBacklightTest, BasicTest) {
  base::FilePath this_test_path = test_path_.Append("basic_test");
  const int64_t kBrightness = 128;
  const int64_t kMaxBrightness = 255;
  const int64_t kActualBrightness = 127;

  base::FilePath my_path = this_test_path.Append("pwm-backlight");
  PopulateBacklightDir(my_path, kBrightness, kMaxBrightness, kActualBrightness);

  InternalBacklight backlight;
  ASSERT_TRUE(backlight.Init(this_test_path, "*"));
  EXPECT_EQ(kActualBrightness, backlight.GetCurrentBrightnessLevel());
  EXPECT_EQ(kMaxBrightness, backlight.GetMaxBrightnessLevel());
}

// Make sure things work OK when there is no actual_brightness file.
TEST_F(InternalBacklightTest, NoActualBrightnessTest) {
  base::FilePath this_test_path =
      test_path_.Append("no_actual_brightness_test");
  const int64_t kBrightness = 128;
  const int64_t kMaxBrightness = 255;

  base::FilePath my_path = this_test_path.Append("pwm-backlight");
  PopulateBacklightDir(my_path, kBrightness, kMaxBrightness, -1);

  InternalBacklight backlight;
  ASSERT_TRUE(backlight.Init(this_test_path, "*"));
  EXPECT_EQ(kBrightness, backlight.GetCurrentBrightnessLevel());
  EXPECT_EQ(kMaxBrightness, backlight.GetMaxBrightnessLevel());
}

// Test that we pick the device with the greatest granularity.
TEST_F(InternalBacklightTest, GranularityTest) {
  const base::FilePath this_test_path = test_path_.Append("granularity_test");
  const base::FilePath a_path = this_test_path.Append("a");
  PopulateBacklightDir(a_path, 10, 127, 11);
  const base::FilePath b_path = this_test_path.Append("b");
  PopulateBacklightDir(b_path, 20, 255, 21);
  const base::FilePath c_path = this_test_path.Append("c");
  PopulateBacklightDir(c_path, 30, 63, 31);

  InternalBacklight backlight;
  ASSERT_TRUE(backlight.Init(this_test_path, "*"));
  EXPECT_EQ(21, backlight.GetCurrentBrightnessLevel());
  EXPECT_EQ(255, backlight.GetMaxBrightnessLevel());
}

// Test that directories starting with a "." are ignored.
TEST_F(InternalBacklightTest, NoDotDirsTest) {
  base::FilePath this_test_path = test_path_.Append("no_dot_dirs_test");

  // We'll just create one dir and it will have a dot in it.  Then, we can
  // be sure that we didn't just get lucky...
  base::FilePath my_path = this_test_path.Append(".pwm-backlight");
  PopulateBacklightDir(my_path, 128, 255, 127);

  InternalBacklight backlight;
  EXPECT_FALSE(backlight.Init(this_test_path, "*"));
}

// Test that the glob is working correctly for searching for backlight dirs.
TEST_F(InternalBacklightTest, GlobTest) {
  base::FilePath this_test_path = test_path_.Append("glob_test");

  // Purposely give my::kbd_backlight a lower "max_level" than
  // .no::kbd_backlight so that we know that dirs starting with a "." are
  // ignored.
  base::FilePath my_path = this_test_path.Append("my::kbd_backlight");
  PopulateBacklightDir(my_path, 1, 2, -1);

  base::FilePath ignore1_path = this_test_path.Append("ignore1");
  PopulateBacklightDir(ignore1_path, 3, 4, -1);

  base::FilePath ignore2_path = this_test_path.Append(".no::kbd_backlight");
  PopulateBacklightDir(ignore2_path, 5, 6, -1);

  InternalBacklight backlight;
  ASSERT_TRUE(backlight.Init(this_test_path, "*:kbd_backlight"));

  EXPECT_EQ(1, backlight.GetCurrentBrightnessLevel());
  EXPECT_EQ(2, backlight.GetMaxBrightnessLevel());
}

TEST_F(InternalBacklightTest, Transitions) {
  const int kMaxBrightness = 100;
  base::FilePath backlight_dir = test_path_.Append("transitions_test");
  PopulateBacklightDir(backlight_dir, 50, kMaxBrightness, 50);

  InternalBacklight backlight;
  const base::TimeTicks kStartTime = base::TimeTicks::FromInternalValue(10000);
  backlight.clock()->set_current_time_for_testing(kStartTime);
  ASSERT_TRUE(backlight.Init(test_path_, "*"));

  // An instant transition to the maximum level shouldn't use a timer.
  backlight.SetBrightnessLevel(kMaxBrightness, base::TimeDelta());
  EXPECT_FALSE(backlight.transition_timer_is_running());
  EXPECT_EQ(kMaxBrightness, ReadBrightness(backlight_dir));
  EXPECT_EQ(kMaxBrightness, backlight.GetCurrentBrightnessLevel());

  // Start a transition to the backlight's halfway point.
  const int64_t kHalfBrightness = kMaxBrightness / 2;
  const base::TimeDelta kDuration = base::TimeDelta::FromMilliseconds(1000);
  backlight.SetBrightnessLevel(kHalfBrightness, kDuration);

  // If the timeout fires at this point, we should still be at the maximum
  // level.
  EXPECT_TRUE(backlight.transition_timer_is_running());
  EXPECT_EQ(kStartTime.ToInternalValue(),
            backlight.transition_timer_start_time().ToInternalValue());
  EXPECT_TRUE(backlight.TriggerTransitionTimeoutForTesting());
  EXPECT_EQ(kMaxBrightness, ReadBrightness(backlight_dir));
  EXPECT_EQ(kMaxBrightness, backlight.GetCurrentBrightnessLevel());

  // Let half of the transition duration pass.
  const base::TimeTicks kMidpointTime = kStartTime + kDuration / 2;
  backlight.clock()->set_current_time_for_testing(kMidpointTime);
  EXPECT_TRUE(backlight.TriggerTransitionTimeoutForTesting());
  const int64_t kMidpointBrightness = (kMaxBrightness + kHalfBrightness) / 2;
  EXPECT_EQ(kMidpointBrightness, ReadBrightness(backlight_dir));
  EXPECT_EQ(kMidpointBrightness, backlight.GetCurrentBrightnessLevel());

  // At the end of the transition, we should return false to cancel the timeout.
  const base::TimeTicks kEndTime = kStartTime + kDuration;
  backlight.clock()->set_current_time_for_testing(kEndTime);
  EXPECT_FALSE(backlight.TriggerTransitionTimeoutForTesting());
  EXPECT_FALSE(backlight.transition_timer_is_running());
  EXPECT_EQ(kHalfBrightness, ReadBrightness(backlight_dir));
  EXPECT_EQ(kHalfBrightness, backlight.GetCurrentBrightnessLevel());
}

TEST_F(InternalBacklightTest, InterruptTransition) {
  // Make the backlight start at its max level.
  const int kMaxBrightness = 100;
  base::FilePath backlight_dir = test_path_.Append("backlight");
  PopulateBacklightDir(
      backlight_dir, kMaxBrightness, kMaxBrightness, kMaxBrightness);
  InternalBacklight backlight;
  backlight.clock()->set_current_time_for_testing(
      base::TimeTicks::FromInternalValue(10000));
  ASSERT_TRUE(backlight.Init(test_path_, "*"));

  // Start a one-second transition to 0.
  const base::TimeDelta kDuration = base::TimeDelta::FromSeconds(1);
  backlight.SetBrightnessLevel(0, kDuration);

  // Let the timer fire after half a second. The backlight should be at half
  // brightness.
  backlight.clock()->set_current_time_for_testing(
      backlight.clock()->GetCurrentTime() + kDuration / 2);
  EXPECT_TRUE(backlight.TriggerTransitionTimeoutForTesting());
  const int kHalfBrightness = kMaxBrightness / 2;
  EXPECT_EQ(kHalfBrightness, ReadBrightness(backlight_dir));

  // If a request to use half brightness arrives at this point, the timer should
  // be stopped.
  backlight.SetBrightnessLevel(kHalfBrightness, kDuration);
  EXPECT_FALSE(backlight.transition_timer_is_running());
  EXPECT_EQ(kHalfBrightness, ReadBrightness(backlight_dir));

  // Set the brightness to 0 immediately and start a one-second transition to
  // the maximum level.
  backlight.SetBrightnessLevel(0, base::TimeDelta());
  const base::TimeTicks kInterruptedTransitionStartTime =
      backlight.clock()->GetCurrentTime();
  backlight.SetBrightnessLevel(kMaxBrightness, kDuration);
  EXPECT_TRUE(backlight.transition_timer_is_running());
  EXPECT_EQ(0, ReadBrightness(backlight_dir));

  // At the halfway point, interrupt the transition with a new request to go to
  // 75% of the max over a second.
  backlight.clock()->set_current_time_for_testing(
      backlight.clock()->GetCurrentTime() + kDuration / 2);
  EXPECT_TRUE(backlight.TriggerTransitionTimeoutForTesting());
  EXPECT_EQ(kHalfBrightness, ReadBrightness(backlight_dir));
  const int kThreeQuartersBrightness =
      kHalfBrightness + (kMaxBrightness - kHalfBrightness) / 2;
  backlight.SetBrightnessLevel(kThreeQuartersBrightness, kDuration);
  EXPECT_EQ(kHalfBrightness, ReadBrightness(backlight_dir));

  // Since the timer was already running, it shouldn't be restarted.
  EXPECT_EQ(kInterruptedTransitionStartTime.ToInternalValue(),
            backlight.transition_timer_start_time().ToInternalValue());
  EXPECT_TRUE(backlight.transition_timer_is_running());

  // After a second, the backlight should be at 75% and the timer should stop.
  backlight.clock()->set_current_time_for_testing(
      backlight.clock()->GetCurrentTime() + kDuration);
  EXPECT_FALSE(backlight.TriggerTransitionTimeoutForTesting());
  EXPECT_EQ(kThreeQuartersBrightness, ReadBrightness(backlight_dir));
}

TEST_F(InternalBacklightTest, BlPower) {
  const int kMaxBrightness = 100;
  const base::FilePath kDir = test_path_.Append("backlight");
  PopulateBacklightDir(kDir, kMaxBrightness, kMaxBrightness, kMaxBrightness);

  const base::FilePath kPowerFile =
      kDir.Append(InternalBacklight::kBlPowerFilename);
  ASSERT_EQ(0, base::WriteFile(kPowerFile, "", 0));

  // The bl_power file shouldn't be touched initially.
  InternalBacklight backlight;
  backlight.clock()->set_current_time_for_testing(
      base::TimeTicks::FromInternalValue(10000));
  ASSERT_TRUE(backlight.Init(test_path_, "*"));
  std::string data;
  ASSERT_TRUE(base::ReadFileToString(kPowerFile, &data));
  EXPECT_EQ("", data);

  // A transition from nonzero brightness to 0 should result in
  // FB_BLANK_POWERDOWN being written to bl_power. This should happen before the
  // updated level is written, but there's no easy way to test this without
  // adding yet another delegate.
  backlight.SetBrightnessLevel(0, base::TimeDelta());
  EXPECT_EQ(FB_BLANK_POWERDOWN, ReadFile(kPowerFile));

  // When the brightness goes from 0 to nonzero, FB_BLANK_UNBLANK should be written to
  // bl_power.
  backlight.SetBrightnessLevel(1, base::TimeDelta());
  EXPECT_EQ(FB_BLANK_UNBLANK, ReadFile(kPowerFile));

  // Clear the file and check that it doesn't get rewritten when moving between
  // two nonzero levels.
  const std::string kUnblankValue = base::StringPrintf("%d", FB_BLANK_UNBLANK);
  ASSERT_EQ(0, base::WriteFile(kPowerFile, "", 0));
  backlight.SetBrightnessLevel(kMaxBrightness, base::TimeDelta());
  ASSERT_TRUE(base::ReadFileToString(kPowerFile, &data));
  EXPECT_EQ("", data);
  ASSERT_EQ(base::WriteFile(kPowerFile, kUnblankValue.c_str(), kUnblankValue.size()), 1);

  // Now do an animated transition. bl_power should remain at FB_BLANK_UNBLANK
  // until the backlight level reaches 0.
  const base::TimeDelta kDuration = base::TimeDelta::FromSeconds(1);
  backlight.SetBrightnessLevel(0, kDuration);
  EXPECT_EQ(FB_BLANK_UNBLANK, ReadFile(kPowerFile));

  backlight.clock()->set_current_time_for_testing(
      backlight.clock()->GetCurrentTime() + kDuration / 2);
  ASSERT_TRUE(backlight.TriggerTransitionTimeoutForTesting());
  ASSERT_NE(0, ReadBrightness(kDir));
  EXPECT_EQ(FB_BLANK_UNBLANK, ReadFile(kPowerFile));

  backlight.clock()->set_current_time_for_testing(
      backlight.clock()->GetCurrentTime() + kDuration / 2);
  ASSERT_FALSE(backlight.TriggerTransitionTimeoutForTesting());
  ASSERT_EQ(0, ReadBrightness(kDir));
  EXPECT_EQ(FB_BLANK_POWERDOWN, ReadFile(kPowerFile));

  // Animate back to the max brightness and check that bl_power goes to 1 in
  // conjunction with the first nonzero level.
  backlight.SetBrightnessLevel(kMaxBrightness, kDuration);
  EXPECT_EQ(FB_BLANK_POWERDOWN, ReadFile(kPowerFile));

  backlight.clock()->set_current_time_for_testing(
      backlight.clock()->GetCurrentTime() + kDuration / 2);
  ASSERT_TRUE(backlight.TriggerTransitionTimeoutForTesting());
  ASSERT_NE(0, ReadBrightness(kDir));
  EXPECT_EQ(FB_BLANK_UNBLANK, ReadFile(kPowerFile));
}

}  // namespace system
}  // namespace power_manager
