// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/crash_collector_test.h"

#include <inttypes.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <base/task/single_thread_task_executor.h>
#include <base/test/simple_test_clock.h>
#include <base/threading/thread_task_runner_handle.h>
#include <brillo/syslog_logging.h>
#include <dbus/object_path.h>
#include <dbus/message.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>
#include <metrics/metrics_library_mock.h>

#include "crash-reporter/crash_collector.h"
#include "crash-reporter/paths.h"
#include "crash-reporter/test_util.h"

using base::FilePath;
using base::StringPrintf;
using brillo::FindLog;
using ::testing::_;
using ::testing::Invoke;
using ::testing::IsEmpty;
using ::testing::Return;

// The QEMU emulator we use to run unit tests on simulated ARM boards does not
// support memfd_create. (https://bugs.launchpad.net/qemu/+bug/1734792) Skip
// tests that rely on memfd_create on ARM boards. (The tast test will still
// provide a basic sanity check.)
#if defined(ARCH_CPU_ARM_FAMILY)
#define DISABLED_ON_QEMU_FOR_MEMFD_CREATE(test_name) DISABLED_##test_name
#else
#define DISABLED_ON_QEMU_FOR_MEMFD_CREATE(test_name) test_name
#endif

namespace {

// Fake "now" timestamp in milliseconds since Unix Epoch. Corresponds to
// Sept 1, 2020, but basically arbitrary.
constexpr int64_t kFakeNow = 1598929274543LL;

}  // namespace

CrashCollectorMock::CrashCollectorMock() : CrashCollector("mock") {}
CrashCollectorMock::CrashCollectorMock(
    CrashDirectorySelectionMethod crash_directory_selection_method,
    CrashSendingMode crash_sending_mode)
    : CrashCollector(
          "mock", crash_directory_selection_method, crash_sending_mode) {}

class CrashCollectorTest : public ::testing::Test {
 public:
  void SetUp() {
    EXPECT_CALL(collector_, SetUpDBus()).WillRepeatedly(Return());

    collector_.Initialize(false);

    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());
    test_dir_ = scoped_temp_dir_.GetPath();
    // TODO(jkardatzke): Cleanup the usage of paths in here so that we use this
    // technique instead rather than setting various specific dirs.
    paths::SetPrefixForTesting(test_dir_);

    brillo::ClearLog();
  }

  bool CheckHasCapacity();

  // Body of FinishCrashInCrashLoopModeSuccessfulResponse and
  // FinishCrashInCrashLoopModeErrorResponse.
  void TestFinishCrashInCrashLoopMode(bool give_success_response);

 protected:
  CrashCollectorMock collector_;
  FilePath test_dir_;
  base::ScopedTempDir scoped_temp_dir_;
};

TEST_F(CrashCollectorTest, WriteNewFile) {
  FilePath test_file = test_dir_.Append("test_new");
  const char kBuffer[] = "buffer";
  EXPECT_EQ(strlen(kBuffer),
            collector_.WriteNewFile(test_file, kBuffer, strlen(kBuffer)));
  EXPECT_EQ(collector_.get_bytes_written(), strlen(kBuffer));
  EXPECT_LT(collector_.WriteNewFile(test_file, kBuffer, strlen(kBuffer)), 0);
  EXPECT_EQ(collector_.get_bytes_written(), strlen(kBuffer));
}

TEST_F(CrashCollectorTest,
       DISABLED_ON_QEMU_FOR_MEMFD_CREATE(CrashLoopModeCreatesInMemoryFiles)) {
  CrashCollectorMock collector(
      CrashCollector::kUseNormalCrashDirectorySelectionMethod,
      CrashCollector::kCrashLoopSendingMode);
  collector.Initialize(false);

  const char kBuffer[] = "Hello, this is buffer";
  const FilePath kPath = test_dir_.Append("buffer.txt");
  EXPECT_EQ(collector.WriteNewFile(kPath, kBuffer, strlen(kBuffer)),
            strlen(kBuffer));

  auto result = collector.get_in_memory_files_for_test();
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(std::get<0>(result[0]), "buffer.txt");
  base::File file(std::get<1>(result[0]).release());
  EXPECT_TRUE(file.IsValid());
  EXPECT_EQ(file.GetLength(), strlen(kBuffer));
  char result_buffer[100] = {'\0'};
  EXPECT_EQ(file.Read(0, result_buffer, sizeof(result_buffer)),
            strlen(kBuffer));
  EXPECT_EQ(std::string(kBuffer), std::string(result_buffer));
  // This should be an in-memory file, not a real file.
  EXPECT_FALSE(base::PathExists(kPath));
  EXPECT_EQ(collector.get_bytes_written(), strlen(kBuffer));
}

TEST_F(CrashCollectorTest,
       DISABLED_ON_QEMU_FOR_MEMFD_CREATE(
           CrashLoopModeCreatesMultipleInMemoryFiles)) {
  CrashCollectorMock collector(
      CrashCollector::kUseNormalCrashDirectorySelectionMethod,
      CrashCollector::kCrashLoopSendingMode);
  collector.Initialize(false);

  const char kBuffer1[] = "Hello, this is buffer";
  const FilePath kPath1 = test_dir_.Append("buffer1.txt");
  EXPECT_EQ(collector.WriteNewFile(kPath1, kBuffer1, strlen(kBuffer1)),
            strlen(kBuffer1));

  const char kBuffer2[] = "Another buffer";
  const FilePath kPath2 = test_dir_.Append("buffer2.txt");
  EXPECT_EQ(collector.WriteNewFile(kPath2, kBuffer2, strlen(kBuffer2)),
            strlen(kBuffer2));

  const char kBuffer3[] = "Funny meme-ish text here";
  const FilePath kPath3 = test_dir_.Append("buffer3.txt");
  EXPECT_EQ(collector.WriteNewFile(kPath3, kBuffer3, strlen(kBuffer3)),
            strlen(kBuffer3));

  auto result = collector.get_in_memory_files_for_test();
  EXPECT_EQ(result.size(), 3);
  bool found1 = false;
  bool found2 = false;
  bool found3 = false;
  // Order doesn't matter as long as they're all there.
  for (int i = 0; i < 3; i++) {
    const char* expected_buffer = nullptr;
    if (std::get<0>(result[i]) == "buffer1.txt") {
      EXPECT_FALSE(found1);
      found1 = true;
      expected_buffer = kBuffer1;
    } else if (std::get<0>(result[i]) == "buffer2.txt") {
      EXPECT_FALSE(found2);
      found2 = true;
      expected_buffer = kBuffer2;
    } else {
      EXPECT_EQ(std::get<0>(result[i]), "buffer3.txt");
      EXPECT_FALSE(found3);
      found3 = true;
      expected_buffer = kBuffer3;
    }
    base::File file(std::get<1>(result[i]).release());
    EXPECT_TRUE(file.IsValid());
    EXPECT_EQ(file.GetLength(), strlen(expected_buffer));
    char result_buffer[100] = {'\0'};
    EXPECT_EQ(file.Read(0, result_buffer, sizeof(result_buffer)),
              strlen(expected_buffer));
    EXPECT_EQ(std::string(expected_buffer), std::string(result_buffer));
  }
  // These should be an in-memory files, not a real files.
  EXPECT_FALSE(base::PathExists(kPath1));
  EXPECT_FALSE(base::PathExists(kPath2));
  EXPECT_FALSE(base::PathExists(kPath3));
  EXPECT_EQ(collector.get_bytes_written(),
            strlen(kBuffer1) + strlen(kBuffer2) + strlen(kBuffer3));
}

TEST_F(CrashCollectorTest,
       DISABLED_ON_QEMU_FOR_MEMFD_CREATE(
           CrashLoopModeWillNotCreateDuplicateFileNames)) {
  CrashCollectorMock collector(
      CrashCollector::kUseNormalCrashDirectorySelectionMethod,
      CrashCollector::kCrashLoopSendingMode);
  collector.Initialize(false);

  const FilePath kPath = test_dir_.Append("buffer.txt");
  const char kBuffer[] = "Hello, this is buffer";
  // First should succeed.
  EXPECT_EQ(collector.WriteNewFile(kPath, kBuffer, strlen(kBuffer)),
            strlen(kBuffer));
  EXPECT_EQ(collector.get_bytes_written(), strlen(kBuffer));

  // Second should fail.
  EXPECT_EQ(collector.WriteNewFile(kPath, kBuffer, strlen(kBuffer)), -1);
  EXPECT_EQ(collector.get_bytes_written(), strlen(kBuffer));

  ASSERT_EQ(collector.get_in_memory_files_for_test().size(), 1);
}

TEST_F(CrashCollectorTest, WriteNewCompressedFile) {
  FilePath test_file = test_dir_.Append("test_compressed_new.gz");
  const char kBuffer[] = "buffer";
  EXPECT_TRUE(
      collector_.WriteNewCompressedFile(test_file, kBuffer, strlen(kBuffer)));
  EXPECT_TRUE(base::PathExists(test_file));
  int64_t file_size = -1;
  EXPECT_TRUE(base::GetFileSize(test_file, &file_size));
  EXPECT_EQ(collector_.get_bytes_written(), file_size);

  int decompress_result = system(("gunzip " + test_file.value()).c_str());
  EXPECT_TRUE(WIFEXITED(decompress_result));
  EXPECT_EQ(WEXITSTATUS(decompress_result), 0);

  FilePath test_file_uncompressed = test_file.RemoveFinalExtension();
  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(test_file_uncompressed, &contents));
  EXPECT_EQ(kBuffer, contents);
}

TEST_F(CrashCollectorTest, WriteNewCompressedFileFailsIfFileExists) {
  FilePath test_file = test_dir_.Append("test_compressed_exist.gz");
  base::File touch_test_file(test_file,
                             base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  EXPECT_TRUE(touch_test_file.IsValid());
  touch_test_file.Close();

  const char kBuffer[] = "buffer";
  EXPECT_FALSE(
      collector_.WriteNewCompressedFile(test_file, kBuffer, strlen(kBuffer)));
  EXPECT_EQ(collector_.get_bytes_written(), 0);
}

TEST_F(CrashCollectorTest,
       DISABLED_ON_QEMU_FOR_MEMFD_CREATE(
           CrashLoopModeCreatesInMemoryCompressedFiles)) {
  CrashCollectorMock collector(
      CrashCollector::kUseNormalCrashDirectorySelectionMethod,
      CrashCollector::kCrashLoopSendingMode);
  collector.Initialize(false);

  const char kBuffer[] = "Hello, this is buffer";
  const FilePath kPath = test_dir_.Append("buffer.txt.gz");
  EXPECT_TRUE(
      collector.WriteNewCompressedFile(kPath, kBuffer, strlen(kBuffer)));

  // This should be an in-memory file, not a real file.
  EXPECT_FALSE(base::PathExists(kPath));

  auto result = collector.get_in_memory_files_for_test();
  ASSERT_EQ(result.size(), 1);
  EXPECT_EQ(std::get<0>(result[0]), "buffer.txt.gz");
  base::File file(std::get<1>(result[0]).release());
  EXPECT_TRUE(file.IsValid());
  char compressed_result_buffer[100] = {'\0'};
  int read_amount =
      file.Read(0, compressed_result_buffer, sizeof(compressed_result_buffer));
  ASSERT_GT(read_amount, 0);
  EXPECT_EQ(collector.get_bytes_written(), read_amount);

  // Uncompress the data.
  base::FilePath uncompressed_path = test_dir_.Append("result.txt");
  base::FilePath compressed_path = uncompressed_path.AddExtension("gz");
  base::File compressed_file(compressed_path,
                             base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  EXPECT_TRUE(compressed_file.IsValid())
      << base::File::ErrorToString(compressed_file.error_details());
  EXPECT_EQ(compressed_file.Write(0, compressed_result_buffer, read_amount),
            read_amount);
  compressed_file.Close();
  int decompress_result = system(("gunzip " + compressed_path.value()).c_str());
  EXPECT_TRUE(WIFEXITED(decompress_result));
  EXPECT_EQ(WEXITSTATUS(decompress_result), 0);

  std::string result_buffer;
  EXPECT_TRUE(base::ReadFileToString(uncompressed_path, &result_buffer));
  EXPECT_EQ(std::string(kBuffer), result_buffer);
}

TEST_F(CrashCollectorTest,
       DISABLED_ON_QEMU_FOR_MEMFD_CREATE(
           CrashLoopModeWillNotCreateDuplicateCompressedFileNames)) {
  CrashCollectorMock collector(
      CrashCollector::kUseNormalCrashDirectorySelectionMethod,
      CrashCollector::kCrashLoopSendingMode);
  collector.Initialize(false);

  const FilePath kPath = test_dir_.Append("buffer.txt.gz");
  const char kBuffer[] = "Hello, this is buffer";
  // First should succeed.
  EXPECT_TRUE(
      collector.WriteNewCompressedFile(kPath, kBuffer, strlen(kBuffer)));
  EXPECT_GT(collector.get_bytes_written(), 0);
  off_t bytes_written_after_first = collector.get_bytes_written();

  // Second should fail.
  EXPECT_FALSE(
      collector.WriteNewCompressedFile(kPath, kBuffer, strlen(kBuffer)));
  EXPECT_EQ(collector.get_bytes_written(), bytes_written_after_first);

  ASSERT_EQ(collector.get_in_memory_files_for_test().size(), 1);
}

TEST_F(CrashCollectorTest, RemoveNewFileRemovesNormalFiles) {
  const FilePath kPath = test_dir_.Append("buffer.txt");
  const char kBuffer[] = "Hello, this is buffer";
  EXPECT_EQ(strlen(kBuffer),
            collector_.WriteNewFile(kPath, kBuffer, strlen(kBuffer)));
  EXPECT_EQ(collector_.get_bytes_written(), strlen(kBuffer));
  EXPECT_TRUE(base::PathExists(kPath));

  EXPECT_TRUE(collector_.RemoveNewFile(kPath));
  EXPECT_EQ(collector_.get_bytes_written(), 0);
  EXPECT_FALSE(base::PathExists(kPath));
}

TEST_F(CrashCollectorTest, RemoveNewFileRemovesCompressedFiles) {
  const FilePath kPath = test_dir_.Append("buffer.txt.gz");
  const char kBuffer[] = "Hello, this is buffer";
  EXPECT_TRUE(
      collector_.WriteNewCompressedFile(kPath, kBuffer, strlen(kBuffer)));
  EXPECT_GT(collector_.get_bytes_written(), 0);
  EXPECT_TRUE(base::PathExists(kPath));

  EXPECT_TRUE(collector_.RemoveNewFile(kPath));
  EXPECT_EQ(collector_.get_bytes_written(), 0);
  EXPECT_FALSE(base::PathExists(kPath));
}

TEST_F(CrashCollectorTest, RemoveNewFileFailsOnNonExistantFiles) {
  const FilePath kPath = test_dir_.Append("doesnt_exist");
  EXPECT_FALSE(collector_.RemoveNewFile(kPath));
  EXPECT_EQ(collector_.get_bytes_written(), 0);
}

TEST_F(CrashCollectorTest,
       DISABLED_ON_QEMU_FOR_MEMFD_CREATE(
           RemoveNewFileRemovesNormalFilesInCrashLoopMode)) {
  CrashCollectorMock collector(
      CrashCollector::kUseNormalCrashDirectorySelectionMethod,
      CrashCollector::kCrashLoopSendingMode);
  collector.Initialize(false);

  const FilePath kPath = test_dir_.Append("buffer.txt");
  const char kBuffer[] = "Hello, this is buffer";
  EXPECT_EQ(strlen(kBuffer),
            collector.WriteNewFile(kPath, kBuffer, strlen(kBuffer)));
  EXPECT_EQ(collector.get_bytes_written(), strlen(kBuffer));

  EXPECT_TRUE(collector.RemoveNewFile(kPath));
  EXPECT_EQ(collector.get_bytes_written(), 0);
  EXPECT_THAT(collector.get_in_memory_files_for_test(), IsEmpty());
}

TEST_F(CrashCollectorTest,
       DISABLED_ON_QEMU_FOR_MEMFD_CREATE(
           RemoveNewFileRemovesCorrectFileInCrashLoopMode)) {
  CrashCollectorMock collector(
      CrashCollector::kUseNormalCrashDirectorySelectionMethod,
      CrashCollector::kCrashLoopSendingMode);
  collector.Initialize(false);

  const FilePath kPath1 = test_dir_.Append("buffer1.txt");
  const char kBuffer1[] = "Hello, this is buffer";
  EXPECT_EQ(strlen(kBuffer1),
            collector.WriteNewFile(kPath1, kBuffer1, strlen(kBuffer1)));
  const FilePath kPath2 = test_dir_.Append("buffer2.txt");
  const char kBuffer2[] =
      "And if you gaze long into an abyss, you may become the domain expert on "
      "the abyss";
  EXPECT_EQ(strlen(kBuffer2),
            collector.WriteNewFile(kPath2, kBuffer2, strlen(kBuffer2)));
  EXPECT_EQ(collector.get_bytes_written(), strlen(kBuffer1) + strlen(kBuffer2));

  EXPECT_TRUE(collector.RemoveNewFile(kPath1));
  EXPECT_EQ(collector.get_bytes_written(), strlen(kBuffer2));
  auto results = collector.get_in_memory_files_for_test();
  ASSERT_EQ(results.size(), 1);
  EXPECT_EQ(std::get<0>(results[0]), "buffer2.txt");
}

TEST_F(CrashCollectorTest,
       DISABLED_ON_QEMU_FOR_MEMFD_CREATE(
           RemoveNewFileRemovesCompressedFilesInCrashLoopMode)) {
  CrashCollectorMock collector(
      CrashCollector::kUseNormalCrashDirectorySelectionMethod,
      CrashCollector::kCrashLoopSendingMode);
  collector.Initialize(false);

  const FilePath kPath = test_dir_.Append("buffer.txt.gz");
  const char kBuffer[] = "Hello, this is buffer";
  EXPECT_TRUE(
      collector.WriteNewCompressedFile(kPath, kBuffer, strlen(kBuffer)));
  EXPECT_GT(collector.get_bytes_written(), 0);

  EXPECT_TRUE(collector.RemoveNewFile(kPath));
  EXPECT_EQ(collector.get_bytes_written(), 0);
  EXPECT_THAT(collector.get_in_memory_files_for_test(), IsEmpty());
}

TEST_F(CrashCollectorTest,
       RemoveNewFileFailsOnNonExistantFilesInCrashLoopMode) {
  CrashCollectorMock collector(
      CrashCollector::kUseNormalCrashDirectorySelectionMethod,
      CrashCollector::kCrashLoopSendingMode);
  collector.Initialize(false);

  const FilePath kPath = test_dir_.Append("doesnt_exist");
  EXPECT_FALSE(collector.RemoveNewFile(kPath));
  EXPECT_EQ(collector.get_bytes_written(), 0);
}

TEST_F(CrashCollectorTest, Sanitize) {
  EXPECT_EQ("chrome", collector_.Sanitize("chrome"));
  EXPECT_EQ("CHROME", collector_.Sanitize("CHROME"));
  EXPECT_EQ("1chrome2", collector_.Sanitize("1chrome2"));
  EXPECT_EQ("chrome__deleted_", collector_.Sanitize("chrome (deleted)"));
  EXPECT_EQ("foo_bar", collector_.Sanitize("foo.bar"));
  EXPECT_EQ("", collector_.Sanitize(""));
  EXPECT_EQ("_", collector_.Sanitize(" "));
}

TEST_F(CrashCollectorTest, StripMacAddressesBasic) {
  // Basic tests of StripSensitiveData...

  // Make sure we work OK with a string w/ no MAC addresses.
  const std::string kCrashWithNoMacsOrig =
      "<7>[111566.131728] PM: Entering mem sleep\n";
  std::string crash_with_no_macs(kCrashWithNoMacsOrig);
  collector_.StripSensitiveData(&crash_with_no_macs);
  EXPECT_EQ(kCrashWithNoMacsOrig, crash_with_no_macs);

  // Make sure that we handle the case where there's nothing before/after the
  // MAC address.
  const std::string kJustAMacOrig = "11:22:33:44:55:66";
  const std::string kJustAMacStripped = "00:00:00:00:00:01";
  std::string just_a_mac(kJustAMacOrig);
  collector_.StripSensitiveData(&just_a_mac);
  EXPECT_EQ(kJustAMacStripped, just_a_mac);

  // Test MAC addresses crammed together to make sure it gets both of them.
  //
  // I'm not sure that the code does ideal on these two test cases (they don't
  // look like two MAC addresses to me), but since we don't see them I think
  // it's OK to behave as shown here.
  const std::string kCrammedMacs1Orig = "11:22:33:44:55:66:11:22:33:44:55:66";
  const std::string kCrammedMacs1Stripped =
      "00:00:00:00:00:01:00:00:00:00:00:01";
  std::string crammed_macs_1(kCrammedMacs1Orig);
  collector_.StripSensitiveData(&crammed_macs_1);
  EXPECT_EQ(kCrammedMacs1Stripped, crammed_macs_1);

  const std::string kCrammedMacs2Orig = "11:22:33:44:55:6611:22:33:44:55:66";
  const std::string kCrammedMacs2Stripped =
      "00:00:00:00:00:0100:00:00:00:00:01";
  std::string crammed_macs_2(kCrammedMacs2Orig);
  collector_.StripSensitiveData(&crammed_macs_2);
  EXPECT_EQ(kCrammedMacs2Stripped, crammed_macs_2);

  // Test case-sensitiveness (we shouldn't be case-senstive).
  const std::string kCapsMacOrig = "AA:BB:CC:DD:EE:FF";
  const std::string kCapsMacStripped = "00:00:00:00:00:01";
  std::string caps_mac(kCapsMacOrig);
  collector_.StripSensitiveData(&caps_mac);
  EXPECT_EQ(kCapsMacStripped, caps_mac);

  const std::string kLowerMacOrig = "aa:bb:cc:dd:ee:ff";
  const std::string kLowerMacStripped = "00:00:00:00:00:01";
  std::string lower_mac(kLowerMacOrig);
  collector_.StripSensitiveData(&lower_mac);
  EXPECT_EQ(kLowerMacStripped, lower_mac);
}

TEST_F(CrashCollectorTest, StripMacAddressesBulk) {
  // Test calling StripSensitiveData w/ lots of MAC addresses in the "log".

  // Test that stripping code handles more than 256 unique MAC addresses, since
  // that overflows past the last byte...
  // We'll write up some code that generates 258 unique MAC addresses.  Sorta
  // cheating since the code is very similar to the current code in
  // StripSensitiveData(), but would catch if someone changed that later.
  std::string lotsa_macs_orig;
  std::string lotsa_macs_stripped;
  int i;
  for (i = 0; i < 258; i++) {
    lotsa_macs_orig +=
        StringPrintf(" 11:11:11:11:%02X:%02x", (i & 0xff00) >> 8, i & 0x00ff);
    lotsa_macs_stripped += StringPrintf(
        " 00:00:00:00:%02X:%02x", ((i + 1) & 0xff00) >> 8, (i + 1) & 0x00ff);
  }
  std::string lotsa_macs(lotsa_macs_orig);
  collector_.StripMacAddresses(&lotsa_macs);
  EXPECT_EQ(lotsa_macs_stripped, lotsa_macs);
}

TEST_F(CrashCollectorTest, StripSensitiveDataSample) {
  // Test calling StripSensitiveData w/ some actual lines from a real crash;
  // included two MAC addresses (though replaced them with some bogusness).
  const std::string kCrashWithMacsOrig =
      "<6>[111567.195339] ata1.00: ACPI cmd ef/10:03:00:00:00:a0 (SET FEATURES)"
      " filtered out\n"
      "<7>[108539.540144] wlan0: authenticate with 11:22:33:44:55:66 (try 1)\n"
      "<7>[108539.554973] wlan0: associate with 11:22:33:44:55:66 (try 1)\n"
      "<6>[110136.587583] usb0: register 'QCUSBNet2k' at usb-0000:00:1d.7-2,"
      " QCUSBNet Ethernet Device, 99:88:77:66:55:44\n"
      "<7>[110964.314648] wlan0: deauthenticated from 11:22:33:44:55:66"
      " (Reason: 6)\n"
      "<7>[110964.325057] phy0: Removed STA 11:22:33:44:55:66\n"
      "<7>[110964.325115] phy0: Destroyed STA 11:22:33:44:55:66\n"
      "<6>[110969.219172] usb0: register 'QCUSBNet2k' at usb-0000:00:1d.7-2,"
      " QCUSBNet Ethernet Device, 99:88:77:66:55:44\n"
      "<7>[111566.131728] PM: Entering mem sleep\n";
  const std::string kCrashWithMacsStripped =
      "<6>[111567.195339] ata1.00: ACPI cmd ef/10:03:00:00:00:a0 (SET FEATURES)"
      " filtered out\n"
      "<7>[108539.540144] wlan0: authenticate with 00:00:00:00:00:01 (try 1)\n"
      "<7>[108539.554973] wlan0: associate with 00:00:00:00:00:01 (try 1)\n"
      "<6>[110136.587583] usb0: register 'QCUSBNet2k' at usb-0000:00:1d.7-2,"
      " QCUSBNet Ethernet Device, 00:00:00:00:00:02\n"
      "<7>[110964.314648] wlan0: deauthenticated from 00:00:00:00:00:01"
      " (Reason: 6)\n"
      "<7>[110964.325057] phy0: Removed STA 00:00:00:00:00:01\n"
      "<7>[110964.325115] phy0: Destroyed STA 00:00:00:00:00:01\n"
      "<6>[110969.219172] usb0: register 'QCUSBNet2k' at usb-0000:00:1d.7-2,"
      " QCUSBNet Ethernet Device, 00:00:00:00:00:02\n"
      "<7>[111566.131728] PM: Entering mem sleep\n";
  std::string crash_with_macs(kCrashWithMacsOrig);
  collector_.StripMacAddresses(&crash_with_macs);
  EXPECT_EQ(kCrashWithMacsStripped, crash_with_macs);
}

TEST_F(CrashCollectorTest, StripEmailAddresses) {
  std::string logs =
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit,"
      " sed do eiusmod tempor incididunt ut labore et dolore \n"
      "magna aliqua. Ut enim ad minim veniam, quis nostrud "
      "exercitation ullamco foo.bar+baz@secret.com laboris \n"
      "nisi ut aliquip ex ea commodo consequat. Duis aute "
      "irure dolor in reprehenderit (support@example.com) in \n"
      "voluptate velit esse cillum dolore eu fugiat nulla "
      "pariatur. Excepteur sint occaecat:abuse@dev.reallylong,\n"
      "cupidatat non proident, sunt in culpa qui officia "
      "deserunt mollit anim id est laborum.";
  collector_.StripEmailAddresses(&logs);
  EXPECT_EQ(0, logs.find("Lorem ipsum"));
  EXPECT_EQ(std::string::npos, logs.find("foo.bar"));
  EXPECT_EQ(std::string::npos, logs.find("secret"));
  EXPECT_EQ(std::string::npos, logs.find("support"));
  EXPECT_EQ(std::string::npos, logs.find("example.com"));
  EXPECT_EQ(std::string::npos, logs.find("abuse"));
  EXPECT_EQ(std::string::npos, logs.find("dev.reallylong"));
}

TEST_F(CrashCollectorTest, StripSerialNumbers) {
  // Test calling StripSensitiveData w/ some actual lines from a real crash;
  // included two MAC addresses (though replaced them with some bogusness).
  const std::string kCrashWithUsbSerialNumbers =
      "[ 1.974401] usb 1-7: new high-speed USB device number 4 using xhci_hcd\n"
      "[ 2.159587] usb 1-7: New USB device found, idVendor=2232, "
      "idProduct=1082, bcdDevice= 0.08\n"
      "[ 2.159620] usb 1-7: New USB device strings: Mfr=3, Product=1, "
      "SerialNumber=2\n"
      "[ 2.159644] usb 1-7: Product: 720p HD Camera\n"
      "[ 2.159661] usb 1-7: Manufacturer: Namuga\n"
      "[ 2.159676] usb 1-7: SerialNumber: 200901010001\n"
      "[ 2.212541] usb 1-2.1: new high-speed USB device number 5 using "
      "xhci_hcd\n"
      "[ 2.248559] Switched to clocksource tsc\n"
      "[ 2.296473] usb 1-2.1: New USB device found, idVendor=0409, "
      "idProduct=005a, bcdDevice= 1.00\n"
      "[ 2.296506] usb 1-2.1: New USB device strings: Mfr=0, Product=0, "
      "SerialNumber=0\n"
      "[ 2.297266] hub 1-2.1:1.0: USB hub found\n"
      "[ 2.297326] hub 1-2.1:1.0: 4 ports detected\n"
      "[ 2.570494] usb 1-2.1.2: new high-speed USB device number 6 using "
      "xhci_hcd\n"
      "[ 2.670246] usb 1-2.1.2: New USB device found, idVendor=13fe, "
      "idProduct=5500, bcdDevice= 1.00\n"
      "[ 2.670286] usb 1-2.1.2: New USB device strings: Mfr=1, Product=2, "
      "SerialNumber=3\n"
      "[ 2.670338] usb 1-2.1.2: Product: Patriot Memory\n"
      "[ 2.670359] usb 1-2.1.2: Manufacturer:\n"
      "[ 2.670379] usb 1-2.1.2: SerialNumber: 0701534FB0282809\n";
  const std::string kCrashWithUsbSerialNumbersStripped =
      "[ 1.974401] usb 1-7: new high-speed USB device number 4 using xhci_hcd\n"
      "[ 2.159587] usb 1-7: New USB device found, idVendor=2232, "
      "idProduct=1082, bcdDevice= 0.08\n"
      "[ 2.159620] usb 1-7: New USB device strings: Mfr=3, Product=1, "
      "<redacted serial number>\n"
      "[ 2.159644] usb 1-7: Product: 720p HD Camera\n"
      "[ 2.159661] usb 1-7: Manufacturer: Namuga\n"
      "[ 2.159676] usb 1-7: <redacted serial number>\n"
      "[ 2.212541] usb 1-2.1: new high-speed USB device number 5 using "
      "xhci_hcd\n"
      "[ 2.248559] Switched to clocksource tsc\n"
      "[ 2.296473] usb 1-2.1: New USB device found, idVendor=0409, "
      "idProduct=005a, bcdDevice= 1.00\n"
      "[ 2.296506] usb 1-2.1: New USB device strings: Mfr=0, Product=0, "
      "<redacted serial number>\n"
      "[ 2.297266] hub 1-2.1:1.0: USB hub found\n"
      "[ 2.297326] hub 1-2.1:1.0: 4 ports detected\n"
      "[ 2.570494] usb 1-2.1.2: new high-speed USB device number 6 using "
      "xhci_hcd\n"
      "[ 2.670246] usb 1-2.1.2: New USB device found, idVendor=13fe, "
      "idProduct=5500, bcdDevice= 1.00\n"
      "[ 2.670286] usb 1-2.1.2: New USB device strings: Mfr=1, Product=2, "
      "<redacted serial number>\n"
      "[ 2.670338] usb 1-2.1.2: Product: Patriot Memory\n"
      "[ 2.670359] usb 1-2.1.2: Manufacturer:\n"
      "[ 2.670379] usb 1-2.1.2: <redacted serial number>\n";
  std::string crash_with_usb_serial_numbers(kCrashWithUsbSerialNumbers);
  collector_.StripSerialNumbers(&crash_with_usb_serial_numbers);
  EXPECT_EQ(kCrashWithUsbSerialNumbersStripped, crash_with_usb_serial_numbers);
}

TEST_F(CrashCollectorTest, GetCrashDirectoryInfo) {
  FilePath path;
  const int kRootUid = 0;
  const int kNtpUid = 5;
  const int kChronosUid = 1000;
  const int kCrashAccessGid = 419;
  const mode_t kExpectedSystemMode = 02770;

  mode_t directory_mode;
  uid_t directory_owner;
  gid_t directory_group;

  path = collector_
             .GetCrashDirectoryInfo(
                 kRootUid, kChronosUid, /*use_non_chronos_cryptohome=*/false,
                 &directory_mode, &directory_owner, &directory_group)
             .value();
  EXPECT_EQ("/var/spool/crash", path.value());
  EXPECT_EQ(kExpectedSystemMode, directory_mode);
  EXPECT_EQ(kRootUid, directory_owner);
  EXPECT_EQ(kCrashAccessGid, directory_group);

  path = collector_
             .GetCrashDirectoryInfo(
                 kNtpUid, kChronosUid, /*use_non_chronos_cryptohome=*/false,
                 &directory_mode, &directory_owner, &directory_group)
             .value();
  EXPECT_EQ("/var/spool/crash", path.value());
  EXPECT_EQ(kExpectedSystemMode, directory_mode);
  EXPECT_EQ(kRootUid, directory_owner);
  EXPECT_EQ(kCrashAccessGid, directory_group);

#if !USE_KVM_GUEST
  const int kCrashUserUid = 20137;
  const int kCrashUserAccessGid = 420;
  const mode_t kExpectedUserMode = 02770;
  const mode_t kExpectedDaemonStoreMode = 03770;

  // When running in the VM, all crashes will go to the system directory.
  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  test_util::SetActiveSessions(mock, {{"user", "hashcakes"}});
  collector_.session_manager_proxy_.reset(mock);

  path = collector_
             .GetCrashDirectoryInfo(
                 kChronosUid, kChronosUid, /*use_non_chronos_cryptohome=*/false,
                 &directory_mode, &directory_owner, &directory_group)
             .value();
  EXPECT_EQ(test_dir_.Append("home/user/hashcakes/crash").value(),
            path.value());
  EXPECT_EQ(kExpectedUserMode, directory_mode);
  EXPECT_EQ(kChronosUid, directory_owner);
  EXPECT_EQ(kCrashUserAccessGid, directory_group);

  path = collector_
             .GetCrashDirectoryInfo(
                 kChronosUid, kChronosUid, /*use_non_chronos_cryptohome=*/true,
                 &directory_mode, &directory_owner, &directory_group)
             .value();
  EXPECT_EQ(test_dir_.Append("run/daemon-store/crash/hashcakes").value(),
            path.value());
  EXPECT_EQ(kExpectedDaemonStoreMode, directory_mode);
  EXPECT_EQ(kCrashUserUid, directory_owner);
  EXPECT_EQ(kCrashUserAccessGid, directory_group);
#endif  // !USE_KVM_GUEST
}

TEST_F(CrashCollectorTest, GetCrashDirectoryInfoLoggedOut) {
  FilePath path;
  const int kChronosUid = 1000;
  const mode_t kExpectedUserMode = 02770;

  mode_t directory_mode;
  uid_t directory_owner;
  gid_t directory_group;

  auto* mock = new org::chromium::SessionManagerInterfaceProxyMock;
  test_util::SetActiveSessions(mock, {});
  collector_.session_manager_proxy_.reset(mock);

  path = collector_
             .GetCrashDirectoryInfo(
                 kChronosUid, kChronosUid, /*use_non_chronos_cryptohome=*/false,
                 &directory_mode, &directory_owner, &directory_group)
             .value();
  EXPECT_EQ(kExpectedUserMode, directory_mode);
#if USE_KVM_GUEST
  // Inside the VM, everything goes to /var/spool/crash.
  const int kCrashAccessGid = 419;
  EXPECT_EQ("/var/spool/crash", path.value());
  EXPECT_EQ(0, directory_owner);
  EXPECT_EQ(kCrashAccessGid, directory_group);
#else
  const int kCrashUserAccessGid = 420;
  EXPECT_EQ("/home/chronos/crash", path.value());
  EXPECT_EQ(kChronosUid, directory_owner);
  EXPECT_EQ(kCrashUserAccessGid, directory_group);
#endif  // USE_KVM_GUEST
}

TEST_F(CrashCollectorTest, FormatDumpBasename) {
  struct tm tm = {};
  tm.tm_sec = 15;
  tm.tm_min = 50;
  tm.tm_hour = 13;
  tm.tm_mday = 23;
  tm.tm_mon = 4;
  tm.tm_year = 110;
  tm.tm_isdst = -1;
  std::string basename = collector_.FormatDumpBasename("foo", mktime(&tm), 100);
  ASSERT_EQ("foo.20100523.135015.100", basename);
}

TEST_F(CrashCollectorTest, GetCrashPath) {
  EXPECT_EQ("/var/spool/crash/myprog.20100101.1200.1234.core",
            collector_
                .GetCrashPath(FilePath("/var/spool/crash"),
                              "myprog.20100101.1200.1234", "core")
                .value());
  EXPECT_EQ("/home/chronos/user/crash/chrome.20100101.1200.1234.dmp",
            collector_
                .GetCrashPath(FilePath("/home/chronos/user/crash"),
                              "chrome.20100101.1200.1234", "dmp")
                .value());
}

TEST_F(CrashCollectorTest, ParseProcessTicksFromStat) {
  uint64_t ticks;
  EXPECT_FALSE(CrashCollector::ParseProcessTicksFromStat("", &ticks));
  EXPECT_FALSE(CrashCollector::ParseProcessTicksFromStat("123 (foo)", &ticks));

  constexpr char kTruncatedStat[] =
      "234641 (cat) R 234581 234641 234581 34821 234641 4194304 117 0 0 0 0 0 "
      "0 0 20 0 1 0";

  EXPECT_FALSE(
      CrashCollector::ParseProcessTicksFromStat(kTruncatedStat, &ticks));

  constexpr char kInvalidStat[] =
      "234641 (cat) R 234581 234641 234581 34821 234641 4194304 117 0 0 0 0 0 "
      "0 0 20 0 1 0 foo";

  EXPECT_FALSE(CrashCollector::ParseProcessTicksFromStat(kInvalidStat, &ticks));

  // Executable name is ") (".
  constexpr char kStat[] =
      "234641 () () R 234581 234641 234581 34821 234641 4194304 117 0 0 0 0 0 "
      "0 0 20 0 1 0 2092891 6090752 182 18446744073709551615 94720364494848 "
      "94720364525584 140735323062016 0 0 0 0 0 0 0 0 0 17 32 0 0 0 0 0 "
      "94720366623824 94720366625440 94720371765248 140735323070153 "
      "140735323070173 140735323070173 140735323074543 0";

  EXPECT_TRUE(CrashCollector::ParseProcessTicksFromStat(kStat, &ticks));
  EXPECT_EQ(2092891, ticks);
}

TEST_F(CrashCollectorTest, GetUptime) {
  base::TimeDelta uptime_at_process_start;
  EXPECT_TRUE(CrashCollector::GetUptimeAtProcessStart(
      getpid(), &uptime_at_process_start));

  base::TimeDelta uptime;
  EXPECT_TRUE(CrashCollector::GetUptime(&uptime));

  EXPECT_GT(uptime, uptime_at_process_start);
}

bool CrashCollectorTest::CheckHasCapacity() {
  std::string full_message = StringPrintf("Crash directory %s already full",
                                          test_dir_.value().c_str());
  bool has_capacity = collector_.CheckHasCapacity(test_dir_);
  bool has_message = FindLog(full_message.c_str());
  EXPECT_EQ(has_message, !has_capacity);
  return has_capacity;
}

TEST_F(CrashCollectorTest, CheckHasCapacityUsual) {
  // Test kMaxCrashDirectorySize - 1 non-meta files can be added.
  for (int i = 0; i < CrashCollector::kMaxCrashDirectorySize - 1; ++i) {
    ASSERT_TRUE(test_util::CreateFile(
        test_dir_.Append(StringPrintf("file%d.core", i)), ""));
    EXPECT_TRUE(CheckHasCapacity());
  }

  // Test supplemental files fit with longer names.
  for (int i = 0; i < CrashCollector::kMaxCrashDirectorySize - 1; ++i) {
    ASSERT_TRUE(test_util::CreateFile(
        test_dir_.Append(StringPrintf("file%d.log.gz", i)), ""));
    EXPECT_TRUE(CheckHasCapacity());
  }

  // Test an additional kMaxCrashDirectorySize - 1 meta files fit.
  for (int i = 0; i < CrashCollector::kMaxCrashDirectorySize - 1; ++i) {
    ASSERT_TRUE(test_util::CreateFile(
        test_dir_.Append(StringPrintf("file%d.meta", i)), ""));
    EXPECT_TRUE(CheckHasCapacity());
  }

  // Test an additional kMaxCrashDirectorySize meta files don't fit.
  for (int i = 0; i < CrashCollector::kMaxCrashDirectorySize; ++i) {
    ASSERT_TRUE(test_util::CreateFile(
        test_dir_.Append(StringPrintf("overage%d.meta", i)), ""));
    EXPECT_FALSE(CheckHasCapacity());
  }
}

TEST_F(CrashCollectorTest, CheckHasCapacityCorrectBasename) {
  // Test kMaxCrashDirectorySize - 1 files can be added.
  for (int i = 0; i < CrashCollector::kMaxCrashDirectorySize - 1; ++i) {
    ASSERT_TRUE(test_util::CreateFile(
        test_dir_.Append(StringPrintf("file.%d.core", i)), ""));
    EXPECT_TRUE(CheckHasCapacity());
  }
  ASSERT_TRUE(test_util::CreateFile(test_dir_.Append("file.last.core"), ""));
  EXPECT_FALSE(CheckHasCapacity());
}

TEST_F(CrashCollectorTest, CheckHasCapacityStrangeNames) {
  // Test many files with different extensions and same base fit.
  for (int i = 0; i < 5 * CrashCollector::kMaxCrashDirectorySize; ++i) {
    ASSERT_TRUE(
        test_util::CreateFile(test_dir_.Append(StringPrintf("a.%d", i)), ""));
    EXPECT_TRUE(CheckHasCapacity());
  }
  // Test dot files are treated as individual files.
  for (int i = 0; i < CrashCollector::kMaxCrashDirectorySize - 2; ++i) {
    ASSERT_TRUE(test_util::CreateFile(
        test_dir_.Append(StringPrintf(".file%d", i)), ""));
    EXPECT_TRUE(CheckHasCapacity());
  }
  ASSERT_TRUE(test_util::CreateFile(test_dir_.Append("normal.meta"), ""));
  EXPECT_TRUE(CheckHasCapacity());
}

struct MetaDataTest {
  std::string test_case_name;
  bool test_in_prog = false;
  std::string exec_name = "kernel";
  std::string expected_meta;
};

class CrashCollectorParameterizedTest
    : public CrashCollectorTest,
      public ::testing::WithParamInterface<MetaDataTest> {
 public:
  static constexpr char kPayloadName[] = "payload-file";
  static constexpr char kKernelName[] = "Linux";
  static constexpr char kKernelVersion[] =
      "3.8.11 #1 SMP Wed Aug 22 02:18:30 PDT 2018";
  // Returns the time we want to use for the OS timestamp. Returns the
  // same value (May 3, 2020 -- basically arbitrary) every time it is run, but
  // base::Time doesn't support constexpr.
  static base::Time GetOsTimeForTest() {
    base::Time::Exploded exploded;
    exploded.year = 2020;
    exploded.month = 5;
    exploded.day_of_month = 3;
    exploded.day_of_week = 0;
    exploded.hour = 7;
    exploded.minute = 22;
    exploded.second = 41;
    // Must not have a millisecond component because ext2/ext3 have a second
    // granularity.
    exploded.millisecond = 0;
    base::Time result;
    CHECK(base::Time::FromUTCExploded(exploded, &result));
    return result;
  }
};

constexpr char CrashCollectorParameterizedTest::kPayloadName[];
constexpr char CrashCollectorParameterizedTest::kKernelName[];
constexpr char CrashCollectorParameterizedTest::kKernelVersion[];

TEST_P(CrashCollectorParameterizedTest, MetaData) {
  MetaDataTest test_case = GetParam();
  if (test_case.test_in_prog) {
    ASSERT_TRUE(
        test_util::CreateFile(paths::GetAt(paths::kSystemRunStateDirectory,
                                           paths::kInProgressTestName),
                              "some.Test"));
  }

  const char kMetaFileBasename[] = "generated.meta";
  FilePath meta_file = test_dir_.Append(kMetaFileBasename);
  FilePath lsb_release = paths::Get("/etc/lsb-release");
  FilePath payload_file = test_dir_.Append(kPayloadName);
  std::string contents;
  collector_.set_lsb_release_for_test(lsb_release);
  const char kLsbContents[] =
      "CHROMEOS_RELEASE_BOARD=lumpy\n"
      "CHROMEOS_RELEASE_VERSION=6727.0.2015_01_26_0853\n"
      "CHROMEOS_RELEASE_NAME=Chromium OS\n"
      "CHROMEOS_RELEASE_CHROME_MILESTONE=82\n"
      "CHROMEOS_RELEASE_DESCRIPTION=6727.0.2015_01_26_0853 (Test Build - foo)";
  ASSERT_TRUE(test_util::CreateFile(lsb_release, kLsbContents));
  const base::Time kFakeOsTime = GetOsTimeForTest();
  ASSERT_TRUE(base::TouchFile(lsb_release, kFakeOsTime, kFakeOsTime));
  const char kPayload[] = "foo";
  ASSERT_TRUE(test_util::CreateFile(payload_file, kPayload));
  collector_.AddCrashMetaData("foo", "bar");
  collector_.AddCrashMetaData("weird  key#@!", "weird\nvalue");
  // Empty key should be ignored and not added.
  collector_.AddCrashMetaData("", "empty_key_val");
  std::unique_ptr<base::SimpleTestClock> test_clock =
      std::make_unique<base::SimpleTestClock>();
  test_clock->SetNow(base::Time::UnixEpoch() +
                     base::TimeDelta::FromMilliseconds(kFakeNow));
  collector_.set_test_clock(std::move(test_clock));
  collector_.set_test_kernel_info(kKernelName, kKernelVersion);
  collector_.FinishCrash(meta_file, test_case.exec_name, kPayloadName);
  EXPECT_TRUE(base::ReadFileToString(meta_file, &contents));
  EXPECT_EQ(test_case.expected_meta, contents);
  EXPECT_EQ(test_case.expected_meta.size(), collector_.get_bytes_written());
}

std::vector<MetaDataTest> GenerateMetaDataTests() {
  const base::Time kOsTimestamp =
      CrashCollectorParameterizedTest::GetOsTimeForTest();
  MetaDataTest base;
  base.test_case_name = "Base";
  base.expected_meta = StringPrintf(
      "upload_var_collector=mock\n"
      "foo=bar\n"
      "weird__key___=weird\\nvalue\n"
      "upload_var_reportTimeMillis=%" PRId64
      "\n"
      "exec_name=kernel\n"
      "upload_var_lsb-release=6727.0.2015_01_26_0853 (Test Build - foo)\n"
      "ver=6727.0.2015_01_26_0853\n"
      "upload_var_cros_milestone=82\n"
      "os_millis=%" PRId64
      "\n"
      "upload_var_osName=%s\n"
      "upload_var_osVersion=%s\n"
      "payload=%s\n"
      "done=1\n",
      kFakeNow, (kOsTimestamp - base::Time::UnixEpoch()).InMilliseconds(),
      CrashCollectorParameterizedTest::kKernelName,
      CrashCollectorParameterizedTest::kKernelVersion,
      CrashCollectorParameterizedTest::kPayloadName);

  MetaDataTest test_in_progress;
  test_in_progress.test_case_name = "Test_in_progress";
  test_in_progress.test_in_prog = true;
  test_in_progress.expected_meta = StringPrintf(
      "upload_var_collector=mock\n"
      "foo=bar\n"
      "weird__key___=weird\\nvalue\n"
      "upload_var_in_progress_integration_test=some.Test\n"
      "upload_var_reportTimeMillis=%" PRId64
      "\n"
      "exec_name=kernel\n"
      "upload_var_lsb-release=6727.0.2015_01_26_0853 (Test Build - foo)\n"
      "ver=6727.0.2015_01_26_0853\n"
      "upload_var_cros_milestone=82\n"
      "os_millis=%" PRId64
      "\n"
      "upload_var_osName=%s\n"
      "upload_var_osVersion=%s\n"
      "payload=%s\n"
      "done=1\n",
      kFakeNow, (kOsTimestamp - base::Time::UnixEpoch()).InMilliseconds(),
      CrashCollectorParameterizedTest::kKernelName,
      CrashCollectorParameterizedTest::kKernelVersion,
      CrashCollectorParameterizedTest::kPayloadName);

  MetaDataTest no_exec_name;
  no_exec_name.test_case_name = "No_exec_name";
  no_exec_name.exec_name = "";
  no_exec_name.expected_meta = StringPrintf(
      "upload_var_collector=mock\n"
      "foo=bar\n"
      "weird__key___=weird\\nvalue\n"
      "upload_var_reportTimeMillis=%" PRId64
      "\n"
      "upload_var_lsb-release=6727.0.2015_01_26_0853 (Test Build - foo)\n"
      "ver=6727.0.2015_01_26_0853\n"
      "upload_var_cros_milestone=82\n"
      "os_millis=%" PRId64
      "\n"
      "upload_var_osName=%s\n"
      "upload_var_osVersion=%s\n"
      "payload=%s\n"
      "done=1\n",
      kFakeNow, (kOsTimestamp - base::Time::UnixEpoch()).InMilliseconds(),
      CrashCollectorParameterizedTest::kKernelName,
      CrashCollectorParameterizedTest::kKernelVersion,
      CrashCollectorParameterizedTest::kPayloadName);

  return {base, test_in_progress, no_exec_name};
}

INSTANTIATE_TEST_SUITE_P(CrashCollectorInstantiation,
                         CrashCollectorParameterizedTest,
                         testing::ValuesIn(GenerateMetaDataTests()),
                         [](const testing::TestParamInfo<MetaDataTest>& info) {
                           return info.param.test_case_name;
                         });

TEST_F(CrashCollectorTest, ErrorCollectionMetaData) {
  // Set up metadata the collector will read
  FilePath lsb_release = paths::Get("/etc/lsb-release");
  std::string contents;
  collector_.set_lsb_release_for_test(lsb_release);
  const char kLsbContents[] =
      "CHROMEOS_RELEASE_BOARD=lumpy\n"
      "CHROMEOS_RELEASE_VERSION=6727.0.2015_01_26_0853\n"
      "CHROMEOS_RELEASE_NAME=Chromium OS\n"
      "CHROMEOS_RELEASE_CHROME_MILESTONE=82\n"
      "CHROMEOS_RELEASE_DESCRIPTION=6727.0.2015_01_26_0853 (Test Build - foo)";
  ASSERT_TRUE(test_util::CreateFile(lsb_release, kLsbContents));
  base::Time os_time = base::Time::Now() - base::TimeDelta::FromDays(123);
  // ext2/ext3 seem to have a timestamp granularity of 1s so round this time
  // value down to the nearest second.
  os_time = base::TimeDelta::FromSeconds(
                (os_time - base::Time::UnixEpoch()).InSeconds()) +
            base::Time::UnixEpoch();
  ASSERT_TRUE(base::TouchFile(lsb_release, os_time, os_time));

  std::unique_ptr<base::SimpleTestClock> test_clock =
      std::make_unique<base::SimpleTestClock>();
  test_clock->SetNow(base::Time::UnixEpoch() +
                     base::TimeDelta::FromMilliseconds(kFakeNow));
  collector_.set_test_clock(std::move(test_clock));

  const char kKernelName[] = "Linux";
  const char kKernelVersion[] = "3.8.11 #1 SMP Wed Aug 22 02:18:30 PDT 2018";
  collector_.set_test_kernel_info(kKernelName, kKernelVersion);
  collector_.set_crash_directory_for_test(test_dir_);

  collector_.EnqueueCollectionErrorLog(
      CrashCollector::kErrorUnsupported32BitCoreFile, "some_exec");

  base::FilePath meta_file_path;
  ASSERT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_dir_, "crash_reporter_failure.*.meta", &meta_file_path));

  base::FilePath base_name = meta_file_path.BaseName().RemoveExtension();
  base::FilePath pslog_name = base_name.AddExtension("pslog");
  base::FilePath log_name = base_name.AddExtension("log");

  EXPECT_TRUE(base::ReadFileToString(meta_file_path, &contents));
  std::string expected_meta = StringPrintf(
      "upload_var_collector=crash_reporter_failure\n"
      "upload_var_orig_collector=mock\n"
      "upload_var_orig_exec=some_exec\n"
      "sig=crash_reporter-user-collection_unsupported-32bit-core-file\n"
      "error_type=unsupported-32bit-core-file\n"
      "upload_file_pslog=%s\n"
      "upload_var_reportTimeMillis=%" PRId64
      "\n"
      "exec_name=crash_reporter_failure\n"
      "upload_var_lsb-release=6727.0.2015_01_26_0853 (Test Build - foo)\n"
      "ver=6727.0.2015_01_26_0853\n"
      "upload_var_cros_milestone=82\n"
      "os_millis=%" PRId64
      "\n"
      "upload_var_osName=%s\n"
      "upload_var_osVersion=%s\n"
      "payload=%s\n"
      "done=1\n",
      pslog_name.value().c_str(), kFakeNow,
      (os_time - base::Time::UnixEpoch()).InMilliseconds(), kKernelName,
      kKernelVersion, log_name.value().c_str());
  EXPECT_EQ(expected_meta, contents);
}

// Test target of symlink is not overwritten.
TEST_F(CrashCollectorTest, MetaDataDoesntOverwriteSymlink) {
  const char kSymlinkTarget[] = "important_file";
  FilePath symlink_target_path = test_dir_.Append(kSymlinkTarget);
  const char kOriginalContents[] = "Very important contents";
  EXPECT_EQ(base::WriteFile(symlink_target_path, kOriginalContents,
                            strlen(kOriginalContents)),
            strlen(kOriginalContents));

  FilePath meta_symlink_path = test_dir_.Append("symlink.meta");
  ASSERT_EQ(0, symlink(kSymlinkTarget, meta_symlink_path.value().c_str()));
  ASSERT_TRUE(base::PathExists(meta_symlink_path));

  const char kPayloadName[] = "payload2-file";
  FilePath payload_file = test_dir_.Append(kPayloadName);
  ASSERT_TRUE(test_util::CreateFile(payload_file, "whatever"));

  brillo::ClearLog();
  collector_.FinishCrash(meta_symlink_path, "kernel", kPayloadName);
  // Target file contents should have stayed the same.
  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(symlink_target_path, &contents));
  EXPECT_EQ(kOriginalContents, contents);
  EXPECT_TRUE(FindLog("Unable to write"));
  EXPECT_EQ(collector_.get_bytes_written(), 0);
}

// Test target of dangling symlink is not created.
TEST_F(CrashCollectorTest, MetaDataDoesntCreateSymlink) {
  const char kSymlinkTarget[] = "important_file";
  FilePath symlink_target_path = test_dir_.Append(kSymlinkTarget);
  ASSERT_FALSE(base::PathExists(symlink_target_path));

  FilePath meta_symlink_path = test_dir_.Append("symlink.meta");
  ASSERT_EQ(0, symlink(kSymlinkTarget, meta_symlink_path.value().c_str()));
  ASSERT_FALSE(base::PathExists(meta_symlink_path));

  const char kPayloadName[] = "payload2-file";
  FilePath payload_file = test_dir_.Append(kPayloadName);
  ASSERT_TRUE(test_util::CreateFile(payload_file, "whatever"));

  brillo::ClearLog();
  collector_.FinishCrash(meta_symlink_path, "kernel", kPayloadName);
  EXPECT_FALSE(base::PathExists(symlink_target_path));
  EXPECT_TRUE(FindLog("Unable to write"));
  EXPECT_EQ(collector_.get_bytes_written(), 0);
}

TEST_F(CrashCollectorTest, CollectionLogsToUMA) {
  auto metrics_lib = std::make_unique<MetricsLibraryMock>();
  MetricsLibraryMock* mock_ref = metrics_lib.get();
  collector_.set_metrics_library_for_test(std::move(metrics_lib));

  const FilePath kMetaFilePath = test_dir_.Append("meta.txt");
  const char kPayloadName[] = "payload-file";
  FilePath payload_file = test_dir_.Append(kPayloadName);

  EXPECT_CALL(*mock_ref, SendCrosEventToUMA("Crash.Collector.CollectionCount"))
      .WillOnce(Return(true));
  collector_.FinishCrash(kMetaFilePath, "kernel", kPayloadName);
}

TEST_F(CrashCollectorTest, GetLogContents) {
  FilePath config_file = test_dir_.Append("crash_config");
  FilePath output_file = test_dir_.Append("crash_log.gz");
  const char kConfigContents[] =
      "foobar=echo hello there | \\\n  sed -e \"s/there/world/\"";
  ASSERT_TRUE(test_util::CreateFile(config_file, kConfigContents));
  base::DeleteFile(FilePath(output_file), false);
  EXPECT_FALSE(collector_.GetLogContents(config_file, "barfoo", output_file));
  EXPECT_FALSE(base::PathExists(output_file));
  EXPECT_EQ(collector_.get_bytes_written(), 0);
  base::DeleteFile(FilePath(output_file), false);
  EXPECT_TRUE(collector_.GetLogContents(config_file, "foobar", output_file));
  ASSERT_TRUE(base::PathExists(output_file));
  EXPECT_GT(collector_.get_bytes_written(), 0);

  int decompress_result = system(("gunzip " + output_file.value()).c_str());
  EXPECT_TRUE(WIFEXITED(decompress_result));
  EXPECT_EQ(WEXITSTATUS(decompress_result), 0);

  FilePath decompressed_output_file = test_dir_.Append("crash_log");
  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(decompressed_output_file, &contents));
  EXPECT_EQ("hello world\n", contents);
}

TEST_F(CrashCollectorTest, GetMultipleLogContents) {
  FilePath config_file = test_dir_.Append("crash_config");
  FilePath output_file = test_dir_.Append("crash_log");
  const char kConfigContents[] =
      "foobaz=echo foobaz\n"
      "bazbar=echo bazbar";
  ASSERT_TRUE(test_util::CreateFile(config_file, kConfigContents));
  base::DeleteFile(FilePath(output_file), false);

  // If both commands fail, expect no output.
  EXPECT_FALSE(collector_.GetMultipleLogContents(
      config_file, {"foobar", "barfoo"}, output_file));
  ASSERT_FALSE(base::PathExists(output_file));
  EXPECT_EQ(collector_.get_bytes_written(), 0);

  // If one command fails, expect output from the other command.
  EXPECT_TRUE(collector_.GetMultipleLogContents(
      config_file, {"foobar", "bazbar"}, output_file));
  ASSERT_TRUE(base::PathExists(output_file));
  EXPECT_GT(collector_.get_bytes_written(), 0);
  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(output_file, &contents));
  EXPECT_EQ("bazbar\n", contents);
  base::DeleteFile(FilePath(output_file), false);

  // Expect output from both commands.
  EXPECT_TRUE(collector_.GetMultipleLogContents(
      config_file, {"foobaz", "bazbar"}, output_file));
  ASSERT_TRUE(base::PathExists(output_file));
  EXPECT_GT(collector_.get_bytes_written(), 0);
  EXPECT_TRUE(base::ReadFileToString(output_file, &contents));
  EXPECT_EQ("foobaz\nbazbar\n", contents);
}

TEST_F(CrashCollectorTest, GetProcessTree) {
  const FilePath output_file = test_dir_.Append("log");
  std::string contents;

  ASSERT_TRUE(collector_.GetProcessTree(getpid(), output_file));
  ASSERT_TRUE(base::PathExists(output_file));
  EXPECT_TRUE(base::ReadFileToString(output_file, &contents));
  EXPECT_LT(300, contents.size());
  EXPECT_EQ(collector_.get_bytes_written(), contents.size());
  base::DeleteFile(FilePath(output_file), false);

  ASSERT_TRUE(collector_.GetProcessTree(0, output_file));
  ASSERT_TRUE(base::PathExists(output_file));
  std::string contents_pid_0;
  EXPECT_TRUE(base::ReadFileToString(output_file, &contents_pid_0));
  EXPECT_GT(100, contents_pid_0.size());
  EXPECT_EQ(collector_.get_bytes_written(),
            contents.size() + contents_pid_0.size());
}

TEST_F(CrashCollectorTest, TruncatedLog) {
  FilePath config_file = test_dir_.Append("crash_config");
  FilePath output_file = test_dir_.Append("crash_log.gz");
  const char kConfigContents[] = "foobar=echo These are log contents.";
  ASSERT_TRUE(test_util::CreateFile(config_file, kConfigContents));
  base::DeleteFile(FilePath(output_file), false);
  collector_.max_log_size_ = 10;
  EXPECT_TRUE(collector_.GetLogContents(config_file, "foobar", output_file));
  ASSERT_TRUE(base::PathExists(output_file));
  int64_t file_size = -1;
  EXPECT_TRUE(base::GetFileSize(output_file, &file_size));
  EXPECT_EQ(collector_.get_bytes_written(), file_size);

  int decompress_result = system(("gunzip " + output_file.value()).c_str());
  EXPECT_TRUE(WIFEXITED(decompress_result));
  EXPECT_EQ(WEXITSTATUS(decompress_result), 0);

  FilePath decompressed_output_file = test_dir_.Append("crash_log");
  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(decompressed_output_file, &contents));
  EXPECT_EQ("These are \n<TRUNCATED>\n", contents);
}

// Check that the mode is reset properly.
TEST_F(CrashCollectorTest, CreateDirectoryWithSettingsMode) {
  int mode;
  EXPECT_TRUE(base::SetPosixFilePermissions(test_dir_, 0700));
  EXPECT_TRUE(CrashCollector::CreateDirectoryWithSettings(
      test_dir_, 0755, getuid(), getgid(), nullptr));
  EXPECT_TRUE(base::GetPosixFilePermissions(test_dir_, &mode));
  EXPECT_EQ(0755, mode);
}

// Check non-dir handling.
TEST_F(CrashCollectorTest, CreateDirectoryWithSettingsNonDir) {
  const base::FilePath file = test_dir_.Append("file");

  // Do not walk past a non-dir.
  ASSERT_TRUE(test_util::CreateFile(file, ""));
  EXPECT_FALSE(CrashCollector::CreateDirectoryWithSettings(
      file.Append("subdir"), 0755, getuid(), getgid(), nullptr));
  EXPECT_TRUE(base::PathExists(file));
  EXPECT_FALSE(base::DirectoryExists(file));

  // Remove files and create dirs.
  EXPECT_TRUE(CrashCollector::CreateDirectoryWithSettings(file, 0755, getuid(),
                                                          getgid(), nullptr));
  EXPECT_TRUE(base::DirectoryExists(file));
}

// Check we only create a single subdir.
TEST_F(CrashCollectorTest, CreateDirectoryWithSettingsSubdir) {
  const base::FilePath subdir = test_dir_.Append("sub");
  const base::FilePath subsubdir = subdir.Append("subsub");

  // Accessing sub/subsub/ should fail.
  EXPECT_FALSE(CrashCollector::CreateDirectoryWithSettings(
      subsubdir, 0755, getuid(), getgid(), nullptr));
  EXPECT_FALSE(base::PathExists(subdir));

  // Accessing sub/ should work.
  EXPECT_TRUE(CrashCollector::CreateDirectoryWithSettings(
      subdir, 0755, getuid(), getgid(), nullptr));
  EXPECT_TRUE(base::DirectoryExists(subdir));

  // Accessing sub/subsub/ should now work.
  EXPECT_TRUE(CrashCollector::CreateDirectoryWithSettings(
      subsubdir, 0755, getuid(), getgid(), nullptr));
  EXPECT_TRUE(base::DirectoryExists(subsubdir));
}

// Check symlink handling.
TEST_F(CrashCollectorTest, CreateDirectoryWithSettingsSymlinks) {
  base::FilePath td;

  // Do not walk an intermediate symlink (final target doesn't exist).
  // test/sub/
  // test/sym -> sub
  // Then access test/sym/subsub/.
  td = test_dir_.Append("1");
  EXPECT_TRUE(base::CreateDirectory(td.Append("sub")));
  EXPECT_TRUE(
      base::CreateSymbolicLink(base::FilePath("sub"), td.Append("sym")));
  EXPECT_FALSE(CrashCollector::CreateDirectoryWithSettings(
      td.Append("sym1/subsub"), 0755, getuid(), getgid(), nullptr));
  EXPECT_TRUE(base::IsLink(td.Append("sym")));
  EXPECT_FALSE(base::PathExists(td.Append("sub/subsub")));

  // Do not walk an intermediate symlink (final target exists).
  // test/sub/subsub/
  // test/sym -> sub
  // Then access test/sym/subsub/.
  td = test_dir_.Append("2");
  EXPECT_TRUE(base::CreateDirectory(td.Append("sub/subsub")));
  EXPECT_TRUE(
      base::CreateSymbolicLink(base::FilePath("sub"), td.Append("sym")));
  EXPECT_FALSE(CrashCollector::CreateDirectoryWithSettings(
      td.Append("sym/subsub"), 0755, getuid(), getgid(), nullptr));
  EXPECT_TRUE(base::IsLink(td.Append("sym")));

  // If the final path is a symlink, we should remove it and make a dir.
  // test/sub/
  // test/sub/sym -> subsub
  td = test_dir_.Append("3");
  EXPECT_TRUE(base::CreateDirectory(td.Append("sub/subsub")));
  EXPECT_TRUE(
      base::CreateSymbolicLink(base::FilePath("subsub"), td.Append("sub/sym")));
  EXPECT_TRUE(CrashCollector::CreateDirectoryWithSettings(
      td.Append("sub/sym"), 0755, getuid(), getgid(), nullptr));
  EXPECT_FALSE(base::IsLink(td.Append("sub/sym")));
  EXPECT_TRUE(base::DirectoryExists(td.Append("sub/sym")));

  // If the final path is a symlink, we should remove it and make a dir.
  // test/sub/subsub
  // test/sub/sym -> subsub
  td = test_dir_.Append("4");
  EXPECT_TRUE(base::CreateDirectory(td.Append("sub")));
  EXPECT_TRUE(
      base::CreateSymbolicLink(base::FilePath("subsub"), td.Append("sub/sym")));
  EXPECT_TRUE(CrashCollector::CreateDirectoryWithSettings(
      td.Append("sub/sym"), 0755, getuid(), getgid(), nullptr));
  EXPECT_FALSE(base::IsLink(td.Append("sub/sym")));
  EXPECT_TRUE(base::DirectoryExists(td.Append("sub/sym")));
  EXPECT_FALSE(base::PathExists(td.Append("sub/subsub")));
}

// Test that CreateDirectoryWithSettings only changes the directory if a file
// permission mode is not specified.
TEST_F(CrashCollectorTest, CreateDirectoryWithSettings_FixPermissionsShallow) {
  FilePath crash_dir = test_dir_.Append("crash_perms");
  ASSERT_TRUE(base::CreateDirectory(crash_dir.Append("foo/bar")));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir, 0777));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir.Append("foo"), 0766));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir.Append("foo/bar"), 0744));

  const char contents[] = "hello";
  ASSERT_EQ(
      base::WriteFile(crash_dir.Append("file"), contents, strlen(contents)),
      strlen(contents));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir.Append("file"), 0600));

  int fd;
  int expected_mode = 0755;
  EXPECT_TRUE(CrashCollector::CreateDirectoryWithSettings(
      crash_dir, expected_mode, getuid(), getgid(), &fd));
  struct stat st;
  EXPECT_EQ(fstat(fd, &st), 0);
  EXPECT_EQ(st.st_mode & 07777, expected_mode);

  close(fd);

  int actual_mode;
  EXPECT_TRUE(base::GetPosixFilePermissions(crash_dir, &actual_mode));
  EXPECT_EQ(actual_mode, expected_mode);

  EXPECT_TRUE(
      base::GetPosixFilePermissions(crash_dir.Append("file"), &actual_mode));
  EXPECT_EQ(actual_mode, 0600);

  EXPECT_TRUE(
      base::GetPosixFilePermissions(crash_dir.Append("foo"), &actual_mode));
  EXPECT_EQ(actual_mode, 0766);

  EXPECT_TRUE(
      base::GetPosixFilePermissions(crash_dir.Append("foo/bar"), &actual_mode));
  EXPECT_EQ(actual_mode, 0744);
}

// TODO(mutexlox): Test the following cases:
//   - Owner/Group changes are possible (may need to run as root?)
// Test that CreateDirectoryWithSettings fixes the permissions of a full tree.
TEST_F(CrashCollectorTest,
       CreateDirectoryWithSettings_FixPermissionsRecursive) {
  FilePath crash_dir = test_dir_.Append("crash_perms");
  ASSERT_TRUE(base::CreateDirectory(crash_dir.Append("foo/bar")));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir, 0777));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir.Append("foo"), 0766));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir.Append("foo/bar"), 0744));

  const char contents[] = "hello";
  ASSERT_EQ(
      base::WriteFile(crash_dir.Append("file"), contents, strlen(contents)),
      strlen(contents));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir.Append("file"), 0600));

  int fd;
  int expected_mode = 0755;
  int expected_file_mode = 0644;
  EXPECT_TRUE(CrashCollector::CreateDirectoryWithSettings(
      crash_dir, expected_mode, getuid(), getgid(), &fd, expected_file_mode));
  struct stat st;
  EXPECT_EQ(fstat(fd, &st), 0);
  EXPECT_EQ(st.st_mode & 07777, expected_mode);

  close(fd);

  int actual_mode;
  EXPECT_TRUE(base::GetPosixFilePermissions(crash_dir, &actual_mode));
  EXPECT_EQ(actual_mode, expected_mode);

  EXPECT_TRUE(
      base::GetPosixFilePermissions(crash_dir.Append("file"), &actual_mode));
  EXPECT_EQ(actual_mode, expected_file_mode);

  EXPECT_TRUE(
      base::GetPosixFilePermissions(crash_dir.Append("foo"), &actual_mode));
  EXPECT_EQ(actual_mode, expected_mode);

  EXPECT_TRUE(
      base::GetPosixFilePermissions(crash_dir.Append("foo/bar"), &actual_mode));
  EXPECT_EQ(actual_mode, expected_mode);
}

// Verify that CreateDirectoryWithSettings will fix subdirectories even if the
// top-level directory is correct.
TEST_F(CrashCollectorTest, CreateDirectoryWithSettings_FixSubdirPermissions) {
  FilePath crash_dir = test_dir_.Append("crash_perms");
  int expected_mode = 0755;

  ASSERT_TRUE(base::CreateDirectory(crash_dir.Append("foo/bar")));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir, expected_mode));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir.Append("foo"), 0766));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir.Append("foo/bar"), 0744));

  const char contents[] = "hello";
  ASSERT_EQ(
      base::WriteFile(crash_dir.Append("file"), contents, strlen(contents)),
      strlen(contents));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir.Append("file"), 0600));

  int fd;
  int expected_file_mode = 0644;
  EXPECT_TRUE(CrashCollector::CreateDirectoryWithSettings(
      crash_dir, expected_mode, getuid(), getgid(), &fd, expected_file_mode));
  struct stat st;
  EXPECT_EQ(fstat(fd, &st), 0);
  EXPECT_EQ(st.st_mode & 07777, expected_mode);

  close(fd);

  int actual_mode;
  EXPECT_TRUE(base::GetPosixFilePermissions(crash_dir, &actual_mode));
  EXPECT_EQ(actual_mode, expected_mode);

  EXPECT_TRUE(
      base::GetPosixFilePermissions(crash_dir.Append("file"), &actual_mode));
  EXPECT_EQ(actual_mode, expected_file_mode);

  EXPECT_TRUE(
      base::GetPosixFilePermissions(crash_dir.Append("foo"), &actual_mode));
  EXPECT_EQ(actual_mode, expected_mode);

  EXPECT_TRUE(
      base::GetPosixFilePermissions(crash_dir.Append("foo/bar"), &actual_mode));
  EXPECT_EQ(actual_mode, expected_mode);
}

TEST_F(CrashCollectorTest, RunAsRoot_CreateDirectoryWithSettings_FixOwners) {
  ASSERT_EQ(getuid(), 0);
  ASSERT_EQ(getgid(), 0);

  FilePath crash_dir = test_dir_.Append("crash_perms");
  ASSERT_TRUE(base::CreateDirectory(crash_dir));
  ASSERT_TRUE(base::SetPosixFilePermissions(crash_dir, 0777));

  ASSERT_EQ(chown(crash_dir.value().c_str(), 1001, 1001), 0);

  int fd;
  int expected_mode = 0755;
  EXPECT_TRUE(CrashCollector::CreateDirectoryWithSettings(
      crash_dir, expected_mode, getuid(), getgid(), &fd));
  struct stat st;
  EXPECT_EQ(fstat(fd, &st), 0);
  EXPECT_EQ(st.st_mode & 07777, expected_mode);
  EXPECT_EQ(st.st_uid, getuid());
  EXPECT_EQ(st.st_gid, getgid());

  close(fd);

  int actual_mode;
  EXPECT_TRUE(base::GetPosixFilePermissions(crash_dir, &actual_mode));
  EXPECT_EQ(actual_mode, expected_mode);
}

void CrashCollectorTest::TestFinishCrashInCrashLoopMode(
    bool give_success_response) {
  const char kBuffer[] = "Buffer full of goodness";
  const FilePath kPath = test_dir_.Append("buffer.txt");
  const FilePath kMetaFilePath = test_dir_.Append("meta.txt");
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);

  CrashCollectorMock collector(
      CrashCollector::kUseNormalCrashDirectorySelectionMethod,
      CrashCollector::kCrashLoopSendingMode);
  dbus::Bus::Options bus_options;
  auto mock_bus = base::MakeRefCounted<dbus::MockBus>(bus_options);
  auto mock_object_proxy = base::MakeRefCounted<dbus::MockObjectProxy>(
      mock_bus.get(), "org.chromium.debugd",
      dbus::ObjectPath("/org/chromium/debugd"));
  EXPECT_CALL(collector, SetUpDBus())
      .WillOnce(Invoke([&collector, &mock_bus]() {
        collector.bus_ = mock_bus;
        collector.debugd_proxy_ =
            std::make_unique<org::chromium::debugdProxy>(mock_bus);
      }))
      .WillRepeatedly(Return());
  EXPECT_CALL(*mock_bus,
              GetObjectProxy("org.chromium.debugd",
                             dbus::ObjectPath("/org/chromium/debugd")))
      .WillRepeatedly(Return(mock_object_proxy.get()));
  std::unique_ptr<dbus::Response> empty_response;
  std::unique_ptr<dbus::ErrorResponse> empty_error_response;
  EXPECT_CALL(*mock_object_proxy, DoCallMethodWithErrorCallback(_, 0, _, _))
      .WillOnce(Invoke([&](dbus::MethodCall* method_call, int timeout_ms,
                           dbus::ObjectProxy::ResponseCallback* callback,
                           dbus::ObjectProxy::ErrorCallback* error_callback) {
        // We can't copy or move the method_call object, and it will be
        // destroyed shortly after this lambda ends, so we must validate its
        // contents inside the lambda.
        dbus::MessageReader reader(method_call);
        dbus::MessageReader array_reader(nullptr);
        EXPECT_TRUE(reader.PopArray(&array_reader));
        EXPECT_FALSE(reader.HasMoreData());
        dbus::MessageReader struct_reader_1(nullptr);
        EXPECT_TRUE(array_reader.PopStruct(&struct_reader_1));
        dbus::MessageReader struct_reader_2(nullptr);
        EXPECT_TRUE(array_reader.PopStruct(&struct_reader_2));
        EXPECT_FALSE(array_reader.HasMoreData())
            << "Should only have 2 files in array";
        std::string file_name_1;
        EXPECT_TRUE(struct_reader_1.PopString(&file_name_1));
        base::ScopedFD fd_1;
        EXPECT_TRUE(struct_reader_1.PopFileDescriptor(&fd_1));
        EXPECT_TRUE(fd_1.is_valid());
        EXPECT_FALSE(struct_reader_1.HasMoreData());

        std::string file_name_2;
        EXPECT_TRUE(struct_reader_2.PopString(&file_name_2));
        base::ScopedFD fd_2;
        EXPECT_TRUE(struct_reader_2.PopFileDescriptor(&fd_2));
        EXPECT_TRUE(fd_2.is_valid());
        EXPECT_FALSE(struct_reader_2.HasMoreData());

        base::ScopedFD payload_fd;
        base::ScopedFD meta_fd;
        if (file_name_1 == "buffer.txt") {
          EXPECT_EQ(file_name_2, "meta.txt");
          payload_fd = std::move(fd_1);
          meta_fd = std::move(fd_2);
        } else {
          EXPECT_EQ(file_name_1, "meta.txt");
          EXPECT_EQ(file_name_2, "buffer.txt");
          payload_fd = std::move(fd_2);
          meta_fd = std::move(fd_1);
        }
        base::File payload_file(payload_fd.release());
        EXPECT_TRUE(payload_file.IsValid());
        EXPECT_EQ(payload_file.GetLength(), strlen(kBuffer));
        char result_buffer[100] = {'\0'};
        EXPECT_EQ(payload_file.Read(0, result_buffer, sizeof(result_buffer)),
                  strlen(kBuffer));
        EXPECT_EQ(std::string(kBuffer), std::string(result_buffer));

        base::File meta_file(meta_fd.release());
        EXPECT_TRUE(meta_file.IsValid());
        EXPECT_GT(meta_file.GetLength(), 0);

        ASSERT_TRUE(base::ThreadTaskRunnerHandle::IsSet());
        // Serial would normally be set by the transmission code before we tried
        // to make a reply from it. Since we are bypassing the transmission
        // code, we must set the serial number here.
        method_call->SetSerial(1);
        if (give_success_response) {
          empty_response = dbus::Response::FromMethodCall(method_call);
          base::ThreadTaskRunnerHandle::Get()->PostTask(
              FROM_HERE,
              base::BindOnce(std::move(*callback), empty_response.get()));
        } else {
          empty_error_response = dbus::ErrorResponse::FromMethodCall(
              method_call, "org.freedesktop.DBus.Error.Failed",
              "Things didn't work");
          base::ThreadTaskRunnerHandle::Get()->PostTask(
              FROM_HERE, base::BindOnce(std::move(*error_callback),
                                        empty_error_response.get()));
        }
      }));

  collector.Initialize(false);

  EXPECT_EQ(collector.WriteNewFile(kPath, kBuffer, strlen(kBuffer)),
            strlen(kBuffer));
  EXPECT_EQ(collector.get_bytes_written(), strlen(kBuffer));
  collector.FinishCrash(kMetaFilePath, "kernel", kPath.BaseName().value());
  EXPECT_GT(collector.get_bytes_written(), strlen(kBuffer));
}

TEST_F(CrashCollectorTest,
       DISABLED_ON_QEMU_FOR_MEMFD_CREATE(
           FinishCrashInCrashLoopModeSuccessfulResponse)) {
  TestFinishCrashInCrashLoopMode(true);
}

TEST_F(CrashCollectorTest,
       DISABLED_ON_QEMU_FOR_MEMFD_CREATE(
           FinishCrashInCrashLoopModeErrorResponse)) {
  TestFinishCrashInCrashLoopMode(false);
}
