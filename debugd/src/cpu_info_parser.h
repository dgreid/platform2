// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DEBUGD_SRC_CPU_INFO_PARSER_H_
#define DEBUGD_SRC_CPU_INFO_PARSER_H_

#include <string>

namespace debugd {

// CPUInfoParser is a class that can be used to parse /proc/cpuinfo and gather
// data from it.
//
// Example:
//
// CPUInfoParser cpu_info_parser;
// std::string cpu_model_name;
// cpu_info_parser.GetKey("model name", &cpu_model_name);
// cpu_model_name should now contain something like "Intel (R) Celeron(R) CPU".
class CPUInfoParser {
 public:
  // Reads the contents of /proc/cpuinfo.
  CPUInfoParser();

  // Reads the contents of |cpuinfo_filename|
  explicit CPUInfoParser(const std::string& cpuinfo_filename);

  // Sets the contents from |contents| without reading a file.
  enum SetContentsType { SET_CONTENTS };
  CPUInfoParser(SetContentsType set_contents, const std::string& contents);

  // Get particular key from the cached cpu information file.
  bool GetKey(const std::string& key, std::string* value) const;

 private:
  // The entire /proc/cpuinfo contents.
  std::string contents_;
};

}  // namespace debugd

#endif  // DEBUGD_SRC_CPU_INFO_PARSER_H_

