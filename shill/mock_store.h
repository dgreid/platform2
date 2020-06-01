// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_STORE_H_
#define SHILL_MOCK_STORE_H_

#include <set>
#include <string>
#include <vector>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/key_value_store.h"
#include "shill/store_interface.h"

namespace shill {

class MockStore : public StoreInterface {
 public:
  MockStore();
  ~MockStore() override;

  MOCK_METHOD(bool, IsEmpty, (), (const, override));
  MOCK_METHOD(bool, Open, (), (override));
  MOCK_METHOD(bool, Close, (), (override));
  MOCK_METHOD(bool, Flush, (), (override));
  MOCK_METHOD(bool, MarkAsCorrupted, (), (override));
  MOCK_METHOD(std::set<std::string>, GetGroups, (), (const, override));
  MOCK_METHOD(std::set<std::string>,
              GetGroupsWithKey,
              (const std::string&),
              (const, override));
  MOCK_METHOD(std::set<std::string>,
              GetGroupsWithProperties,
              (const KeyValueStore&),
              (const, override));
  MOCK_METHOD(bool, ContainsGroup, (const std::string&), (const, override));
  MOCK_METHOD(bool,
              DeleteKey,
              (const std::string&, const std::string&),
              (override));
  MOCK_METHOD(bool, DeleteGroup, (const std::string&), (override));
  MOCK_METHOD(bool, SetHeader, (const std::string&), (override));
  MOCK_METHOD(bool,
              GetString,
              (const std::string&, const std::string&, std::string*),
              (const, override));
  MOCK_METHOD(bool,
              SetString,
              (const std::string&, const std::string&, const std::string&),
              (override));
  MOCK_METHOD(bool,
              GetBool,
              (const std::string&, const std::string&, bool*),
              (const, override));
  MOCK_METHOD(bool,
              SetBool,
              (const std::string&, const std::string&, bool),
              (override));
  MOCK_METHOD(bool,
              GetInt,
              (const std::string&, const std::string&, int*),
              (const, override));
  MOCK_METHOD(bool,
              SetInt,
              (const std::string&, const std::string&, int),
              (override));
  MOCK_METHOD(bool,
              GetUint64,
              (const std::string&, const std::string&, uint64_t*),
              (const, override));
  MOCK_METHOD(bool,
              SetUint64,
              (const std::string&, const std::string&, uint64_t),
              (override));
  MOCK_METHOD(bool,
              GetStringList,
              (const std::string&,
               const std::string&,
               std::vector<std::string>*),
              (const, override));
  MOCK_METHOD(bool,
              SetStringList,
              (const std::string&,
               const std::string&,
               const std::vector<std::string>&),
              (override));
  MOCK_METHOD(bool,
              GetCryptedString,
              (const std::string&,
               const std::string&,
               const std::string&,
               std::string*),
              (const, override));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockStore);
};

}  // namespace shill

#endif  // SHILL_MOCK_STORE_H_
