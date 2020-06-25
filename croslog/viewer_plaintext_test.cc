// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "croslog/viewer_plaintext.h"

#include <gtest/gtest.h>

namespace croslog {

class ViewerPlaintextTest : public ::testing::Test {
 public:
  ViewerPlaintextTest() = default;

 private:
  DISALLOW_COPY_AND_ASSIGN(ViewerPlaintextTest);
};

TEST_F(ViewerPlaintextTest, ShouldFilterOutEntry) {
  {
    Config c;
    LogEntry e{base::Time::Now(), Severity::ERROR, "TAG", 1234,
               "MESSAGE",         "ENTIRE STRING"};

    ViewerPlaintext v(c);
    EXPECT_FALSE(v.ShouldFilterOutEntry(e));
  }

  {
    Config c;
    c.identifier = "TAG1";
    LogEntry e1{base::Time::Now(), Severity::ERROR, "TAG1", 1234,
                "MESSAGE",         "ENTIRE STRING"};
    LogEntry e2{base::Time::Now(), Severity::ERROR, "TAG2", 1234,
                "MESSAGE",         "ENTIRE STRING"};

    ViewerPlaintext v(c);
    EXPECT_FALSE(v.ShouldFilterOutEntry(e1));
    EXPECT_TRUE(v.ShouldFilterOutEntry(e2));
  }

  {
    Config c;
    c.severity = Severity::ERROR;
    LogEntry e1{base::Time::Now(), Severity::WARNING, "TAG", 1234,
                "MESSAGE",         "ENTIRE STRING"};
    LogEntry e2{base::Time::Now(), Severity::ERROR, "TAG", 1234,
                "MESSAGE",         "ENTIRE STRING"};
    LogEntry e3{base::Time::Now(), Severity::CRIT, "TAG", 1234,
                "MESSAGE",         "ENTIRE STRING"};

    ViewerPlaintext v(c);
    EXPECT_TRUE(v.ShouldFilterOutEntry(e1));
    EXPECT_FALSE(v.ShouldFilterOutEntry(e2));
    EXPECT_FALSE(v.ShouldFilterOutEntry(e3));
  }

  {
    Config c;
    c.grep = "M.....E";
    LogEntry e1{base::Time::Now(), Severity::ERROR, "TAG", 1234,
                "MESSAGE",         "ENTIRE STRING"};
    LogEntry e2{base::Time::Now(), Severity::ERROR, "TAG", 1234,
                "xxMESSAGE",       "ENTIRE STRING"};
    LogEntry e3{base::Time::Now(), Severity::ERROR, "TAG", 1234,
                "MESSAGExx",       "ENTIRE STRING"};
    LogEntry e4{base::Time::Now(), Severity::ERROR, "TAG", 1234,
                "xxMESSAGExx",     "ENTIRE STRING"};
    LogEntry e5{base::Time::Now(), Severity::ERROR, "TAG", 1234,
                "message",         "ENTIRE STRING"};

    ViewerPlaintext v(c);
    EXPECT_FALSE(v.ShouldFilterOutEntry(e1));
    EXPECT_FALSE(v.ShouldFilterOutEntry(e2));
    EXPECT_FALSE(v.ShouldFilterOutEntry(e3));
    EXPECT_FALSE(v.ShouldFilterOutEntry(e4));
    EXPECT_TRUE(v.ShouldFilterOutEntry(e5));
  }
}

}  // namespace croslog
