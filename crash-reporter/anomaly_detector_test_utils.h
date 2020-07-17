// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRASH_REPORTER_ANOMALY_DETECTOR_TEST_UTILS_H_
#define CRASH_REPORTER_ANOMALY_DETECTOR_TEST_UTILS_H_

#include <initializer_list>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/optional.h>

namespace anomaly {

struct CrashReport;
class Parser;

struct ParserRun {
  base::Optional<std::string> find_this = base::nullopt;
  base::Optional<std::string> replace_with = base::nullopt;
  base::Optional<std::string> expected_text = base::nullopt;
  base::Optional<std::string> expected_flag = base::nullopt;
  size_t expected_size = 1;
};

std::vector<CrashReport> ParseLogMessages(
    Parser* parser, const std::vector<std::string>& log_msgs);

void ReplaceMsgContent(std::vector<std::string>* log_msgs,
                       const std::string& find_this,
                       const std::string& replace_with);

std::vector<std::string> GetTestLogMessages(base::FilePath input_file);

void ParserTest(const std::string& input_file_name,
                std::initializer_list<ParserRun> parser_runs,
                anomaly::Parser* parser);

template <class T>
void ParserTest(const std::string& input_file_name,
                std::initializer_list<ParserRun> parser_runs) {
  T parser;
  ParserTest(input_file_name, parser_runs, &parser);
}

}  // namespace anomaly

#endif  // CRASH_REPORTER_ANOMALY_DETECTOR_TEST_UTILS_H_
