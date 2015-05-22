// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Wrapper around /etc/os-release and /etc/os-release.d.
// Standard fields can come from both places depending on how we set them. They
// should always be accessed through this interface.

#ifndef LIBCHROMEOS_CHROMEOS_OSRELEASE_READER_H_
#define LIBCHROMEOS_CHROMEOS_OSRELEASE_READER_H_

#include <string>

#include <chromeos/chromeos_export.h>
#include <chromeos/key_value_store.h>
#include <gtest/gtest_prod.h>

namespace chromeos {

class CHROMEOS_EXPORT OsReleaseReader final {
 public:
  // Create an empty reader
  OsReleaseReader() = default;

  // Loads the key=value pairs from either /etc/os-release.d/<KEY> or
  // /etc/os-release.
  void Load();

  // Same as the private Load method.
  // This need to be public so that services can use it in testing mode (for
  // autotest tests for example).
  // This should not be used in production so suffix it with TestingOnly to
  // make it obvious.
  void LoadTestingOnly(const base::FilePath& root_dir);

  // Getter for the given key. Returns whether the key was found on the store.
  bool GetString(const std::string& key, std::string* value) const;

 private:
  // The map storing all the key-value pairs.
  KeyValueStore store_;

  // os-release can be lazily loaded if need be.
  bool initialized_;

  // Load the data from a given root_dir.
  CHROMEOS_PRIVATE void Load(const base::FilePath& root_dir);

  DISALLOW_COPY_AND_ASSIGN(OsReleaseReader);
};

}  // namespace chromeos

#endif  // LIBCHROMEOS_CHROMEOS_OSRELEASE_READER_H_
