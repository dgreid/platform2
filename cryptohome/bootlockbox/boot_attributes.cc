// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/bootlockbox/boot_attributes.h"

#include <base/files/file_path.h>
#include <base/logging.h>
#include <brillo/secure_blob.h>

#include "cryptohome/bootlockbox/boot_lockbox.h"
#include "cryptohome/install_attributes.pb.h"
#include "cryptohome/platform.h"

using base::FilePath;

namespace cryptohome {

const int BootAttributes::kAttributeFileVersion = 1;

const char BootAttributes::kAttributeFile[] =
  "/var/lib/boot-lockbox/boot_attributes.pb";

const char BootAttributes::kSignatureFile[] =
  "/var/lib/boot-lockbox/boot_attributes.sig";

BootAttributes::BootAttributes(BootLockbox* boot_lockbox, Platform* platform)
    : boot_lockbox_(boot_lockbox),
      platform_(platform) {
}

BootAttributes::~BootAttributes() {
}

bool BootAttributes::Load() {
  brillo::Blob data;
  brillo::SecureBlob signature;
  if (!platform_->ReadFile(FilePath(kAttributeFile), &data) ||
      !platform_->ReadFileToSecureBlob(FilePath(kSignatureFile), &signature)) {
    LOG(INFO) << "Cannot read boot lockbox files.";
    return false;
  }

  if (!boot_lockbox_->Verify(data, signature)) {
    LOG(ERROR) << "Cannot verify the signature of the boot lockbox.";
    return false;
  }

  SerializedInstallAttributes message;
  if (!message.ParseFromArray(data.data(), data.size())) {
    LOG(ERROR) << "Cannot parse the content of the boot lockbox.";
    return false;
  }

  AttributeMap tmp;
  for (int i = 0; i < message.attributes_size(); ++i) {
    const std::string& name = message.attributes(i).name();
    const std::string& value = message.attributes(i).value();
    tmp[name] = value;
  }

  attributes_.swap(tmp);
  write_buffer_ = attributes_;
  return true;
}

bool BootAttributes::Get(const std::string& name, std::string* value) const {
  AttributeMap::const_iterator it = attributes_.find(name);
  if (it == attributes_.end()) {
    return false;
  }

  *value = it->second;
  return true;
}

void BootAttributes::Set(const std::string& name, const std::string& value) {
  write_buffer_[name] = value;
}

bool BootAttributes::FlushAndSign() {
  SerializedInstallAttributes message;
  message.set_version(kAttributeFileVersion);

  AttributeMap::const_iterator it;
  for (it = write_buffer_.begin(); it != write_buffer_.end(); ++it) {
    SerializedInstallAttributes::Attribute* attr = message.add_attributes();
    attr->set_name(it->first);
    attr->set_value(it->second);
  }

  brillo::Blob content;
  content.resize(message.ByteSizeLong());
  message.SerializeWithCachedSizesToArray(content.data());

  brillo::SecureBlob signature;
  if (!boot_lockbox_->Sign(content, &signature)) {
    return false;
  }

  // Write the attributes and the signature to the files.
  if (!platform_->WriteFile(FilePath(kAttributeFile), content)) {
    LOG(ERROR) << "Failed to write to the boot attribute file.";
    return false;
  }
  brillo::Blob signature_content(signature.begin(), signature.end());
  if (!platform_->WriteFile(FilePath(kSignatureFile), signature_content)) {
    LOG(ERROR) << "Failed to write to the boot attribute signature file.";
    return false;
  }

  // Since two files are written, atomicity cannot be achieved easily.
  // Therefore this only aims for durability.
  platform_->DataSyncFile(FilePath(kAttributeFile));
  platform_->DataSyncFile(FilePath(kSignatureFile));

  attributes_ = write_buffer_;
  return true;
}

}  // namespace cryptohome
