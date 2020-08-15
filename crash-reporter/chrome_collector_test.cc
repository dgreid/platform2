// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "crash-reporter/chrome_collector.h"

#include <stdio.h>

#include <utility>

#include <base/auto_reset.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <brillo/data_encoding.h>
#include <brillo/syslog_logging.h>
#include <debugd/dbus-proxy-mocks.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "crash-reporter/test_util.h"

using base::FilePath;
using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::HasSubstr;
using ::testing::Invoke;
using ::testing::Not;
using ::testing::Return;
using ::testing::SetArgPointee;

namespace {
const char kTestCrashDirectory[] = "test-crash-directory";

const char kCrashFormatGood[] = "value1:10:abcdefghijvalue2:5:12345";
const char kCrashFormatEmbeddedNewline[] =
    "value1:10:abcd\r\nghijvalue2:5:12\n34";
const char* const kCrashFormatBadValues[] = {
    "value1:10:abcdefghijvalue2:6=12345",
    "value1:10:abcdefghijvalue2:512345",
    "value1:10::abcdefghijvalue2:5=12345",
    "value1:10:abcdefghijvalue2:4=12345",
};

const char kCrashFormatWithFile[] =
    "value1:10:abcdefghijvalue2:5:12345"
    "some_file\"; filename=\"foo.txt\":15:12345\n789\n12345"
    "value3:2:ok";

// Matches the :20: in kCrashFormatWithDumpFile
const int kOutputDumpFileSize = 20;
// Matches the :15: in kCrashFormatWithDumpFile
const int kOutputOtherFileSize = 15;

const char kCrashFormatWithDumpFile[] =
    "value1:10:abcdefghij"
    "value2:5:12345"
    "some_file\"; filename=\"foo.txt\":15:12345\n789\n12345"
    "upload_file_minidump\"; filename=\"dump\":20:0123456789abcdefghij"
    "value3:2:ok";
const char kCrashFormatWithDumpFileWithEmbeddedNulBytes[] =
    "value1:10:abcdefghij"
    "value2:5:12345"
    "some_file\"; filename=\"foo.txt\":15:12\00045\n789\n12\00045"
    "upload_file_minidump\"; filename=\"dump\":20:"
    "\00012345678\000\a\bcd\x0e\x0fghij"
    "value3:2:ok";
const char kCrashFormatWithWeirdFilename[] =
    "value1:10:abcdefghij"
    "value2:5:12345"
    "dotdotfile\"; filename=\"../a.txt\":15:12345\n789\n12345"
    "upload_file_minidump\"; filename=\"dump\":20:0123456789abcdefghij"
    "value3:2:ok";

const char kSampleDriErrorStateEncoded[] =
    "<base64>: SXQgYXBwZWFycyB0byBiZSBzb21lIHNvcnQgb2YgZXJyb3IgZGF0YS4=";
const char kSampleDriErrorStateDecoded[] =
    "It appears to be some sort of error data.";

const char kSampleDriErrorStateEncodedLong[] =
    "<base64>: "
    "MDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5M"
    "DAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OT"
    "AKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg"
    "5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4"
    "OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2N"
    "zg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Nj"
    "c4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU"
    "2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1"
    "Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzN"
    "DU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMz"
    "Q1Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTI"
    "zNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEy"
    "MzQ1Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwM"
    "TIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMD"
    "EyMzQ1Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTA"
    "wMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkw"
    "MDEyMzQ1Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4O"
    "TAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3OD"
    "kwMDEyMzQ1Njc4OTAKMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc"
    "4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3ODkwMDEyMzQ1Njc4OTAwMTIzNDU2Nzg5MDAxMjM0NTY3"
    "ODkwMDEyMzQ1Njc4OTAK";

bool IsMetrics() {
  return true;
}

}  // namespace

class ChromeCollectorMock : public ChromeCollector {
 public:
  ChromeCollectorMock() : ChromeCollector(kNormalCrashSendMode) {}
  MOCK_METHOD(void, SetUpDBus, (), (override));
};

class ChromeCollectorTest : public ::testing::Test {
 protected:
  void ExpectFileEquals(const char* golden, const FilePath& file_path) {
    std::string contents;
    EXPECT_TRUE(base::ReadFileToString(file_path, &contents));
    EXPECT_EQ(golden, contents);
  }

  // Set things up so that the call to get the DriErrorState will return the
  // indicating string. Set to "<empty>" to avoid creating a DriErrorState.
  void SetUpDriErrorStateToReturn(std::string result) {
    debugd_proxy_mock_ = std::make_unique<org::chromium::debugdProxyMock>();
    ON_CALL(*debugd_proxy_mock_, GetLog("i915_error_state", _, _, _))
        .WillByDefault(DoAll(SetArgPointee<1>(result), Return(true)));
    EXPECT_CALL(collector_, SetUpDBus())
        .Times(AnyNumber())
        .WillOnce(Invoke([this]() {
          collector_.debugd_proxy_ = std::move(debugd_proxy_mock_);
        }))
        .WillRepeatedly(Return());
  }

  // Sets up the logs config so that HandleCrash will produce a relatively small
  // chrome.txt.gz.
  void SetUpLogsShort() {
    base::FilePath config_file =
        scoped_temp_dir_.GetPath().Append("crash_config");
    const char kConfigContents[] = "chrome_test=echo hello there";
    ASSERT_TRUE(test_util::CreateFile(config_file, kConfigContents));
    collector_.set_log_config_path(config_file.value());
  }

  // Sets up the logs config so that HandleCrash will produce a relatively large
  // chrome.txt.gz -- even compressed, should be over 10K.
  void SetUpLogsLong() {
    base::FilePath config_file =
        scoped_temp_dir_.GetPath().Append("crash_config");
    const char kConfigContents[] = "chrome_test=seq 1 10000";
    ASSERT_TRUE(test_util::CreateFile(config_file, kConfigContents));
    collector_.set_log_config_path(config_file.value());
  }

  void Decompress(const base::FilePath& path) {
    int decompress_result = system(("gunzip " + path.value()).c_str());
    EXPECT_TRUE(WIFEXITED(decompress_result));
    EXPECT_EQ(WEXITSTATUS(decompress_result), 0);
  }

  ChromeCollectorMock collector_;
  base::FilePath test_crash_directory_;
  base::ScopedTempDir scoped_temp_dir_;

 private:
  // The proxy mock we pass to the collector_ when SetUpDBus is called. Private
  // because this is set to nullptr when SetUpDBus is run, so calling
  // EXPECT_CALL(*debugd_proxy_mock_, ...) is dangerous. Better to just set
  // things up in SetUpDriErrorStateToReturn, when we know it isn't nullptr yet.
  std::unique_ptr<org::chromium::debugdProxyMock> debugd_proxy_mock_;

  void SetUp() override {
    std::string dummy_to_check_validity;
    ASSERT_TRUE(brillo::data_encoding::Base64Decode(
        kSampleDriErrorStateEncodedLong + strlen("<base64>: "),
        &dummy_to_check_validity));

    collector_.Initialize(IsMetrics, false);
    brillo::ClearLog();

    ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir());

    test_crash_directory_ =
        scoped_temp_dir_.GetPath().Append(kTestCrashDirectory);
    ASSERT_TRUE(CreateDirectory(test_crash_directory_));
    collector_.set_crash_directory_for_test(test_crash_directory_);
  }
};

TEST_F(ChromeCollectorTest, GoodValues) {
  FilePath dir(".");
  EXPECT_TRUE(collector_.ParseCrashLog(kCrashFormatGood, dir,
                                       dir.Append("minidump.dmp"), "base"));

  // Check to see if the values made it in properly.
  std::string meta = collector_.extra_metadata_;
  EXPECT_TRUE(meta.find("value1=abcdefghij") != std::string::npos);
  EXPECT_TRUE(meta.find("value2=12345") != std::string::npos);
}

TEST_F(ChromeCollectorTest, Newlines) {
  FilePath dir(".");
  EXPECT_TRUE(collector_.ParseCrashLog(kCrashFormatEmbeddedNewline, dir,
                                       dir.Append("minidump.dmp"), "base"));

  // Check to see if the values were escaped.
  std::string meta = collector_.extra_metadata_;
  EXPECT_TRUE(meta.find("value1=abcd\\r\\nghij") != std::string::npos);
  EXPECT_TRUE(meta.find("value2=12\\n34") != std::string::npos);
}

TEST_F(ChromeCollectorTest, BadValues) {
  FilePath dir(".");
  for (const char* data : kCrashFormatBadValues) {
    brillo::ClearLog();
    EXPECT_FALSE(collector_.ParseCrashLog(data, dir, dir.Append("minidump.dmp"),
                                          "base"));
  }
}

TEST_F(ChromeCollectorTest, File) {
  base::ScopedTempDir scoped_temp_dir;
  ASSERT_TRUE(scoped_temp_dir.CreateUniqueTempDir());
  const FilePath& dir = scoped_temp_dir.GetPath();
  EXPECT_TRUE(collector_.ParseCrashLog(kCrashFormatWithFile, dir,
                                       dir.Append("minidump.dmp"), "base"));

  // Check to see if the values are still correct and that the file was
  // written with the right data.
  std::string meta = collector_.extra_metadata_;
  EXPECT_TRUE(meta.find("value1=abcdefghij") != std::string::npos);
  EXPECT_TRUE(meta.find("value2=12345") != std::string::npos);
  EXPECT_TRUE(meta.find("value3=ok") != std::string::npos);
  ExpectFileEquals("12345\n789\n12345", dir.Append("base-foo_txt.other"));
}

TEST_F(ChromeCollectorTest, HandleCrash) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn("<empty>");

  FilePath log_file;
  {
    base::ScopedFILE output(
#if BASE_VER < 780000
        base::CreateAndOpenTemporaryFileInDir
#else
        base::CreateAndOpenTemporaryStreamInDir
#endif
        (dir, &log_file));
    ASSERT_TRUE(output.get());
    base::AutoReset<FILE*> auto_reset_file_ptr(&collector_.output_file_ptr_,
                                               output.get());
    EXPECT_TRUE(
        collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test"));
  }
  ExpectFileEquals(ChromeCollector::kSuccessMagic, log_file);

  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  std::string output_dump_file_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_dump_file, &output_dump_file_contents));
  EXPECT_EQ(output_dump_file_contents, "0123456789abcdefghij");

  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));
  std::string other_file_contents;
  EXPECT_TRUE(base::ReadFileToString(other_file, &other_file_contents));
  EXPECT_EQ(other_file_contents, "12345\n789\n12345");

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + output_dump_file_contents.size() +
                other_file_contents.size());
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
}

TEST_F(ChromeCollectorTest, HandleCrashWithEmbeddedNuls) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  std::string input(kCrashFormatWithDumpFileWithEmbeddedNulBytes,
                    sizeof(kCrashFormatWithDumpFileWithEmbeddedNulBytes) - 1);
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, input));
  SetUpDriErrorStateToReturn("<empty>");

  FilePath log_file;
  {
    base::ScopedFILE output(
#if BASE_VER < 780000
        base::CreateAndOpenTemporaryFileInDir
#else
        base::CreateAndOpenTemporaryStreamInDir
#endif
        (dir, &log_file));
    ASSERT_TRUE(output.get());
    base::AutoReset<FILE*> auto_reset_file_ptr(&collector_.output_file_ptr_,
                                               output.get());
    EXPECT_TRUE(
        collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test"));
  }
  ExpectFileEquals(ChromeCollector::kSuccessMagic, log_file);

  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  std::string output_dump_file_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_dump_file, &output_dump_file_contents));
  std::string expected_dump_contents("\00012345678\000\a\bcd\x0e\x0fghij", 20);
  EXPECT_EQ(output_dump_file_contents, expected_dump_contents);

  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));
  std::string other_file_contents;
  EXPECT_TRUE(base::ReadFileToString(other_file, &other_file_contents));
  std::string expected_other_contents("12\00045\n789\n12\00045", 15);
  EXPECT_EQ(other_file_contents, expected_other_contents);

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + output_dump_file_contents.size() +
                other_file_contents.size());
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
}

TEST_F(ChromeCollectorTest, HandleCrashWithWeirdFilename) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  std::string input(kCrashFormatWithWeirdFilename,
                    sizeof(kCrashFormatWithWeirdFilename) - 1);
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, input));
  SetUpDriErrorStateToReturn("<empty>");

  FilePath log_file;
  {
    base::ScopedFILE output(
#if BASE_VER < 780000
        base::CreateAndOpenTemporaryFileInDir
#else
        base::CreateAndOpenTemporaryStreamInDir
#endif
        (dir, &log_file));
    ASSERT_TRUE(output.get());
    base::AutoReset<FILE*> auto_reset_file_ptr(&collector_.output_file_ptr_,
                                               output.get());
    EXPECT_TRUE(
        collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test"));
  }
  ExpectFileEquals(ChromeCollector::kSuccessMagic, log_file);

  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  std::string output_dump_file_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_dump_file, &output_dump_file_contents));
  EXPECT_EQ(output_dump_file_contents, "0123456789abcdefghij");

  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-___a_txt.other", &other_file));
  std::string other_file_contents;
  EXPECT_TRUE(base::ReadFileToString(other_file, &other_file_contents));
  EXPECT_EQ(other_file_contents, "12345\n789\n12345");

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + output_dump_file_contents.size() +
                other_file_contents.size());
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_dotdotfile=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
}

TEST_F(ChromeCollectorTest, HandleCrashWithLogsAndDriErrorState) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn(kSampleDriErrorStateEncoded);
  SetUpLogsShort();

  EXPECT_TRUE(collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test"));

  base::FilePath output_dri_error_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.i915_error_state.log.xz",
      &output_dri_error_file));
  std::string dri_error_file_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_dri_error_file, &dri_error_file_contents));
  EXPECT_EQ(dri_error_file_contents, kSampleDriErrorStateDecoded);

  base::FilePath output_log;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.chrome.txt.gz", &output_log));
  int64_t output_log_compressed_size = 0;
  EXPECT_TRUE(base::GetFileSize(output_log, &output_log_compressed_size));
  Decompress(output_log);
  base::FilePath output_log_uncompressed = output_log.RemoveFinalExtension();
  std::string output_log_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_log_uncompressed, &output_log_contents));
  EXPECT_EQ(output_log_contents, "hello there\n");

  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  std::string output_dump_file_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_dump_file, &output_dump_file_contents));
  EXPECT_EQ(output_dump_file_contents, "0123456789abcdefghij");

  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));
  std::string other_file_contents;
  EXPECT_TRUE(base::ReadFileToString(other_file, &other_file_contents));
  EXPECT_EQ(other_file_contents, "12345\n789\n12345");

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + output_log_compressed_size +
                dri_error_file_contents.size() + other_file_contents.size() +
                output_dump_file_contents.size());
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_chrome.txt=" +
                                            output_log.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_i915_error_state.log.xz=" +
                        output_dri_error_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
}

TEST_F(ChromeCollectorTest, HandleCrashSkipsSupplementalFilesIfDumpFileLarge) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn(kSampleDriErrorStateEncoded);
  SetUpLogsShort();
  // Make dmp file "too large"
  collector_.set_max_upload_bytes_for_test(1);
  EXPECT_TRUE(collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test"));

  // Supplemental files not written.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.i915_error_state.log.xz",
      nullptr));
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.chrome.txt.gz", nullptr));

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(
      collector_.get_bytes_written(),
      meta_file_contents.size() + kOutputDumpFileSize + kOutputOtherFileSize);
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_chrome.txt")));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state.log.xz")));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
}

TEST_F(ChromeCollectorTest, HandleCrashSkipsLargeLogFiles) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn(kSampleDriErrorStateEncoded);
  SetUpLogsLong();
  collector_.set_max_upload_bytes_for_test(1000);
  EXPECT_TRUE(collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test"));

  // Log file not written.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.chrome.txt.gz", nullptr));

  // Error state file still written even after log file rejected.
  base::FilePath output_dri_error_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.i915_error_state.log.xz",
      &output_dri_error_file));
  std::string dri_error_file_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_dri_error_file, &dri_error_file_contents));
  EXPECT_EQ(dri_error_file_contents, kSampleDriErrorStateDecoded);

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + kOutputDumpFileSize +
                kOutputOtherFileSize + dri_error_file_contents.size());
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_chrome.txt")));
  EXPECT_THAT(meta_file_contents,
              HasSubstr("upload_file_i915_error_state.log.xz=" +
                        output_dri_error_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
}

TEST_F(ChromeCollectorTest, HandleCrashSkipsLargeDriErrorFiles) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn(kSampleDriErrorStateEncodedLong);
  SetUpLogsShort();
  collector_.set_max_upload_bytes_for_test(1000);
  EXPECT_TRUE(collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test"));

  // Dri Error State file not written.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.i915_error_state.log.xz",
      nullptr));

  // Log file still written even after Dri Error State file rejected.
  base::FilePath output_log;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.chrome.txt.gz", &output_log));
  int64_t output_log_compressed_size = 0;
  EXPECT_TRUE(base::GetFileSize(output_log, &output_log_compressed_size));
  Decompress(output_log);
  base::FilePath output_log_uncompressed = output_log.RemoveFinalExtension();
  std::string output_log_contents;
  EXPECT_TRUE(
      base::ReadFileToString(output_log_uncompressed, &output_log_contents));
  EXPECT_EQ(output_log_contents, "hello there\n");

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(collector_.get_bytes_written(),
            meta_file_contents.size() + kOutputDumpFileSize +
                kOutputOtherFileSize + output_log_compressed_size);
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_chrome.txt=" +
                                            output_log.BaseName().value()));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state.log.xz=")));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
}

TEST_F(ChromeCollectorTest, HandleCrashSkipsLargeLogsAndLargeDriErrorFiles) {
  const FilePath& dir = scoped_temp_dir_.GetPath();
  FilePath input_dump_file = dir.Append("test.dmp");
  ASSERT_TRUE(test_util::CreateFile(input_dump_file, kCrashFormatWithDumpFile));
  SetUpDriErrorStateToReturn(kSampleDriErrorStateEncodedLong);
  SetUpLogsLong();
  collector_.set_max_upload_bytes_for_test(1000);
  EXPECT_TRUE(collector_.HandleCrash(input_dump_file, 123, 456, "chrome_test"));

  // Dri Error State file not written.
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.i915_error_state.log.xz",
      nullptr));

  // Log file not written
  EXPECT_FALSE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.chrome.txt.gz", nullptr));

  // .dmp file and other files in the input dump still written.
  base::FilePath output_dump_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.dmp", &output_dump_file));
  base::FilePath other_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123-foo_txt.other", &other_file));

  base::FilePath meta_file;
  EXPECT_TRUE(test_util::DirectoryHasFileWithPattern(
      test_crash_directory_, "chrome_test.*.123.meta", &meta_file));
  std::string meta_file_contents;
  EXPECT_TRUE(base::ReadFileToString(meta_file, &meta_file_contents));
  EXPECT_EQ(
      collector_.get_bytes_written(),
      meta_file_contents.size() + kOutputDumpFileSize + kOutputOtherFileSize);
  EXPECT_THAT(meta_file_contents,
              HasSubstr("payload=" + output_dump_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_file_some_file=" +
                                            other_file.BaseName().value()));
  EXPECT_THAT(meta_file_contents, Not(HasSubstr("upload_file_chrome.txt=")));
  EXPECT_THAT(meta_file_contents,
              Not(HasSubstr("upload_file_i915_error_state.log.xz=")));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value1=abcdefghij"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value2=12345"));
  EXPECT_THAT(meta_file_contents, HasSubstr("upload_var_value3=ok"));
}
