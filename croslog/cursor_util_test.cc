// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/cursor_util.h"

#include <memory>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "gtest/gtest.h"

#include "croslog/log_line_reader.h"

namespace croslog {

class CursorUtilTest : public ::testing::Test {
 public:
  CursorUtilTest() = default;
  CursorUtilTest(const CursorUtilTest&) = delete;
  CursorUtilTest& operator=(const CursorUtilTest&) = delete;

  static base::Time TimeFromExploded(int year,
                                     int month,
                                     int day_of_month,
                                     int hour,
                                     int minute,
                                     int second,
                                     int microsec,
                                     int timezone_hour) {
    base::Time time;
    EXPECT_TRUE(base::Time::FromUTCExploded(
        base::Time::Exploded{year, month, 0, day_of_month, hour, minute, second,
                             0},
        &time));
    time += base::TimeDelta::FromMicroseconds(microsec);
    time -= base::TimeDelta::FromHours(timezone_hour);
    return time;
  }
};

TEST_F(CursorUtilTest, Generate) {
  base::Time time1 = TimeFromExploded(2020, 5, 25, 14, 15, 22, 402258, +9);
  EXPECT_EQ("time=002F0508595AD1D2", GenerateCursor(time1));

  base::Time time2 = TimeFromExploded(2021, 10, 5, 12, 5, 43, 465480, +0);
  EXPECT_EQ("time=002F2C3021DB2E08", GenerateCursor(time2));

  base::Time time3 = TimeFromExploded(2019, 3, 14, 1, 22, 23, 2258, -9);
  EXPECT_EQ("time=002EE2A194D09E92", GenerateCursor(time3));
}

TEST_F(CursorUtilTest, Parse) {
  base::Time output;

  EXPECT_TRUE(ParseCursor("time=002F0508595AD1D2", &output));
  EXPECT_EQ(TimeFromExploded(2020, 5, 25, 14, 15, 22, 402258, +9), output);

  EXPECT_TRUE(ParseCursor("time=002F2C3021DB2E08", &output));
  EXPECT_EQ(TimeFromExploded(2021, 10, 5, 12, 5, 43, 465480, +0), output);

  EXPECT_TRUE(ParseCursor("time=002EE2A194D09E92", &output));
  EXPECT_EQ(TimeFromExploded(2019, 3, 14, 1, 22, 23, 2258, -9), output);
}

TEST_F(CursorUtilTest, ParseInvalid) {
  base::Time output;
  EXPECT_FALSE(ParseCursor("TIME=002F0508595AD1D2", &output));
  EXPECT_FALSE(ParseCursor("time=FF2F0508595AD1D2", &output));
  EXPECT_FALSE(ParseCursor("time=002F0508595AD1D2;", &output));
  EXPECT_FALSE(ParseCursor("time=2F0508595AD1D2", &output));
}

}  // namespace croslog
