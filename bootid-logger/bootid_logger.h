// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BOOTID_LOGGER_BOOTID_LOGGER_H_
#define BOOTID_LOGGER_BOOTID_LOGGER_H_

#include <string>

#include <base/files/file_path.h>
#include <base/time/time.h>

constexpr size_t kBootIdLength = 32u;

bool WriteBootEntry(const base::FilePath& bootid_log_path,
                    const std::string& current_boot_id,
                    const base::Time boot_time,
                    const int max_entries);

#endif  // BOOTID_LOGGER_BOOTID_LOGGER_H_
