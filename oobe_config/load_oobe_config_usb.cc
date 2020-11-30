// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/load_oobe_config_usb.h"

#include <pwd.h>
#include <sys/mount.h>
#include <unistd.h>

#include <map>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/file_utils.h>
#include <libtpmcrypto/tpm.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include "oobe_config/usb_utils.h"

using base::FilePath;
using base::ScopedTempDir;
using std::map;
using std::string;
using std::unique_ptr;
using std::vector;

namespace oobe_config {

namespace {
#if USE_TPM2
// TPMA_NV_PPWRITE | TPMA_NV_AUTHREAD | TPMA_NV_NO_DA | TPMA_NV_WRITTEN |
// TPMA_NV_PLATFORMCREATE
constexpr uint32_t kTpmPermissions = 0x62040001;
#else
// TPM_NV_PER_PPWRITE
constexpr uint32_t kTpmPermissions = 0x1;
#endif

constexpr uint32_t kHashIndexInTpmNvram = 0x100c;

// Copied from rollback_helper.cc.
// TODO(ahassani): Find a way to use the one in rollback_helper.cc
bool GetUidGid(const string& user, uid_t* uid, gid_t* gid) {
  // Load the passwd entry.
  constexpr int kDefaultPwnameLength = 1024;
  int user_name_length = sysconf(_SC_GETPW_R_SIZE_MAX);
  if (user_name_length == -1) {
    user_name_length = kDefaultPwnameLength;
  }
  if (user_name_length < 0) {
    return false;
  }
  passwd user_info{}, *user_infop;
  vector<char> user_name_buf(static_cast<size_t>(user_name_length));
  getpwnam_r(user.c_str(), &user_info, user_name_buf.data(),
             static_cast<size_t>(user_name_length), &user_infop);
  // NOTE: the return value can be ambiguous in the case that the user does
  // not exist. See "man getpwnam_r" for details.
  if (user_infop == nullptr) {
    return false;
  }

  *uid = user_info.pw_uid;
  *gid = user_info.pw_gid;
  return true;
}

}  // namespace

unique_ptr<LoadOobeConfigUsb> LoadOobeConfigUsb::CreateInstance() {
  return std::make_unique<LoadOobeConfigUsb>(
      FilePath(kStatefulDir), FilePath(kDevDiskById), FilePath(kStoreDir));
}

LoadOobeConfigUsb::LoadOobeConfigUsb(const FilePath& stateful_dir,
                                     const FilePath& device_ids_dir,
                                     const FilePath& store_dir)
    : stateful_(stateful_dir),
      device_ids_dir_(device_ids_dir),
      store_dir_(store_dir),
      public_key_(nullptr) {
  unencrypted_oobe_config_dir_ = stateful_.Append(kUnencryptedOobeConfigDir);
  pub_key_file_ = unencrypted_oobe_config_dir_.Append(kKeyFile);
  config_signature_file_ =
      unencrypted_oobe_config_dir_.Append(kConfigFile).AddExtension("sig");
  enrollment_domain_signature_file_ =
      unencrypted_oobe_config_dir_.Append(kDomainFile).AddExtension("sig");
  usb_device_path_signature_file_ =
      unencrypted_oobe_config_dir_.Append(kUsbDevicePathSigFile);
}

bool LoadOobeConfigUsb::ReadFiles() {
  if (!base::PathExists(stateful_)) {
    LOG(ERROR) << "Stateful partition's path " << stateful_.value()
               << " does not exist.";
    return false;
  }
  if (!base::PathExists(unencrypted_oobe_config_dir_)) {
    LOG(WARNING) << "oobe_config directory on stateful partition "
                 << unencrypted_oobe_config_dir_.value()
                 << " does not exist. This is not an error if the system is not"
                 << " configured for auto oobe.";
    return false;
  }

  if (!base::PathExists(pub_key_file_)) {
    LOG(WARNING) << "Public key file " << pub_key_file_.value()
                 << " does not exist. ";
    return false;
  }

  if (!ReadPublicKey(pub_key_file_, &public_key_)) {
    return false;
  }

  map<FilePath, string*> signature_files = {
      {config_signature_file_, &config_signature_},
      {enrollment_domain_signature_file_, &enrollment_domain_signature_},
      {usb_device_path_signature_file_, &usb_device_path_signature_}};

  for (auto& file : signature_files) {
    if (!base::PathExists(file.first)) {
      LOG(WARNING) << "File " << file.first.value() << " does not exist. ";
      return false;
    }
    if (!base::ReadFileToString(file.first, file.second)) {
      PLOG(ERROR) << "Failed to read file: " << file.first.value();
      return false;
    }
  }

  return true;
}

bool LoadOobeConfigUsb::VerifyPublicKey() {
  std::unique_ptr<tpmcrypto::Tpm> tpm_crypto = tpmcrypto::CreateTpmInstance();

  uint32_t attributes = 0;
  if (!tpm_crypto->GetNVAttributes(kHashIndexInTpmNvram, &attributes)) {
    LOG(ERROR) << "Failed to get NV attributes.";
    return false;
  }
  if (attributes != kTpmPermissions) {
    LOG(ERROR) << "Attributes given (" << attributes
               << ") does not match the expected (" << kTpmPermissions << ").";
    return false;
  }

  string hash_from_tpm;
  if (!tpm_crypto->NVReadNoAuth(kHashIndexInTpmNvram, 0 /* offset */,
                                SHA256_DIGEST_LENGTH, &hash_from_tpm)) {
    LOG(ERROR) << "Failed to read the hash value from TPM.";
    return false;
  }
  if (hash_from_tpm.size() != SHA256_DIGEST_LENGTH) {
    LOG(ERROR) << "NVReadNoAuth() returned data with size "
               << hash_from_tpm.size() << " != " << SHA256_DIGEST_LENGTH;
    return false;
  }

  string public_key_content;
  if (!base::ReadFileToString(pub_key_file_, &public_key_content)) {
    PLOG(ERROR) << "Failed to read the public key: " << pub_key_file_.value();
    return false;
  }

  // Calculating the hash of the public key.
  char hash_from_public_key[SHA256_DIGEST_LENGTH];
  auto address = reinterpret_cast<unsigned char*>(hash_from_public_key);
  if (SHA256(reinterpret_cast<const unsigned char*>(public_key_content.c_str()),
             public_key_content.length(), address) != address) {
    LOG(ERROR) << "openssl returned an invalid hash pointer!";
    return false;
  }

  // Finally, compare the two hashes to see if they are equal.
  if (string(hash_from_public_key, SHA256_DIGEST_LENGTH) != hash_from_tpm) {
    LOG(ERROR) << "Public key hash ("
               << base::HexEncode(hash_from_public_key, SHA256_DIGEST_LENGTH)
               << ") does not match the hash in the TPM ("
               << base::HexEncode(hash_from_tpm.data(), SHA256_DIGEST_LENGTH)
               << ")";
    return false;
  }
  return true;
}

bool LoadOobeConfigUsb::LocateUsbDevice(FilePath* device_id) {
  // /stateful/unencrypted/oobe_auto_config/usb_device_path.sig is the signature
  // of a /dev/disk/by-id path for the root USB device (e.g. /dev/sda). So to
  // obtain which USB we were on pre-reboot:
  // for dev in /dev/disk/by-id/ *:
  //     if dev verifies with usb_device_path.sig with validation_key.pub:
  //       USB block device is at readlink(dev)
  base::FileEnumerator iter(device_ids_dir_, false,
                            base::FileEnumerator::FILES);
  for (auto link = iter.Next(); !link.empty(); link = iter.Next()) {
    if (VerifySignature(link.value(), usb_device_path_signature_,
                        public_key_) &&
        base::NormalizeFilePath(link, device_id)) {
      LOG(INFO) << "Found USB device " << device_id->value();
      return true;
    }
  }

  LOG(ERROR) << "Did not find the USB device. Probably it was taken out?";
  return false;
}

bool LoadOobeConfigUsb::MountUsbDevice(const FilePath& device_path,
                                       const FilePath& mount_point) {
  LOG(INFO) << "Mounting " << device_path.value() << " on "
            << mount_point.value();
  auto kMountFlags = MS_RDONLY | MS_NOEXEC | MS_NOSUID | MS_NODEV;
  if (mount(device_path.value().c_str(), mount_point.value().c_str(), "ext4",
            kMountFlags, nullptr) != 0) {
    PLOG(ERROR) << "Failed to mount " << device_path.value() << "on "
                << mount_point.value();
    return false;
  }

  return true;
}

bool LoadOobeConfigUsb::UnmountUsbDevice(const FilePath& mount_point) {
  LOG(INFO) << "Unmounting " << mount_point.value();
  if (umount(mount_point.value().c_str()) != 0) {
    PLOG(WARNING) << "Failed to unmount " << mount_point.value();
    return false;
  }
  return true;
}

bool LoadOobeConfigUsb::Load() {
  if (!ReadFiles()) {
    return false;
  }

  if (!VerifyPublicKey()) {
    return false;
  }

  // By now we have all the files necessary on the stateful partition. Now we
  // have to look into the USB drives.
  FilePath device_path;
  if (!LocateUsbDevice(&device_path)) {
    return false;
  }

  ScopedTempDir usb_mount_path;
  if (!usb_mount_path.CreateUniqueTempDir() ||
      !MountUsbDevice(device_path, usb_mount_path.GetPath())) {
    return false;
  }

  // /stateful/unencrypted/oobe_auto_config/config.json.sig is the signature of
  // the config.json file on the USB stateful.
  FilePath unencrypted_oobe_config_dir_on_usb =
      usb_mount_path.GetPath().Append(kUnencryptedOobeConfigDir);
  FilePath config_file_on_usb =
      unencrypted_oobe_config_dir_on_usb.Append(kConfigFile);
  if (!base::ReadFileToString(config_file_on_usb, &config_)) {
    LOG(ERROR) << "Failed to read oobe config file: "
               << config_file_on_usb.value();
    return false;
  }
  if (!VerifySignature(config_, config_signature_, public_key_)) {
    return false;
  }

  // /stateful/unencrypted/oobe_auto_config/enrollment_domain.sig is the
  // signature of the enrollment_domain file on the USB stateful.
  FilePath enrollment_domain_file_on_usb =
      unencrypted_oobe_config_dir_on_usb.Append(kDomainFile);
  if (!base::ReadFileToString(enrollment_domain_file_on_usb,
                              &enrollment_domain_)) {
    LOG(ERROR) << "Failed to read enrollment domain file: "
               << enrollment_domain_file_on_usb.value();
    return false;
  }
  if (!VerifySignature(enrollment_domain_, enrollment_domain_signature_,
                       public_key_)) {
    return false;
  }

  // Ignore the failure.
  UnmountUsbDevice(usb_mount_path.GetPath());
  return true;
}

bool LoadOobeConfigUsb::Store() {
  if (!Load()) {
    return false;
  }

  // Find the GID/UID of oobe_config_restore.
  uid_t uid;
  gid_t gid;
  if (!GetUidGid(kOobeConfigRestoreUser, &uid, &gid)) {
    PLOG(ERROR) << "Failed to get the UID/GID for " << kOobeConfigRestoreUser;
    return false;
  }

  map<FilePath, string*> files = {
      {store_dir_.Append(kConfigFile), &config_},
      {store_dir_.Append(kDomainFile), &enrollment_domain_}};

  for (const auto& file : files) {
    if (!brillo::WriteStringToFile(file.first, *file.second)) {
      PLOG(ERROR) << "Failed to write the config to " << file.first.value();
      return false;
    }
    // Change owners to oobe_config_restore.
    if (lchown(file.first.value().c_str(), uid, gid) != 0) {
      PLOG(ERROR) << "Couldn't change ownership of " << file.first.value()
                  << " to " << kOobeConfigRestoreUser;
      return false;
    }
  }

  return true;
}

bool LoadOobeConfigUsb::GetOobeConfigJson(string* config,
                                          string* enrollment_domain) {
  CHECK(config);
  CHECK(enrollment_domain);

  auto config_file = store_dir_.Append(kConfigFile);
  if (!base::ReadFileToString(config_file, config)) {
    PLOG(ERROR) << "Failed to read in the config file: " << config_file.value();
    return false;
  }

  auto enrollment_domain_file = store_dir_.Append(kDomainFile);
  if (!base::ReadFileToString(enrollment_domain_file, enrollment_domain)) {
    PLOG(ERROR) << "Failed to read in the enrollment_domain file: "
                << enrollment_domain_file.value();
    return false;
  }

  return true;
}

void LoadOobeConfigUsb::CleanupFilesOnDevice() {
  if (!base::DirectoryExists(unencrypted_oobe_config_dir_)) {
    return;
  }

  if (!base::DeletePathRecursively(unencrypted_oobe_config_dir_)) {
    LOG(ERROR) << "Failed to delete directory "
               << unencrypted_oobe_config_dir_.value();
  }
}

}  // namespace oobe_config
