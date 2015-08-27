// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debugd/src/cpu_info_parser.h"

#include <sstream>  // NOLINT
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>

namespace {

// The filename that contains CPU information.
const char kDefaultCPUInfoFilename[] = "/proc/cpuinfo";

// The delimiter used in the CPU information filename to separate keys and
// values.
const char kCPUInfoKeyValueDelimiter = ':';

}  // namespace

namespace debugd {

CPUInfoParser::CPUInfoParser() : CPUInfoParser(kDefaultCPUInfoFilename) {}

CPUInfoParser::CPUInfoParser(const std::string& cpuinfo_filename) {
  CHECK(base::ReadFileToString(base::FilePath(cpuinfo_filename), &contents_));
}

CPUInfoParser::CPUInfoParser(
    SetContentsType set_contents, const std::string& contents)
  : contents_(contents) {
}

bool CPUInfoParser::GetKey(const std::string& key, std::string* value) const {
  std::stringstream infile(contents_);
  std::string line;
  while (std::getline(infile, line)) {
    std::vector<std::string> tokens;
    base::SplitString(line, kCPUInfoKeyValueDelimiter, &tokens);
    if (!tokens.size())
      continue;
    base::TrimWhitespace(tokens[0], base::TRIM_ALL, &tokens[0]);
    if (tokens[0] == key) {
      *value = tokens[1];
      return true;
    }
  }
  return false;
}

}  // namespace debugd
