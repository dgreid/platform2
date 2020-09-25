// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <deque>
#include <fstream>
#include <sstream>
#include <string>

#include <fcntl.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "base/strings/stringprintf.h"
#include "base/time/time.h"

namespace {

constexpr char kBootLogFile[] = "/var/log/boot_id.log";
constexpr size_t kBootLogMaxEntries = 500;

std::string GetBootEntryString() {
  struct timespec boot_timespec;
  if (clock_gettime(CLOCK_BOOTTIME, &boot_timespec) == -1) {
    perror("clock_gettime");
    exit(EXIT_FAILURE);
  }

  base::Time boot_time =
      base::Time::Now() - base::TimeDelta::FromTimeSpec(boot_timespec);

  base::Time::Exploded exploded;
  boot_time.LocalExplode(&exploded);

  struct tm lt = {0};
  time_t milliseconds = boot_time.ToTimeT();
  localtime_r(&milliseconds, &lt);
  int32_t timezone_offset_sec = lt.tm_gmtoff;

  std::string boot_time_str(base::StringPrintf(
      "%04d-%02d-%02dT%02d:%02d:%02d.%03d000%+03d:%02d", exploded.year,
      exploded.month, exploded.day_of_month, exploded.hour, exploded.minute,
      exploded.second, exploded.millisecond, (timezone_offset_sec / 3600),
      ((std::abs(timezone_offset_sec) / 60) % 60)));

  std::string boot_id;
  if (std::ifstream("/proc/sys/kernel/random/boot_id", std::ios::in) >>
      boot_id) {
    boot_id.erase(std::remove(boot_id.begin(), boot_id.end(), '-'),
                  boot_id.end());
  }

  std::string boot_entry_str(base::StringPrintf(
      "%s INFO boot_id: %s", boot_time_str.c_str(), boot_id.c_str()));

  return boot_entry_str;
}

void WriteLine(const int fd, const std::string& line) {
  if (write(fd, line.c_str(), line.size()) < 0) {
    perror("write");
    exit(EXIT_FAILURE);
  }

  if (write(fd, "\n", 1) < 0) {
    perror("write");
    exit(EXIT_FAILURE);
  }
}

std::deque<std::string> ReadPreviousBootLogs(const int fd) {
  std::deque<std::string> previous_boot_entries;

  struct stat st;
  fstat(fd, &st);
  const off_t length = st.st_size;

  if (length > 0) {
    // Here, we do mmap and stringstream to read lines.
    // We can't use ifstream here because we want to use fd for keeping reading
    // the same file in case of the file rotation and locking the file.
    char* buffer =
        static_cast<char*>(mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0));
    if (buffer == NULL) {
      perror("mmap");
      exit(EXIT_FAILURE);
    }

    // Set the buffer to the stream without copying it.
    std::istringstream ss(std::string(buffer, length));

    std::string s;
    while (std::getline(ss, s)) {
      // Skip an empty log.
      if (s.empty())
        continue;

      // Skip a duplicated log.
      if (!previous_boot_entries.empty() && previous_boot_entries.back() == s)
        continue;

      previous_boot_entries.push_back(s);
    }

    munmap(buffer, length);

    // Truncate if the logs are overflown.
    while (previous_boot_entries.size() > (kBootLogMaxEntries - 1)) {
      previous_boot_entries.pop_front();
    }
  }

  return previous_boot_entries;
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
  // Open and lock the file.
  const int fd = open(kBootLogFile, O_RDWR | O_CREAT,
                      S_IRUSR | S_IWUSR | S_IROTH | S_IRGRP);
  if (fd == -1) {
    perror("open");
    exit(EXIT_FAILURE);
  }

  if (flock(fd, LOCK_EX) == -1) {
    perror("flock");
    exit(EXIT_FAILURE);
  }

  const std::deque<std::string> previous_boot_entries =
      ReadPreviousBootLogs(fd);
  // Update the current pos to the beginning of the file.
  if (lseek(fd, 0, SEEK_SET) != 0) {
    perror("lseek");
    exit(EXIT_FAILURE);
  }

  // Shrink the file to zero.
  if (ftruncate(fd, 0) != 0) {
    perror("ftruncate");
    exit(EXIT_FAILURE);
  }

  // Rewrite the existing entries.
  for (const std::string& boot_entry : previous_boot_entries) {
    WriteLine(fd, boot_entry);
  }

  // Write a new entry.
  const std::string boot_entry_str = GetBootEntryString();
  if (previous_boot_entries.empty() ||
      previous_boot_entries.back() != boot_entry_str) {
    WriteLine(fd, boot_entry_str);
  }

  // Close the file (and unlock it automatically).
  close(fd);
}
