// Copyright (c) 2010 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These functions can parse a blob of data that's formatted as a simple
// key value store. Each key/value pair is stored on its own line and
// separated by the first '=' on the line.

#ifndef LIBCHROMEOS_CHROMEOS_KEY_VALUE_STORE_H_
#define LIBCHROMEOS_CHROMEOS_KEY_VALUE_STORE_H_

#include <map>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <chromeos/chromeos_export.h>

namespace chromeos {

class CHROMEOS_EXPORT KeyValueStore {
 public:
  // Creates an empty KeyValueStore.
  KeyValueStore() = default;
  virtual ~KeyValueStore() = default;

  // Loads the key=value pairs from the given |path|. Lines starting with '#'
  // and empty lines are ignored, and whitespace around keys is trimmed.
  // Trailing backslashes may be used to extend values across multiple lines.
  // Adds all the read key=values to the store, overriding those already defined
  // but persisting the ones that aren't present on the passed file. Returns
  // whether reading the file succeeded.
  bool Load(const base::FilePath& path);

  // Saves the current store to the given |path| file. Returns whether the file
  // creation succeeded. Calling Load() and then Save() may result in different
  // data being written if the original file contained backslash-terminated
  // lines (i.e. these values will be rewritten on single lines).
  bool Save(const base::FilePath& path) const;

  // Getter for the given key. Returns whether the key was found on the store.
  bool GetString(const std::string& key, std::string* value) const;

  // Setter for the given key. It overrides the key if already exists.
  void SetString(const std::string& key, const std::string& value);

  // Boolean getter. Returns whether the key was found on the store and if it
  // has a valid value ("true" or "false").
  bool GetBoolean(const std::string& key, bool* value) const;

  // Boolean setter. Sets the value as "true" or "false".
  void SetBoolean(const std::string& key, bool value);

  // Retrieves the keys for all values currently stored in the map.
  std::vector<std::string> GetKeys() const;

 private:
  // The map storing all the key-value pairs.
  std::map<std::string, std::string> store_;

  DISALLOW_COPY_AND_ASSIGN(KeyValueStore);
};

}  // namespace chromeos

#endif  // LIBCHROMEOS_CHROMEOS_KEY_VALUE_STORE_H_
