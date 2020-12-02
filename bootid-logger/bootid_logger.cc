// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"

#include "bootid-logger/bootid_logger.h"

namespace {

// 47 bytes = timestamp 31bytes + space + fixed message 15 bytes.
constexpr size_t kBootEntryLength = 47u + kBootIdLength;

// Generate an entry in the boot entry format.
std::string GenerateBootEntryString(const std::string current_boot_id,
                                    const base::Time boot_time) {
  // Boot id must be 32 hexadecimal digits.
  CHECK_EQ(32u, current_boot_id.length());

  // TODO(crbug.com): Change the timezone from local to UTC.

  base::Time::Exploded exploded;
  boot_time.LocalExplode(&exploded);

  struct tm lt = {0};
  time_t milliseconds = boot_time.ToTimeT();
  localtime_r(&milliseconds, &lt);
  int32_t timezone_offset_sec = lt.tm_gmtoff;

  const std::string boot_time_str(base::StringPrintf(
      "%04d-%02d-%02dT%02d:%02d:%02d.%03d000%+03d:%02d", exploded.year,
      exploded.month, exploded.day_of_month, exploded.hour, exploded.minute,
      exploded.second, exploded.millisecond, (timezone_offset_sec / 3600),
      ((std::abs(timezone_offset_sec) / 60) % 60)));

  const std::string boot_id_entry =
      boot_time_str + " INFO boot_id: " + base::ToLowerASCII(current_boot_id);
  CHECK_EQ(kBootEntryLength, boot_id_entry.length());
  return boot_id_entry;
}

// Extracts the boot ID from the givin boot ID entry.
std::string ExtractBootId(const std::string& boot_id_entry) {
  if (boot_id_entry.length() != kBootEntryLength)
    return "";

  return boot_id_entry.substr(32u + 15u, kBootIdLength);
}

// Extracts the boot ID from the givin boot ID entry.
bool ValidateBootEntry(const std::string& boot_id_entry) {
  if (boot_id_entry.length() != kBootEntryLength)
    return false;

  if (boot_id_entry[32] != ' ' || boot_id_entry[37] != ' ' ||
      boot_id_entry[46] != ' ')
    return false;

  return true;
}

// Read previous entries from the log file (FD).
base::Optional<std::deque<std::string>> ReadPreviousBootEntries(
    const int fd, int boot_log_max_entries) {
  std::deque<std::string> previous_boot_entries;

  struct stat st;
  fstat(fd, &st);
  const off_t length = st.st_size;

  if (length > 0) {
    // Here, we do mmap and stringstream to read lines.
    // We can't use ifstream here because we want to use fd for keeping locking
    // on the file.
    char* buffer =
        static_cast<char*>(mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0));
    if (buffer == NULL) {
      PLOG(FATAL) << "mmap failed";
      return base::nullopt;
    }

    // Set the buffer to the stream.
    std::istringstream ss(std::string(buffer, length));

    std::string s;
    while (std::getline(ss, s)) {
      // Skip an empty log.
      if (s.empty())
        continue;

      // Skip a duplicated entry.
      if (!previous_boot_entries.empty() && previous_boot_entries.back() == s)
        continue;

      // Skip an invalid entry.
      if (!ValidateBootEntry(s))
        continue;

      previous_boot_entries.push_back(s);
    }

    munmap(buffer, length);

    // Truncate if the logs are overflown.
    while (previous_boot_entries.size() > (boot_log_max_entries - 1)) {
      previous_boot_entries.pop_front();
    }
  }

  return previous_boot_entries;
}

}  // anonymous namespace

bool WriteBootEntry(const base::FilePath& bootid_log_path,
                    const std::string& current_boot_id,
                    const base::Time boot_time,
                    const int max_entries) {
  // Open the log file.
  base::ScopedFD fd(HANDLE_EINTR(
      open(bootid_log_path.value().c_str(), O_RDWR | O_CREAT | O_CLOEXEC,
           S_IRUSR | S_IWUSR | S_IROTH | S_IRGRP /* 0644 */)));
  if (fd.get() == -1) {
    PLOG(FATAL) << "open failed";
    return false;
  }

  if (HANDLE_EINTR(flock(fd.get(), LOCK_EX)) == -1) {
    PLOG(FATAL) << "flock failed";
    return false;
  }

  auto ret = ReadPreviousBootEntries(fd.get(), max_entries);
  if (!ret.has_value()) {
    LOG(FATAL) << "Reading the log file failed";
    return false;
  }
  std::deque<std::string> previous_boot_entries = std::move(*ret);

  if (!previous_boot_entries.empty() &&
      ExtractBootId(previous_boot_entries.back()) == current_boot_id) {
    LOG(INFO) << "The current Boot ID is same as the previous one. Let's "
                 "ignore this to prevent duplication.";
    return false;
  }

  const std::string boot_entry_str =
      GenerateBootEntryString(current_boot_id, boot_time);
  previous_boot_entries.push_back(boot_entry_str);

  // Update the current pos to the beginning of the file.
  if (lseek(fd.get(), 0, SEEK_SET) != 0) {
    PLOG(FATAL) << "lseek failed";
    return false;
  }

  // Shrink the file to zero.
  if (HANDLE_EINTR(ftruncate(fd.get(), 0)) != 0) {
    PLOG(FATAL) << "ftruncate failed";
    return false;
  }

  // Rewrite the existing entries.
  for (std::string boot_entry : previous_boot_entries) {
    boot_entry.append(1, '\n');

    if (!base::WriteFileDescriptor(fd.get(), boot_entry.c_str(),
                                   boot_entry.size())) {
      PLOG(FATAL) << "Writing to the file failed";
      return false;
    }
  }

  // Automatically the file is closed and unlocked at the end of process.

  return true;
}
