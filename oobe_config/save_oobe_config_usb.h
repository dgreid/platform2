// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_SAVE_OOBE_CONFIG_USB_H_
#define OOBE_CONFIG_SAVE_OOBE_CONFIG_USB_H_

#include <string>

#include <base/files/file_path.h>

namespace oobe_config {

// An object of this class has these responsibilities:
//
// - Sign the oobe config file and write it into
//   unencrypted/oobe_auto_config/config.json.sig on the device.
// - Sign the enrollment domain file if there is any and write it into
//   unencrypted/oobe_auto_config/enrollment_domain.sig on the device.
// - Sign the path to the stateful partition block device in /dev/disk/by-id and
//   write it into unencrypted/oobe_auto_config/usb_device_path.sig.
// - Copy the public key to the device's stateful at
//   unencrypted/oobe_auto_config/validation_key.pub
class SaveOobeConfigUsb {
 public:
  SaveOobeConfigUsb(const base::FilePath& device_stateful_dir,
                    const base::FilePath& usb_stateful_dir,
                    const base::FilePath& device_ids_dir,
                    const base::FilePath& usb_device,
                    const base::FilePath& private_key_file,
                    const base::FilePath& public_key_file);
  SaveOobeConfigUsb(const SaveOobeConfigUsb&) = delete;
  SaveOobeConfigUsb& operator=(const SaveOobeConfigUsb&) = delete;

  virtual ~SaveOobeConfigUsb() = default;

  // Does the main job of signing config and enrollment domain, and copying the
  // public key to the device's stateful partition.
  virtual bool Save() const;

 protected:
  virtual bool SaveInternal() const;

  // Enumerates /dev/disk/by-id/ to find which persistent disk identifier
  // |usb_device_| corresponds to.
  virtual bool FindPersistentMountDevice(base::FilePath* device) const;

  // Clean up whatever file we created on the device's stateful partition.
  virtual void Cleanup() const;

 private:
  friend class SaveOobeConfigUsbTest;

  base::FilePath device_stateful_;
  base::FilePath usb_stateful_;
  base::FilePath device_ids_dir_;
  base::FilePath usb_device_;
  base::FilePath private_key_file_;
  base::FilePath public_key_file_;
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_SAVE_OOBE_CONFIG_USB_H_
