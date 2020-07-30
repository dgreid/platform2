// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/smartctl_check/smartctl_check_utils.h"

#include <sstream>

#include <base/logging.h>
#include <base/strings/string_number_conversions.h>

namespace diagnostics {

// A scraper that is coupled to the format of smartctl -A.
bool ScrapeAvailableSparePercents(const std::string& output,
                                  int* available_spare_pct,
                                  int* available_spare_threshold_pct) {
  bool found_available_spare_pct = false,
       found_available_spare_threshold_pct = false;
  std::istringstream iss(output);
  std::string word;
  while (!iss.eof()) {
    iss >> word;
    if (word == "Available" && !iss.eof()) {
      iss >> word;
      if (word == "Spare:" && !iss.eof()) {
        iss >> word;
        if (word.size() > 2) {
          VLOG(1) << "Found available spare% = " << word;
          DCHECK(!found_available_spare_pct);
          int value;
          if (base::StringToInt(word.substr(0, word.size() - 1), &value)) {
            found_available_spare_pct = true;
            if (available_spare_pct)
              *available_spare_pct = value;
          }
        }
      } else if (word == "Spare" && !iss.eof()) {
        iss >> word;
        if (word == "Threshold:" && !iss.eof()) {
          iss >> word;
          if (word.size() > 2) {
            VLOG(1) << "Found available spare threshold% = " << word;
            int value;
            if (base::StringToInt(word.substr(0, word.size() - 1), &value)) {
              found_available_spare_threshold_pct = true;
              if (available_spare_threshold_pct)
                *available_spare_threshold_pct = value;
            }
          }
        }
      }
      if (found_available_spare_pct && found_available_spare_threshold_pct) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace diagnostics
