// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <tuple>
#include <utility>

#include <base/files/file_util.h>
#include <base/strings/string_util.h>

#include "runtime_probe/utils/file_utils.h"

using base::FilePath;
using base::PathExists;
using base::ReadFileToStringWithMaxSize;
using base::TrimWhitespaceASCII;
using base::Value;
using base::TrimPositions::TRIM_ALL;
using std::pair;
using std::string;
using std::vector;

namespace {

constexpr int kReadFileMaxSize = 1024;

// Get string to be used as key name in |MapFilesToDict|.
string GetKeyName(const string& key) {
  return key;
}

string GetKeyName(const pair<string, string>& key) {
  return key.first;
}

// Get string to be used as file name in |MapFilesToDict|.
string GetFileName(const string& key) {
  return key;
}

string GetFileName(const pair<string, string>& key) {
  return key.second;
}

}  // namespace

namespace runtime_probe {

template <typename KeyType>
base::Optional<Value> MapFilesToDict(const FilePath& dir_path,
                                     const vector<KeyType>& keys,
                                     const vector<KeyType>& optional_keys) {
  Value ret(Value::Type::DICTIONARY);

  for (const auto& key : keys) {
    string file_name = GetFileName(key);
    string key_name = GetKeyName(key);
    const auto file_path = dir_path.Append(file_name);
    string content;

    // missing file
    if (!PathExists(file_path)) {
      LOG(ERROR) << file_path.value() << " doesn't exist";
      return base::nullopt;
    }

    // File exists, but somehow we can't read it.
    if (!ReadFileToStringWithMaxSize(file_path, &content, kReadFileMaxSize)) {
      LOG(ERROR) << file_path.value() << " exists, but we can't read it";
      return base::nullopt;
    }

    ret.SetStringKey(key_name, TrimWhitespaceASCII(content, TRIM_ALL));
  }

  for (const auto& key : optional_keys) {
    string file_name = GetFileName(key);
    string key_name = GetKeyName(key);
    const auto file_path = dir_path.Append(file_name);
    string content;

    if (!base::PathExists(file_path))
      continue;

    if (ReadFileToStringWithMaxSize(file_path, &content, kReadFileMaxSize))
      ret.SetStringKey(key_name, TrimWhitespaceASCII(content, TRIM_ALL));
  }

  return ret;
}

// Explicit template instantiation
template base::Optional<Value> MapFilesToDict<string>(
    const FilePath& dir_path,
    const vector<string>& keys,
    const vector<string>& optional_keys);

// Explicit template instantiation
template base::Optional<Value> MapFilesToDict<pair<string, string>>(
    const FilePath& dir_path,
    const vector<pair<string, string>>& keys,
    const vector<pair<string, string>>& optional_keys);

}  // namespace runtime_probe
