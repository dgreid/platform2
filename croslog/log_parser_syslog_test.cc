// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/log_parser_syslog.h"

#include <memory>
#include <string>
#include <utility>

#include "base/files/file_path.h"
#include "gtest/gtest.h"

#include "croslog/log_line_reader.h"

namespace croslog {

class LogParserSyslogTest : public ::testing::Test {
 public:
  LogParserSyslogTest() = default;

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

 private:
  DISALLOW_COPY_AND_ASSIGN(LogParserSyslogTest);
};

TEST_F(LogParserSyslogTest, Parse) {
  LogParserSyslog parser;
  LogLineReader reader(LogLineReader::Backend::FILE);
  reader.OpenFile(base::FilePath("./testdata/TEST_NORMAL_LOG1"));
  {
    base::Optional<std::string> maybe_line = reader.Forward();
    EXPECT_TRUE(maybe_line.has_value());
    MaybeLogEntry e = parser.Parse(std::move(maybe_line.value()));
    EXPECT_TRUE(e.has_value());
    const std::string& s = e->entire_line();
    EXPECT_GT(s.size(), 32);
    EXPECT_EQ("2020-05-25T14:15:22.402258+09:00", s.substr(0, 32));
    EXPECT_EQ(TimeFromExploded(2020, 5, 25, 14, 15, 22, 402258, +9), e->time());
  }

  {
    base::Optional<std::string> maybe_line = reader.Forward();
    EXPECT_TRUE(maybe_line.has_value());
    MaybeLogEntry e = parser.Parse(std::move(maybe_line.value()));
    EXPECT_TRUE(e.has_value());
    const std::string& s = e->entire_line();
    EXPECT_GT(s.size(), 32);
    EXPECT_EQ("2020-05-25T14:15:22.402260+09:00", s.substr(0, 32));
    EXPECT_EQ(TimeFromExploded(2020, 5, 25, 14, 15, 22, 402260, +9), e->time());
  }
}

}  // namespace croslog
