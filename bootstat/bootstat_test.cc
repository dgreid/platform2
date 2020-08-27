// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bootstat/bootstat.h"
#include "bootstat/bootstat_test.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include <gtest/gtest.h>

namespace {

using std::string;

static void RemoveFile(const string& file_path) {
  EXPECT_EQ(0, unlink(file_path.c_str()))
      << "RemoveFile unlink() " << file_path << ": " << strerror(errno);
}

// Class to track and test the data associated with a single event.
// The primary function is TestLogEvent():  This method wraps calls
// to bootstat_log() with code to track the expected contents of the
// event files.  After logging, the expected content is tested
// against the actual content.
class EventTracker {
 public:
  EventTracker(const string& name,
               const string& uptime_prefix,
               const string& disk_prefix);
  void TestLogEvent(const string& uptime, const string& diskstats);
  void TestLogSymlink(const string& dirname, bool create_target);
  void Reset();

 private:
  string event_name_;
  string uptime_file_name_;
  string uptime_content_;
  string diskstats_file_name_;
  string diskstats_content_;
};

EventTracker::EventTracker(const string& name,
                           const string& uptime_prefix,
                           const string& diskstats_prefix)
    : event_name_(name), uptime_content_(""), diskstats_content_("") {
  string truncated_name = event_name_.substr(0, BOOTSTAT_MAX_EVENT_LEN - 1);
  uptime_file_name_ = uptime_prefix + truncated_name;
  diskstats_file_name_ = diskstats_prefix + truncated_name;
}

// Basic helper function to test whether the contents of the
// specified file exactly match the given contents string.
static void ValidateEventFileContents(const string& file_name,
                                      const string& file_contents) {
  int rv = access(file_name.c_str(), W_OK);
  EXPECT_EQ(0, rv) << "ValidateEventFileContents access(): " << file_name
                   << " is not writable: " << strerror(errno) << ".";

  rv = access(file_name.c_str(), R_OK);
  ASSERT_EQ(0, rv) << "ValidateEventFileContents access(): " << file_name
                   << " is not readable: " << strerror(errno) << ".";

  char* buffer = new char[file_contents.length() + 1];
  int fd = open(file_name.c_str(), O_RDONLY);
  rv = read(fd, buffer, file_contents.length());
  EXPECT_EQ(file_contents.length(), rv)
      << "ValidateEventFileContents read() failed.";

  buffer[file_contents.length()] = '\0';
  string actual_contents(buffer);
  EXPECT_EQ(file_contents, actual_contents)
      << "ValidateEventFileContents content mismatch.";

  rv = read(fd, buffer, 1);
  EXPECT_EQ(0, rv) << "ValidateEventFileContents found data "
                      "in event file past expected EOF";

  (void)close(fd);
  delete[] buffer;
}

// Call bootstat_log() once, and update the expected content for
// this event.  Test that the new content of the event's files
// matches the updated expected content.
void EventTracker::TestLogEvent(const string& uptime, const string& diskstats) {
  bootstat_log(event_name_.c_str());
  uptime_content_ += uptime;
  diskstats_content_ += diskstats;
  ValidateEventFileContents(uptime_file_name_, uptime_content_);
  ValidateEventFileContents(diskstats_file_name_, diskstats_content_);
}

static void MakeSymlink(const string& linkname, const string& filename) {
  int rv = symlink(linkname.c_str(), filename.c_str());
  ASSERT_EQ(0, rv) << "MakeSymlink symlink() failed to make " << linkname
                   << " point to  " << filename << ": " << strerror(errno)
                   << ".";
}

static void CreateSymlinkTarget(const string& filename) {
  const mode_t kFileCreationMode =
      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  int fd = creat(filename.c_str(), kFileCreationMode);
  ASSERT_GE(fd, 0) << "CreateSymlinkTarget creat(): " << filename << ": "
                   << strerror(errno) << ".";
  (void)close(fd);
}

static void TestSymlinkTarget(const string& filename, bool expect_exists) {
  int fd = open(filename.c_str(), O_RDONLY);
  if (expect_exists) {
    EXPECT_GE(fd, 0) << "TestSymlinkTarget open(): " << filename << ": "
                     << strerror(errno) << ".";
    char buff[8];
    ssize_t nbytes = read(fd, buff, sizeof(buff));
    EXPECT_EQ(0, nbytes) << "TestSymlinkTarget read(): nbytes = " << nbytes
                         << ".";
  } else {
    EXPECT_LT(fd, 0) << "TestSymlinkTarget open(): " << filename
                     << ": success was not expected";
  }
}

// Test calling bootstat_log() when the event files are symlinks.
// Calls to log events in this case are expected to produce no
// change in the file system.
//
// The test creates the necessary symlinks for the events, and
// optionally creates targets for the files.
void EventTracker::TestLogSymlink(const string& dirname, bool create_target) {
  string uptime_linkname("uptime.symlink");
  string diskstats_linkname("disk.symlink");

  MakeSymlink(uptime_linkname, uptime_file_name_);
  MakeSymlink(diskstats_linkname, diskstats_file_name_);
  if (create_target) {
    CreateSymlinkTarget(uptime_file_name_);
    CreateSymlinkTarget(diskstats_file_name_);
  }

  bootstat_log(event_name_.c_str());

  TestSymlinkTarget(uptime_file_name_, create_target);
  TestSymlinkTarget(diskstats_file_name_, create_target);

  if (create_target) {
    RemoveFile(dirname + "/" + uptime_linkname);
    RemoveFile(dirname + "/" + diskstats_linkname);
  }
}

// Reset event state back to initial conditions, by deleting the
// associated event files, and clearing the expected contents.
void EventTracker::Reset() {
  uptime_content_.clear();
  diskstats_content_.clear();
  RemoveFile(diskstats_file_name_);
  RemoveFile(uptime_file_name_);
}

// Bootstat test class.  We use this class to override the
// dependencies in bootstat_log() on the file paths for /proc/uptime
// and /sys/block/<device>/stat.
//
// The class uses test-specific interfaces that change the default
// paths from the kernel statistics psuedo-files to temporary paths
// selected by this test.  This class also redirects the location for
// the event files created by bootstat_log() to a temporary directory.
class BootstatTest : public ::testing::Test {
 protected:
  virtual void SetUp();
  virtual void TearDown();

  EventTracker MakeEvent(const string& event_name) {
    return EventTracker(event_name, uptime_event_prefix_, disk_event_prefix_);
  }

  void SetMockStats(const char* uptime_content, const char* disk_content);
  void ClearMockStats();
  void TestLogEvent(EventTracker* event);

  string stats_output_dir_;

 private:
  string uptime_event_prefix_;
  string disk_event_prefix_;

  string mock_uptime_file_name_;
  string mock_uptime_content_;
  string mock_disk_file_name_;
  string mock_disk_content_;
};

void BootstatTest::SetUp() {
  char dir_template[] = "bootstat_test_XXXXXX";
  stats_output_dir_ = string(mkdtemp(dir_template));
  uptime_event_prefix_ = stats_output_dir_ + "/uptime-";
  disk_event_prefix_ = stats_output_dir_ + "/disk-";
  mock_uptime_file_name_ = stats_output_dir_ + "/proc_uptime";
  mock_disk_file_name_ = stats_output_dir_ + "/block_stats";
  bootstat_set_output_directory_for_test(stats_output_dir_.c_str());
}

void BootstatTest::TearDown() {
  bootstat_set_output_directory_for_test(nullptr);
  EXPECT_EQ(0, rmdir(stats_output_dir_.c_str()))
      << "BootstatTest::Teardown rmdir(): " << stats_output_dir_ << ": "
      << strerror(errno) << ".";
}

static void WriteMockStats(const string& content, const string& file_path) {
  const int kFileOpenFlags = O_WRONLY | O_TRUNC | O_CREAT;
  const mode_t kFileCreationMode =
      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
  int fd = open(file_path.c_str(), kFileOpenFlags, kFileCreationMode);
  int nwrite = write(fd, content.c_str(), content.length());
  EXPECT_EQ(content.length(), nwrite) << "WriteMockStats write(): " << file_path
                                      << ": " << strerror(errno) << ".";
  (void)close(fd);
}

// Set the content of the files mocking the contents of the kernel's
// statistics pseudo-files.  The strings provided here will be the
// ones recorded for subsequent calls to bootstat_log() for all
// events.
void BootstatTest::SetMockStats(const char* uptime_data,
                                const char* disk_data) {
  mock_uptime_content_ = string(uptime_data);
  WriteMockStats(mock_uptime_content_, mock_uptime_file_name_);
  mock_disk_content_ = string(disk_data);
  WriteMockStats(mock_disk_content_, mock_disk_file_name_);
  bootstat_set_uptime_file_name_for_test(mock_uptime_file_name_.c_str());
  bootstat_set_disk_file_name_for_test(mock_disk_file_name_.c_str());
}

// Clean up the effects from SetMockStats().
void BootstatTest::ClearMockStats() {
  bootstat_set_uptime_file_name_for_test(nullptr);
  bootstat_set_disk_file_name_for_test(nullptr);
  RemoveFile(mock_uptime_file_name_);
  RemoveFile(mock_disk_file_name_);
}

void BootstatTest::TestLogEvent(EventTracker* event) {
  event->TestLogEvent(mock_uptime_content_, mock_disk_content_);
}

// Test data to be used as input to SetMockStats().
//
// The structure of this array is pairs of strings, terminated by a
// single NULL.  The first string in the pair is content for
// /proc/uptime, the second for /sys/block/<device>/stat.
//
// This data is taken directly from a development system, and is
// representative of valid stats content, though not typical of what
// would be seen immediately after boot.
static const char* bootstat_data[] = {
    /*  0  */
    /* uptime */ "691448.42 11020440.26\n",
    /*  disk  */
    " 1417116    14896 55561564 10935990  4267850 78379879"
    " 661568738 1635920520      158 17856450 1649520570\n",
    /*  1  */
    /* uptime */ "691623.71 11021372.99\n",
    /*  disk  */
    " 1420714    14918 55689988 11006390  4287385 78594261"
    " 663441564 1651579200      152 17974280 1665255160\n",
    /* EOT */ nullptr};

// Tests that event file content matches expectations when an
// event is logged multiple times.
TEST_F(BootstatTest, ContentGeneration) {
  EventTracker ev = MakeEvent(string("test_event"));
  int i = 0;
  while (bootstat_data[i] != nullptr) {
    SetMockStats(bootstat_data[i], bootstat_data[i + 1]);
    TestLogEvent(&ev);
    i += 2;
  }
  ClearMockStats();
  ev.Reset();
}

// Tests that name truncation of logged events works as advertised.
TEST_F(BootstatTest, EventNameTruncation) {
  // clang-format off
  static const char kMostVoluminousEventName[] =
    //             16              32              48              64
    "event-6789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef"  //  64
    "=064+56789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef"  // 128
    "=128+56789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef"  // 191
    "=191+56789abcdef_123456789ABCDEF.123456789abcdef0123456789abcdef";  // 256
  // clang-format on

  string very_long(kMostVoluminousEventName);
  SetMockStats(bootstat_data[0], bootstat_data[1]);

  EventTracker ev = MakeEvent(very_long);
  TestLogEvent(&ev);
  ev.Reset();
  ev = MakeEvent(very_long.substr(0, 1));
  TestLogEvent(&ev);
  ev.Reset();
  ev = MakeEvent(very_long.substr(0, BOOTSTAT_MAX_EVENT_LEN - 1));
  TestLogEvent(&ev);
  ev.Reset();
  ev = MakeEvent(very_long.substr(0, BOOTSTAT_MAX_EVENT_LEN));
  TestLogEvent(&ev);
  ev.Reset();

  ClearMockStats();
}

// Test that event logging does not follow symbolic links.
TEST_F(BootstatTest, SymlinkFollow) {
  EventTracker ev = MakeEvent("symlink-no-follow");
  ev.TestLogSymlink(stats_output_dir_, true);
  ev.Reset();
  ev.TestLogSymlink(stats_output_dir_, false);
  ev.Reset();
}

}  // namespace
