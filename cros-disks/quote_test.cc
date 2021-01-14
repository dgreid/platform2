// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/quote.h"

#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace cros_disks {
namespace {

template <typename T>
std::string ToString(const T& t) {
  std::ostringstream out;
  out << quote(t) << std::flush;
  return out.str();
}

template <typename T>
std::string Redacted(const T& t) {
  std::ostringstream out;
  out << redact(t, true) << std::flush;
  return out.str();
}

TEST(Quote, StringLiteral) {
  EXPECT_EQ(ToString<char[1]>(""), "''");
  EXPECT_EQ(ToString<char[8]>(R"(a\b"c'd)"), R"('a\\b"c\'d')");
}

TEST(Quote, CStyleString) {
  EXPECT_EQ(ToString<const char*>(nullptr), "(null)");
  EXPECT_EQ(ToString<const char*>(""), "''");
  EXPECT_EQ(ToString<const char*>(R"(")"), R"('"')");
  EXPECT_EQ(ToString<const char*>(R"(\)"), R"('\\')");
  EXPECT_EQ(ToString<const char*>("'"), R"('\'')");
  EXPECT_EQ(ToString<const char*>("a"), "'a'");
  EXPECT_EQ(ToString<const char*>(R"(a\b"c'd)"), R"('a\\b"c\'d')");
}

TEST(Quote, StdString) {
  EXPECT_EQ(ToString<std::string>(""), "''");
  EXPECT_EQ(ToString<std::string>(R"(")"), R"('"')");
  EXPECT_EQ(ToString<std::string>(R"(\)"), R"('\\')");
  EXPECT_EQ(ToString<std::string>("'"), R"('\'')");
  EXPECT_EQ(ToString<std::string>("a"), "'a'");
  EXPECT_EQ(ToString<std::string>(R"(a\b"c'd)"), R"('a\\b"c\'d')");
}

TEST(Quote, FilePath) {
  EXPECT_EQ(ToString(base::FilePath("")), "''");
  EXPECT_EQ(ToString(base::FilePath(R"(")")), R"('"')");
  EXPECT_EQ(ToString(base::FilePath(R"(\)")), R"('\\')");
  EXPECT_EQ(ToString(base::FilePath("'")), R"('\'')");
  EXPECT_EQ(ToString(base::FilePath("a")), "'a'");
  EXPECT_EQ(ToString(base::FilePath(R"(a\b"c'd)")), R"('a\\b"c\'d')");
}

TEST(Quote, VectorOfStrings) {
  EXPECT_EQ(ToString<std::vector<std::string>>({}), "[]");
  EXPECT_EQ(ToString<std::vector<std::string>>({""}), "['']");
  EXPECT_EQ(ToString<std::vector<std::string>>({"a"}), "['a']");
  EXPECT_EQ(ToString<std::vector<std::string>>(
                {"", R"(")", R"(\)", "'", "a", R"(a\b"c'd)"}),
            R"(['', '"', '\\', '\'', 'a', 'a\\b"c\'d'])");
}

TEST(Redact, StringLiteral) {
  EXPECT_EQ(Redacted<char[1]>(""), "(redacted)");
  EXPECT_EQ(Redacted<char[8]>(R"(a\b"c'd)"), "(redacted)");
}

TEST(Redact, CStyleString) {
  EXPECT_EQ(Redacted<const char*>(nullptr), "(null)");
  EXPECT_EQ(Redacted<const char*>(""), "(redacted)");
  EXPECT_EQ(Redacted<const char*>("a"), "(redacted)");
}

TEST(Redact, StdString) {
  EXPECT_EQ(Redacted<std::string>(""), "(redacted)");
  EXPECT_EQ(Redacted<std::string>("a"), "(redacted)");
}

TEST(Redact, FilePath) {
  EXPECT_EQ(Redacted(base::FilePath("")), "(redacted)");
  EXPECT_EQ(Redacted(base::FilePath("a")), "(redacted)");
}

TEST(Redact, VectorOfStrings) {
  EXPECT_EQ(Redacted<std::vector<std::string>>({}), "[]");
  EXPECT_EQ(Redacted<std::vector<std::string>>({""}), "[(redacted)]");
  EXPECT_EQ(Redacted<std::vector<std::string>>({"a"}), "[(redacted)]");
  EXPECT_EQ(Redacted<std::vector<std::string>>({"", "'", "a"}),
            R"([(redacted), (redacted), (redacted)])");
}

}  // namespace
}  // namespace cros_disks
