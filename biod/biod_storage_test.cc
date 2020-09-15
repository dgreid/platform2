// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_storage.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <testing/gtest/include/gtest/gtest.h>
#include <base/strings/string_util.h>

namespace biod {

namespace {

const char kBiometricsManagerName[] = "BiometricsManager";

const base::FilePath kFilePath("TestFile");

constexpr int kInvalidRecordFormatVersion = -1;

const char kRecordId1[] = "00000000_0000_0000_0000_000000000001";
const char kUserId1[] = "0000000000000000000000000000000000000001";
const char kLabel1[] = "record1";
const std::vector<uint8_t> kValidationVal1 = {0x00, 0x01};
const char kData1[] = "Hello, world1!";

const char kRecordId2[] = "00000000_0000_0000_0000_000000000002";
const char kUserId2[] = "0000000000000000000000000000000000000002";
const char kLabel2[] = "record2";
const std::vector<uint8_t> kValidationVal2 = {0x00, 0x02};
const char kData2[] = "Hello, world2!";

const char kRecordId3[] = "00000000_0000_0000_0000_000000000003";
const char kLabel3[] = "record3";
const std::vector<uint8_t> kValidationVal3 = {0x00, 0x03};
const char kData3[] = "Hello, world3!";

constexpr int kPermissions600 =
    base::FILE_PERMISSION_READ_BY_USER | base::FILE_PERMISSION_WRITE_BY_USER;
constexpr int kPermissions700 = base::FILE_PERMISSION_USER_MASK;

const char kInvalidUTF8[] = "\xed\xa0\x80\xed\xbf\xbf";

// Flag to control whether to run tests with positive match secret support.
// This can't be a member of the test fixture because it's accessed in the
// TestRecord class in anonymous namespace.
bool test_record_supports_positive_match_secret = true;

class TestRecord : public BiometricsManager::Record {
 public:
  TestRecord(const std::string& id,
             const std::string& user_id,
             const std::string& label,
             const std::vector<uint8_t>& validation_val,
             const std::string& data)
      : id_(id),
        user_id_(user_id),
        label_(label),
        validation_val_(validation_val),
        data_(data) {}

  // BiometricsManager::Record overrides:
  const std::string& GetId() const override { return id_; }
  const std::string& GetUserId() const override { return user_id_; }
  const std::string& GetLabel() const override { return label_; }
  const std::vector<uint8_t>& GetValidationVal() const override {
    return validation_val_;
  }
  const std::string& GetData() const { return data_; }
  bool SetLabel(std::string label) override { return true; }
  bool Remove() override { return true; }
  bool SupportsPositiveMatchSecret() const override {
    return test_record_supports_positive_match_secret;
  }
  bool NeedsNewValidationValue() const override { return false; }

  void ClearValidationValue() { validation_val_.clear(); }

  friend bool operator==(const TestRecord& lhs, const TestRecord& rhs) {
    return lhs.id_ == rhs.id_ && lhs.user_id_ == rhs.user_id_ &&
           lhs.validation_val_ == rhs.validation_val_ &&
           lhs.label_ == rhs.label_ && lhs.data_ == rhs.data_;
  }
  friend inline bool operator!=(const TestRecord& lhs, const TestRecord& rhs) {
    return !(lhs == rhs);
  }

 private:
  std::string id_;
  std::string user_id_;
  std::string label_;
  std::vector<uint8_t> validation_val_;
  std::string data_;
};
}  // namespace

class BiodStorageBaseTest : public ::testing::Test {
 public:
  BiodStorageBaseTest() {
    CHECK(temp_dir_.CreateUniqueTempDir());
    root_path_ = temp_dir_.GetPath().AppendASCII("biod_storage_unittest_root");
    biod_storage_ = std::make_unique<BiodStorage>(
        kBiometricsManagerName,
        base::Bind(&BiodStorageBaseTest::LoadRecord, base::Unretained(this)));
    // Since there is no session manager, allow accesses by default.
    biod_storage_->set_allow_access(true);
    biod_storage_->SetRootPathForTesting(root_path_);
  }

  ~BiodStorageBaseTest() override {
    EXPECT_TRUE(base::DeleteFile(temp_dir_.GetPath(), true));
  }

  base::DictionaryValue CreateRecordDictionary(
      const std::vector<uint8_t>& validation_val) {
    base::DictionaryValue record_dictionary;
    std::string validation_value_str(validation_val.begin(),
                                     validation_val.end());
    base::Base64Encode(validation_value_str, &validation_value_str);
    record_dictionary.SetString("match_validation_value", validation_value_str);
    return record_dictionary;
  }

 protected:
  base::ScopedTempDir temp_dir_;
  base::FilePath root_path_;
  std::unique_ptr<BiodStorage> biod_storage_;
  std::vector<TestRecord> records_;

 private:
  // LoadRecord is a callback passed to biod_storage_. It gets called when
  // biod_storage_ reads a record from storage. It loads the new record into
  // records_.
  bool LoadRecord(int record_format_version,
                  const std::string& user_id,
                  const std::string& label,
                  const std::string& record_id,
                  const std::vector<uint8_t>& validation_val,
                  const base::Value& data_value) {
    std::string data;
    data_value.GetAsString(&data);
    records_.push_back(
        TestRecord(record_id, user_id, label, validation_val, data));
    return true;
  }

  DISALLOW_COPY_AND_ASSIGN(BiodStorageBaseTest);
};

class BiodStorageTest : public BiodStorageBaseTest,
                        public ::testing::WithParamInterface<bool> {
 public:
  BiodStorageTest() { test_record_supports_positive_match_secret = GetParam(); }
};

TEST_P(BiodStorageTest, WriteAndReadRecords) {
  const std::vector<uint8_t> kEmpty;
  const std::vector<TestRecord> kRecords = {
      TestRecord(
          kRecordId1, kUserId1, kLabel1,
          test_record_supports_positive_match_secret ? kValidationVal1 : kEmpty,
          kData1),
      TestRecord(
          kRecordId2, kUserId2, kLabel2,
          test_record_supports_positive_match_secret ? kValidationVal2 : kEmpty,
          kData2),
      TestRecord(
          kRecordId3, kUserId2, kLabel3,
          test_record_supports_positive_match_secret ? kValidationVal3 : kEmpty,
          kData3)};

  // Write the record.
  for (auto const& record : kRecords) {
    EXPECT_TRUE(biod_storage_->WriteRecord(
        record, std::make_unique<base::Value>(record.GetData())));
  }

  // Read the record.
  std::unordered_set<std::string> user_ids({kUserId1, kUserId2});
  EXPECT_TRUE(biod_storage_->ReadRecords(user_ids));
  EXPECT_TRUE(
      std::is_permutation(kRecords.begin(), kRecords.end(), records_.begin()));
}

TEST_F(BiodStorageBaseTest, WriteRecord_InvalidAbsolutePath) {
  auto record =
      TestRecord(kRecordId1, "/absolutepath", kLabel1, kValidationVal1, kData1);

  EXPECT_FALSE(biod_storage_->WriteRecord(
      record, std::make_unique<base::Value>(record.GetData())));
}

TEST_F(BiodStorageBaseTest, WriteRecord_RecordIdNotUTF8) {
  EXPECT_FALSE(base::IsStringUTF8(kInvalidUTF8));

  auto record =
      TestRecord(kInvalidUTF8, kUserId1, kLabel1, kValidationVal1, kData1);

  EXPECT_FALSE(record.IsValidUTF8());
  EXPECT_FALSE(biod_storage_->WriteRecord(
      record, std::make_unique<base::Value>(record.GetData())));
}

TEST_F(BiodStorageBaseTest, WriteRecord_UserIdNotUTF8) {
  EXPECT_FALSE(base::IsStringUTF8(kInvalidUTF8));

  auto record =
      TestRecord(kRecordId1, kInvalidUTF8, kLabel1, kValidationVal1, kData1);

  EXPECT_FALSE(record.IsValidUTF8());
  EXPECT_FALSE(biod_storage_->WriteRecord(
      record, std::make_unique<base::Value>(record.GetData())));
}

TEST_F(BiodStorageBaseTest, WriteRecord_LabelNotUTF8) {
  EXPECT_FALSE(base::IsStringUTF8(kInvalidUTF8));

  auto record =
      TestRecord(kRecordId1, kUserId1, kInvalidUTF8, kValidationVal1, kData1);

  EXPECT_FALSE(record.IsValidUTF8());
  EXPECT_FALSE(biod_storage_->WriteRecord(
      record, std::make_unique<base::Value>(record.GetData())));
}

TEST_F(BiodStorageBaseTest, WriteRecord_CheckUmask) {
  auto record =
      TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal1, kData1);

  const base::FilePath kRecordStorageFilename =
      root_path_.Append("biod")
          .Append(record.GetUserId())
          .Append(kBiometricsManagerName)
          .Append("Record" + record.GetId());

  ASSERT_FALSE(base::PathExists(kRecordStorageFilename));
  ASSERT_FALSE(base::PathExists(kRecordStorageFilename.DirName()));

  EXPECT_TRUE(biod_storage_->WriteRecord(
      record, std::make_unique<base::Value>(record.GetData())));

  // Check permissions of directory
  int actual_permissions;
  EXPECT_TRUE(base::GetPosixFilePermissions(kRecordStorageFilename.DirName(),
                                            &actual_permissions));
  EXPECT_EQ(kPermissions700, actual_permissions);

  // Check permissions of record
  EXPECT_TRUE(base::GetPosixFilePermissions(kRecordStorageFilename,
                                            &actual_permissions));
  EXPECT_EQ(kPermissions600, actual_permissions);
}

TEST_P(BiodStorageTest, DeleteRecord) {
  const std::vector<uint8_t> kEmpty;
  const TestRecord kRecord(
      kRecordId1, kUserId1, kLabel1,
      test_record_supports_positive_match_secret ? kValidationVal1 : kEmpty,
      kData1);

  // Delete a non-existent record.
  EXPECT_TRUE(biod_storage_->DeleteRecord(kUserId1, kRecordId1));

  EXPECT_TRUE(biod_storage_->WriteRecord(
      kRecord, std::make_unique<base::Value>(kRecord.GetData())));

  // Check this record is properly written.
  std::unordered_set<std::string> user_ids({kUserId1});
  EXPECT_TRUE(biod_storage_->ReadRecords(user_ids));
  ASSERT_FALSE(records_.empty());
  ASSERT_EQ(1u, records_.size());
  EXPECT_EQ(records_[0], kRecord);

  records_.clear();

  EXPECT_TRUE(biod_storage_->DeleteRecord(kUserId1, kRecordId1));

  // Check this record is properly deleted.
  EXPECT_TRUE(biod_storage_->ReadRecords(user_ids));
  EXPECT_TRUE(records_.empty());
}

TEST_F(BiodStorageBaseTest, GenerateNewRecordId) {
  // Check the two record ids are different.
  std::string record_id1(biod_storage_->GenerateNewRecordId());
  std::string record_id2(biod_storage_->GenerateNewRecordId());
  EXPECT_NE(record_id1, record_id2);
}

TEST_F(BiodStorageBaseTest, TestEqualOperator) {
  EXPECT_EQ(TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal1, kData1),
            TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal1, kData1));

  EXPECT_NE(TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal1, kData1),
            TestRecord(kRecordId1, kUserId1, kLabel1, kValidationVal2, kData1));
}

TEST_F(BiodStorageBaseTest, TestReadValidationValueFromRecord) {
  auto record_dictionary = CreateRecordDictionary(kValidationVal1);
  auto ret = biod_storage_->ReadValidationValueFromRecord(
      kRecordFormatVersion, &record_dictionary, kFilePath);
  EXPECT_TRUE(ret != nullptr);
  EXPECT_EQ(*ret, kValidationVal1);
}

TEST_F(BiodStorageBaseTest, TestReadValidationValueFromRecordOldVersion) {
  auto record_dictionary = CreateRecordDictionary(kValidationVal1);
  std::vector<uint8_t> empty;
  auto ret = biod_storage_->ReadValidationValueFromRecord(
      kRecordFormatVersionNoValidationValue, &record_dictionary, kFilePath);
  EXPECT_TRUE(ret != nullptr);
  EXPECT_EQ(*ret, empty);
}

TEST_F(BiodStorageBaseTest, TestReadValidationValueFromRecordInvalidVersion) {
  auto record_dictionary = CreateRecordDictionary(kValidationVal1);
  std::vector<uint8_t> empty;
  auto ret = biod_storage_->ReadValidationValueFromRecord(
      kInvalidRecordFormatVersion, &record_dictionary, kFilePath);
  EXPECT_EQ(ret, nullptr);
}

INSTANTIATE_TEST_SUITE_P(RecordsSupportPositiveMatchSecret,
                         BiodStorageTest,
                         ::testing::Values(true, false));
}  // namespace biod
