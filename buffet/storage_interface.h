// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUFFET_STORAGE_INTERFACE_H_
#define BUFFET_STORAGE_INTERFACE_H_

#include <memory>

#include <base/values.h>

namespace buffet {

// We need to persist data in a couple places, and it is convenient to hide
// the details of this storage behind an interface for test purposes.
class StorageInterface {
 public:
  virtual ~StorageInterface() = default;

  // Load the dictionary from storage. If it fails (e.g. the storage container
  // [file?] doesn't exist), then it returns empty unique_ptr (aka nullptr).
  virtual std::unique_ptr<base::DictionaryValue> Load() = 0;

  // Save the dictionary to storage. If saved successfully, returns true. Could
  // fail when writing to physical storage like file system for various reasons
  // (out of disk space,access permissions, etc).
  virtual bool Save(const base::DictionaryValue& config) = 0;
};

}  // namespace buffet

#endif  // BUFFET_STORAGE_INTERFACE_H_
