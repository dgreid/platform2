// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_LOAD_OOBE_CONFIG_USB_H_
#define OOBE_CONFIG_LOAD_OOBE_CONFIG_USB_H_

#include "oobe_config/load_oobe_config_interface.h"

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <crypto/scoped_openssl_types.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

namespace oobe_config {

// An object of this class has the responsibility of loading the oobe config
// file from usb along with the enrollment domain.
class LoadOobeConfigUsb : public LoadOobeConfigInterface {
 public:
  LoadOobeConfigUsb(const base::FilePath& stateful_dir,
                    const base::FilePath& device_ids_dir,
                    const base::FilePath& store_dir);
  LoadOobeConfigUsb(const LoadOobeConfigUsb&) = delete;
  LoadOobeConfigUsb& operator=(const LoadOobeConfigUsb&) = delete;

  ~LoadOobeConfigUsb() = default;

  bool GetOobeConfigJson(std::string* config,
                         std::string* enrollment_domain) override;

  // Store the loaded configs into /var/lib/oobe_config_restore.
  virtual bool Store();

  // Cleans up the oobe_auto_config directory on the device if it exists.
  virtual void CleanupFilesOnDevice();

  // Creates an instance of this object with default paths to stateful partition
  // and device ids;
  static std::unique_ptr<LoadOobeConfigUsb> CreateInstance();

 protected:
  // Loads the configs from USB drive, stateful, etc.
  virtual bool Load();

  // Checks and reads in all the files necessary for USB enrollment.
  virtual bool ReadFiles();

  // Verifies the hash of the public key on the stateful partition matches the
  // one in the TPM.
  virtual bool VerifyPublicKey();

  // Locates the USB device using the device path's signature file.
  virtual bool LocateUsbDevice(base::FilePath* device_id);

  // Mounts the stateful partition of the discovered USB device.
  virtual bool MountUsbDevice(const base::FilePath& device_path,
                              const base::FilePath& mount_point);

  // Unmounts the USB device.
  virtual bool UnmountUsbDevice(const base::FilePath& mount_point);

 private:
  friend class LoadOobeConfigUsbTest;
  FRIEND_TEST(LoadOobeConfigUsbTest, Simple);

  base::FilePath stateful_;
  base::FilePath unencrypted_oobe_config_dir_;
  base::FilePath pub_key_file_;
  base::FilePath config_signature_file_;
  base::FilePath enrollment_domain_signature_file_;
  base::FilePath usb_device_path_signature_file_;
  base::FilePath device_ids_dir_;
  base::FilePath store_dir_;

  crypto::ScopedEVP_PKEY public_key_;
  std::string config_signature_;
  std::string enrollment_domain_signature_;
  std::string usb_device_path_signature_;

  bool config_is_verified_;
  std::string config_;
  std::string enrollment_domain_;
};

}  // namespace oobe_config

#endif  // OOBE_CONFIG_LOAD_OOBE_CONFIG_USB_H_
