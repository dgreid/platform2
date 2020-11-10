// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_BOOTLOCKBOX_BOOT_ATTRIBUTES_H_
#define CRYPTOHOME_BOOTLOCKBOX_BOOT_ATTRIBUTES_H_

#include <map>
#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>

namespace cryptohome {

class BootLockbox;
class Platform;

// BootAttributes is a key-value storage that is built on top of BootLockbox.
// Data stored in BootAttributes can only be modified until a user session
// starts after boot. The data is signed and is tamper-evident. See BootLockbox
// for more detail.
class BootAttributes {
 public:
  static const int kAttributeFileVersion;
  static const char kAttributeFile[];
  static const char kSignatureFile[];

  // Does not take ownership of pointers.
  BootAttributes(BootLockbox* boot_lockbox, Platform* platform);
  BootAttributes(const BootAttributes&) = delete;
  BootAttributes& operator=(const BootAttributes&) = delete;

  virtual ~BootAttributes();

  // Loads the attributes from the file and verifies the signature. Returns
  // true upon success. Returns false if the files do not exist, cannot be
  // read, cannot be parsed, or the signature cannot be verified.
  virtual bool Load();

  // Gets the value of the specified attribute. Returns true upon success.
  // Returns false if the attribute does not exist.
  virtual bool Get(const std::string& name, std::string* value) const;

  // Sets the value of the specified attribute. The value won't be available
  // until FlushAndSign() is called. If |name| already exists, it will override
  // the value.
  virtual void Set(const std::string& name, const std::string& value);

  // Applies all the pending value settings. The content is written to the file
  // and the signature is updated. Returns true upon success. Returns false if
  // the files cannot be written, or the content cannot be signed due to an
  // underlying error.
  virtual bool FlushAndSign();

 private:
  typedef std::map<std::string, std::string> AttributeMap;

  BootLockbox* boot_lockbox_;
  Platform* platform_;

  AttributeMap attributes_;
  AttributeMap write_buffer_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_BOOTLOCKBOX_BOOT_ATTRIBUTES_H_
