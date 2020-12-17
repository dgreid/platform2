// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FAKE_BACKING_DEVICE_H_
#define CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FAKE_BACKING_DEVICE_H_

#include <gmock/gmock.h>

#include <cryptohome/storage/encrypted_container/backing_device.h>

namespace cryptohome {

class FakeBackingDevice : public BackingDevice {
 public:
  FakeBackingDevice(BackingDeviceType type, const base::FilePath& device_path)
      : exists_(false),
        attached_(false),
        type_(type),
        backing_device_path_(device_path) {}

  ~FakeBackingDevice() {}

  bool Create() override {
    if (exists_) {
      return false;
    }
    exists_ = true;
    return true;
  };

  bool Purge() override {
    if (!exists_ || attached_) {
      return false;
    }
    exists_ = false;
    return true;
  }

  bool Setup() override {
    if (!exists_ || attached_) {
      return false;
    }

    attached_ = true;
    return true;
  }

  bool Teardown() override {
    if (!exists_ || !attached_) {
      return false;
    }
    attached_ = false;
    return true;
  }

  BackingDeviceType GetType() override { return type_; }

  base::Optional<base::FilePath> GetPath() override {
    if (!attached_) {
      return base::nullopt;
    }
    return backing_device_path_;
  }

 private:
  bool exists_;
  bool attached_;
  BackingDeviceType type_;
  base::FilePath backing_device_path_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_STORAGE_ENCRYPTED_CONTAINER_FAKE_BACKING_DEVICE_H_
