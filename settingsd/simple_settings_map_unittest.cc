// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "settingsd/simple_settings_map.h"

#include <base/logging.h>
#include <base/values.h>
#include <gtest/gtest.h>
#include <memory>

#include "settingsd/identifier_utils.h"
#include "settingsd/mock_settings_document.h"
#include "settingsd/test_helpers.h"

namespace settingsd {

class SimpleSettingsMapTest : public testing::Test {
 public:
  SimpleSettingsMapTest() {}
  ~SimpleSettingsMapTest() override {}

  void CheckSettingsMapContents(
      const std::map<Key, std::shared_ptr<base::Value>>& expected_values,
      const std::set<Key> expected_deletions,
      const SimpleSettingsMap& settings_map) {
    std::set<Key> value_keys = settings_map.GetKeys(Key());

    std::set<Key> expected_value_keys;
    for (auto& expected_value : expected_values) {
      expected_value_keys.insert(expected_value.first);
      const base::Value* value = settings_map.GetValue(expected_value.first);
      EXPECT_TRUE(base::Value::Equals(expected_value.second.get(), value))
          << "Unexpected value for key " << expected_value.first.ToString();
    }
    EXPECT_EQ(expected_value_keys, value_keys);

    std::set<Key> actual_deletions;
    for (auto& deletion : settings_map.deletion_map_) {
      actual_deletions.insert(deletion.first);
    }
    EXPECT_EQ(expected_deletions, actual_deletions);
  }

 protected:
  void SetUp() override {
    // Prepare document for writer A.
    VersionStamp version_stamp_A;
    version_stamp_A.Set("A", 1);
    version_stamp_A.Set("B", 1);
    document_A_.reset(new MockSettingsDocument(version_stamp_A));

    // Prepare Document for writer B.
    VersionStamp version_stamp_B;
    version_stamp_B.Set("A", 2);
    version_stamp_B.Set("B", 1);
    document_B_.reset(new MockSettingsDocument(version_stamp_B));

    // Prepare Document for writer C.
    VersionStamp version_stamp_C;
    version_stamp_C.Set("A", 3);
    version_stamp_C.Set("B", 1);
    document_C_.reset(new MockSettingsDocument(version_stamp_C));
  }

  std::unique_ptr<MockSettingsDocument> document_A_;
  std::unique_ptr<MockSettingsDocument> document_B_;
  std::unique_ptr<MockSettingsDocument> document_C_;
};

TEST_F(SimpleSettingsMapTest, InsertionSingleDocument) {
  document_A_->SetEntry(Key("A.B.C"), MakeIntValue(1));
  document_A_->SetDeletion(Key("A.B"));
  document_A_->SetDeletion(Key("B"));

  SimpleSettingsMap settings_map;
  settings_map.InsertDocument(std::move(document_A_));

  std::set<Key> expected_deletions = {Key("B"), Key("A.B")};
  std::map<Key, std::shared_ptr<base::Value>> expected_values {
      {Key("A.B.C"), MakeIntValue(1)},
  };
  CheckSettingsMapContents(expected_values, expected_deletions, settings_map);
}

TEST_F(SimpleSettingsMapTest, InsertionTwoDocuments) {
  document_A_->SetEntry(Key("A.B.C"), MakeIntValue(1));
  document_A_->SetDeletion(Key("A.B"));
  document_A_->SetDeletion(Key("B"));
  document_A_->SetEntry(Key("B.C"), MakeIntValue(2));
  document_B_->SetEntry(Key("B.C"), MakeIntValue(3));
  document_B_->SetDeletion(Key("A"));

  SimpleSettingsMap settings_map;
  settings_map.InsertDocument(std::move(document_A_));
  settings_map.InsertDocument(std::move(document_B_));

  std::set<Key> expected_deletions = {Key("A"), Key("B")};
  std::map<Key, std::shared_ptr<base::Value>> expected_values {
      {Key("B.C"), MakeIntValue(3)},
  };
  CheckSettingsMapContents(expected_values, expected_deletions, settings_map);
}

TEST_F(SimpleSettingsMapTest, InsertionTwoDocumentsInverseOrder) {
  document_A_->SetEntry(Key("A.B.C"), MakeIntValue(1));
  document_A_->SetDeletion(Key("A.B"));
  document_A_->SetDeletion(Key("B"));
  document_B_->SetEntry(Key("B.C"), MakeIntValue(2));
  document_B_->SetDeletion(Key("A"));

  SimpleSettingsMap settings_map;
  settings_map.InsertDocument(std::move(document_B_));
  settings_map.InsertDocument(std::move(document_A_));

  std::set<Key> expected_deletions = {Key("A"), Key("B")};
  std::map<Key, std::shared_ptr<base::Value>> expected_values = {
      {Key("B.C"), MakeIntValue(2)},
  };
  CheckSettingsMapContents(expected_values, expected_deletions, settings_map);
}

TEST_F(SimpleSettingsMapTest, DocumentRemoval) {
  document_A_->SetEntry(Key("A"), MakeIntValue(1));
  document_A_->SetEntry(Key("B"), MakeIntValue(2));
  document_B_->SetEntry(Key("B"), MakeIntValue(3));

  SimpleSettingsMap settings_map;
  SettingsDocument* document_B_ptr = document_B_.get();
  settings_map.InsertDocument(std::move(document_A_));
  settings_map.InsertDocument(std::move(document_B_));
  settings_map.RemoveDocument(document_B_ptr);

  std::set<Key> expected_deletions = {};
  std::map<Key, std::shared_ptr<base::Value>> expected_values {
      {Key("A"), MakeIntValue(1)},
      {Key("B"), MakeIntValue(2)},
  };
  CheckSettingsMapContents(expected_values, expected_deletions, settings_map);
}

TEST_F(SimpleSettingsMapTest, RemovalOfDeletion) {
  document_A_->SetEntry(Key("A"), MakeIntValue(1));
  document_A_->SetEntry(Key("B.C"), MakeIntValue(2));
  document_B_->SetDeletion(Key("B"));

  SimpleSettingsMap settings_map;
  SettingsDocument* document_B_ptr = document_B_.get();
  settings_map.InsertDocument(std::move(document_A_));
  settings_map.InsertDocument(std::move(document_B_));
  settings_map.RemoveDocument(document_B_ptr);

  std::set<Key> expected_deletions = {};
  std::map<Key, std::shared_ptr<base::Value>> expected_values = {
      {Key("A"), MakeIntValue(1)},
      {Key("B.C"), MakeIntValue(2)},
  };
  CheckSettingsMapContents(expected_values, expected_deletions, settings_map);
}

TEST_F(SimpleSettingsMapTest, RemovalOfDeletionChildPrefixShineThrough) {
  document_A_->SetEntry(Key("A.B.D"), MakeIntValue(1));
  document_A_->SetEntry(Key("Z.A"), MakeIntValue(-1));
  document_B_->SetEntry(Key("A.B.C"), MakeIntValue(2));
  document_B_->SetEntry(Key("Z.B"), MakeIntValue(-1));
  document_C_->SetDeletion(Key("A.B"));

  SimpleSettingsMap settings_map;
  SettingsDocument* document_C_ptr = document_C_.get();
  settings_map.InsertDocument(std::move(document_A_));
  settings_map.InsertDocument(std::move(document_B_));
  settings_map.InsertDocument(std::move(document_C_));
  settings_map.RemoveDocument(document_C_ptr);

  std::set<Key> expected_deletions = {};
  std::map<Key, std::shared_ptr<base::Value>> expected_values = {
      {Key("A.B.C"), MakeIntValue(2)},
      {Key("A.B.D"), MakeIntValue(1)},
      {Key("Z.A"), MakeIntValue(-1)},
      {Key("Z.B"), MakeIntValue(-1)},
  };
  CheckSettingsMapContents(expected_values, expected_deletions, settings_map);
}

TEST_F(SimpleSettingsMapTest, RemovalOfDeletionParentDeleterUpstream) {
  document_A_->SetEntry(Key("A.A"), MakeIntValue(1));
  document_A_->SetEntry(Key("A.B.C"), MakeIntValue(2));
  document_A_->SetEntry(Key("Z.A"), MakeIntValue(-1));
  document_B_->SetDeletion(Key("A"));
  document_B_->SetEntry(Key("Z.B"), MakeIntValue(-1));
  document_C_->SetDeletion(Key("A.B"));

  SimpleSettingsMap settings_map;
  SettingsDocument* document_C_ptr = document_C_.get();
  settings_map.InsertDocument(std::move(document_A_));
  settings_map.InsertDocument(std::move(document_B_));
  settings_map.InsertDocument(std::move(document_C_));
  settings_map.RemoveDocument(document_C_ptr);

  std::set<Key> expected_deletions = { Key("A") };
  std::map<Key, std::shared_ptr<base::Value>> expected_values = {
      {Key("Z.A"), MakeIntValue(-1)},
      {Key("Z.B"), MakeIntValue(-1)},
  };
  CheckSettingsMapContents(expected_values, expected_deletions, settings_map);
}

TEST_F(SimpleSettingsMapTest, RemovalOfDeletionChildDeleterUpstream) {
  document_A_->SetEntry(Key("A.B.C.D"), MakeIntValue(1));
  document_A_->SetEntry(Key("A.B.D"), MakeIntValue(2));
  document_A_->SetEntry(Key("Z.A"), MakeIntValue(-1));
  document_B_->SetDeletion(Key("A.B.C"));
  document_B_->SetEntry(Key("Z.B"), MakeIntValue(-1));
  document_C_->SetDeletion(Key("A.B"));

  SimpleSettingsMap settings_map;
  SettingsDocument* document_C_ptr = document_C_.get();
  settings_map.InsertDocument(std::move(document_A_));
  settings_map.InsertDocument(std::move(document_B_));
  settings_map.InsertDocument(std::move(document_C_));
  settings_map.RemoveDocument(document_C_ptr);

  std::set<Key> expected_deletions = { Key("A.B.C") };
  std::map<Key, std::shared_ptr<base::Value>> expected_values = {
      {Key("A.B.D"), MakeIntValue(2)},
      {Key("Z.A"), MakeIntValue(-1)},
      {Key("Z.B"), MakeIntValue(-1)},
  };
  CheckSettingsMapContents(expected_values, expected_deletions, settings_map);
}

TEST_F(SimpleSettingsMapTest, BasicRemovalOfDeletionSameDeletionUpstream) {
  document_A_->SetEntry(Key("A.B.C.D"), MakeIntValue(1));
  document_A_->SetEntry(Key("A.B.D"), MakeIntValue(2));
  document_A_->SetEntry(Key("Z.A"), MakeIntValue(-1));
  document_B_->SetDeletion(Key("A.B"));
  document_B_->SetEntry(Key("A.B.C"), MakeIntValue(3));
  document_B_->SetEntry(Key("Z.B"), MakeIntValue(-1));
  document_C_->SetDeletion(Key("A.B"));

  SimpleSettingsMap settings_map;
  SettingsDocument* document_C_ptr = document_C_.get();
  settings_map.InsertDocument(std::move(document_A_));
  settings_map.InsertDocument(std::move(document_B_));
  settings_map.InsertDocument(std::move(document_C_));
  settings_map.RemoveDocument(document_C_ptr);

  std::set<Key> expected_deletions = { Key("A.B") };
  std::map<Key, std::shared_ptr<base::Value>> expected_values = {
      {Key("A.B.C"), MakeIntValue(3)},
      {Key("Z.A"), MakeIntValue(-1)},
      {Key("Z.B"), MakeIntValue(-1)},
  };
  CheckSettingsMapContents(expected_values, expected_deletions, settings_map);
}

}  // namespace settingsd
