/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This tool will attempt to mount or create the encrypted stateful partition,
 * and the various bind mountable subdirectories.
 *
 */
#define _FILE_OFFSET_BITS 64
#define CHROMEOS_ENVIRONMENT

#include <fcntl.h>
#include <sys/time.h>

#include <memory>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/logging.h>
#include <base/strings/string_number_conversions.h>
#include <brillo/flag_helper.h>
#include <brillo/secure_blob.h>
#include <cryptohome/cryptolib.h>
#include <cryptohome/mount_encrypted/encrypted_fs.h>
#include <cryptohome/mount_encrypted/encryption_key.h>
#include <cryptohome/mount_encrypted/mount_encrypted.h>
#include <cryptohome/mount_encrypted/mount_encrypted_metrics.h>
#include <cryptohome/mount_encrypted/tpm.h>
#include <cryptohome/platform.h>
#include <vboot/crossystem.h>
#include <vboot/tlcl.h>

#define PROP_SIZE 64

#if DEBUG_ENABLED
struct timeval tick = {};
struct timeval tick_start = {};
#endif

namespace {
constexpr char kBioCryptoInitPath[] = "/usr/bin/bio_crypto_init";
constexpr char kBioTpmSeedSalt[] = "biod";
constexpr char kBioTpmSeedTmpDir[] = "/run/bio_crypto_init";
constexpr char kBioTpmSeedFile[] = "seed";
static const uid_t kBiodUid = 282;
static const gid_t kBiodGid = 282;

constexpr char kNvramExport[] = "/tmp/lockbox.nvram";
constexpr char kMountEncryptedMetricsPath[] =
    "/run/mount_encrypted/metrics.mount-encrypted";
}  // namespace

static result_code get_system_property(const char* prop,
                                       char* buf,
                                       size_t length) {
  const char* rc;

  rc = VbGetSystemPropertyString(prop, buf, length);
  LOG(INFO) << "Got System Property '" << prop << "': " << (rc ? buf : "FAIL");

  return rc != NULL ? RESULT_SUCCESS : RESULT_FAIL_FATAL;
}

static int has_chromefw(void) {
  static int state = -1;
  char fw[PROP_SIZE];

  /* Cache the state so we don't have to perform the query again. */
  if (state != -1)
    return state;

  if (get_system_property("mainfw_type", fw, sizeof(fw)) != RESULT_SUCCESS)
    state = 0;
  else
    state = strcmp(fw, "nonchrome") != 0;
  return state;
}

// This triggers the live encryption key to be written to disk, encrypted by the
// system key. It is intended to be called by Cryptohome once the TPM is done
// being set up. If the system key is passed as an argument, use it, otherwise
// attempt to query the TPM again.
static result_code finalize_from_cmdline(
    const mount_encrypted::EncryptedFs& encrypted_fs,
    const base::FilePath& rootdir,
    const char* key) {
  // Load the system key.
  brillo::SecureBlob system_key;
  if (!brillo::SecureBlob::HexStringToSecureBlob(std::string(key),
                                                 &system_key) ||
      system_key.size() != DIGEST_LENGTH) {
    LOG(ERROR) << "Failed to parse system key.";
    return RESULT_FAIL_FATAL;
  }

  mount_encrypted::FixedSystemKeyLoader loader(system_key);
  mount_encrypted::EncryptionKey key_manager(&loader, rootdir);
  result_code rc = key_manager.SetTpmSystemKey();
  if (rc != RESULT_SUCCESS) {
    return rc;
  }

  // If there already is an encrypted system key on disk, there is nothing to
  // do. This also covers cases where the system key is not derived from the
  // lockbox space contents (e.g. TPM 2.0 devices, TPM 1.2 devices with
  // encrypted stateful space, factory keys, etc.), for which it is not
  // appropriate to replace the system key. For cases where finalization is
  // unfinished, we clear any stale system keys from disk to make sure we pass
  // the check here.
  if (base::PathExists(key_manager.key_path())) {
    return RESULT_SUCCESS;
  }

  // Load the encryption key.
  brillo::SecureBlob encryption_key = encrypted_fs.GetKey();
  if (encryption_key.empty()) {
    LOG(ERROR) << "Could not get mount encryption key";
    return RESULT_FAIL_FATAL;
  }

  // Persist the encryption key to disk.
  key_manager.PersistEncryptionKey(encryption_key);

  return RESULT_SUCCESS;
}

static result_code report_info(const mount_encrypted::EncryptedFs& encrypted_fs,
                               const base::FilePath& rootdir) {
  mount_encrypted::Tpm tpm;

  printf("TPM: %s\n", tpm.available() ? "yes" : "no");
  if (tpm.available()) {
    bool owned = false;
    printf("TPM Owned: %s\n", tpm.IsOwned(&owned) == RESULT_SUCCESS
                                  ? (owned ? "yes" : "no")
                                  : "fail");
  }
  printf("ChromeOS: %s\n", has_chromefw() ? "yes" : "no");
  printf("TPM2: %s\n", tpm.is_tpm2() ? "yes" : "no");
  if (has_chromefw()) {
    brillo::SecureBlob system_key;
    auto loader = mount_encrypted::SystemKeyLoader::Create(&tpm, rootdir);
    result_code rc = loader->Load(&system_key);
    if (rc != RESULT_SUCCESS) {
      printf("NVRAM: missing.\n");
    } else {
      printf("NVRAM: available.\n");
    }
  } else {
    printf("NVRAM: not present\n");
  }
  // Report info from the encrypted mount.
  encrypted_fs.ReportInfo();

  return RESULT_SUCCESS;
}

// Reads key material from the file |key_material_file|, creates a system key
// using the material, and persists the system key in NVRAM.
//
// This function only supports TPM 2.0 and should be called ONLY for testing
// purposes.
//
// Doesn't take ownership of |platform|.
// Return code indicates if every thing is successful.
static result_code set_system_key(const base::FilePath& rootdir,
                                  const char* key_material_file,
                                  cryptohome::Platform* platform) {
  if (!key_material_file) {
    LOG(ERROR) << "Key material file not provided.";
    return RESULT_FAIL_FATAL;
  }

  mount_encrypted::Tpm tpm;
  if (!tpm.is_tpm2()) {
    LOG(WARNING) << "Custom system key is not supported in TPM 1.2.";
    return RESULT_FAIL_FATAL;
  }

  brillo::SecureBlob key_material;
  if (!platform->ReadFileToSecureBlob(base::FilePath(key_material_file),
                                      &key_material)) {
    LOG(ERROR) << "Failed to read custom system key material from file "
               << key_material_file;
    return RESULT_FAIL_FATAL;
  }

  auto loader = mount_encrypted::SystemKeyLoader::Create(&tpm, rootdir);

  result_code rc = loader->Initialize(key_material, nullptr);
  if (rc != RESULT_SUCCESS) {
    LOG(ERROR) << "Failed to initialize system key NV space contents.";
    return rc;
  }

  rc = loader->Persist();
  if (rc != RESULT_SUCCESS) {
    LOG(ERROR) << "Failed to persist custom system key material in NVRAM.";
    return rc;
  }

  return RESULT_SUCCESS;
}

/* Exports NVRAM contents to tmpfs for use by install attributes */
void nvram_export(const brillo::SecureBlob& contents) {
  int fd;
  LOG(INFO) << "Export NVRAM contents";
  fd = open(kNvramExport, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
  if (fd < 0) {
    PLOG(ERROR) << "open(nvram_export)";
    return;
  }
  if (write(fd, contents.data(), contents.size()) != contents.size()) {
    // Don't leave broken files around
    unlink(kNvramExport);
  }
  close(fd);
}

// Send a secret derived from the system key to the biometric managers, if
// available, via a tmpfs file which will be read by bio_crypto_init.
bool SendSecretToBiodTmpFile(const mount_encrypted::EncryptionKey& key) {
  // If there isn't a bio-sensor, don't bother.
  if (!base::PathExists(base::FilePath(kBioCryptoInitPath))) {
    LOG(INFO) << "There is no biod, so skip sending TPM seed.";
    return true;
  }

  brillo::SecureBlob tpm_seed =
      key.GetDerivedSystemKey(std::string(kBioTpmSeedSalt));
  if (tpm_seed.empty()) {
    LOG(ERROR) << "TPM Seed provided is empty, not writing to tmpfs.";
    return false;
  }

  auto dirname = base::FilePath(kBioTpmSeedTmpDir);
  if (!base::CreateDirectory(dirname)) {
    LOG(ERROR) << "Failed to create dir for TPM seed.";
    return false;
  }

  if (chown(kBioTpmSeedTmpDir, kBiodUid, kBiodGid) == -1) {
    PLOG(ERROR) << "Failed to change ownership of biod tmpfs dir.";
    return false;
  }

  auto filename = dirname.Append(kBioTpmSeedFile);
  if (base::WriteFile(filename, tpm_seed.char_data(), tpm_seed.size()) !=
      tpm_seed.size()) {
    LOG(ERROR) << "Failed to write TPM seed to tmpfs file.";
    return false;
  }

  if (chown(filename.value().c_str(), kBiodUid, kBiodGid) == -1) {
    PLOG(ERROR) << "Failed to change ownership of biod tmpfs file.";
    return false;
  }

  return true;
}

static result_code mount_encrypted_partition(
    mount_encrypted::EncryptedFs* encrypted_fs,
    const base::FilePath& rootdir,
    bool safe_mount) {
  result_code rc;

  mount_encrypted::ScopedMountEncryptedMetricsSingleton scoped_metrics(
      kMountEncryptedMetricsPath);

  // For the mount operation at boot, return RESULT_FAIL_FATAL to trigger
  // chromeos_startup do the stateful wipe.
  rc = encrypted_fs->CheckStates();
  if (rc != RESULT_SUCCESS)
    return rc;

  mount_encrypted::Tpm tpm;
  auto loader = mount_encrypted::SystemKeyLoader::Create(&tpm, rootdir);
  mount_encrypted::EncryptionKey key(loader.get(), rootdir);
  if (has_chromefw() && safe_mount) {
    if (!tpm.available()) {
      // The TPM should be available before we load the system_key.
      LOG(ERROR) << "TPM not available.";
      // We shouldn't continue to load the system_key.
      return RESULT_FAIL_FATAL;
    }
    rc = key.LoadChromeOSSystemKey();
  } else {
    rc = key.SetInsecureFallbackSystemKey();
  }
  mount_encrypted::MountEncryptedMetrics::Get()->ReportSystemKeyStatus(
      key.system_key_status());
  if (rc != RESULT_SUCCESS) {
    return rc;
  }

  rc = key.LoadEncryptionKey();
  mount_encrypted::MountEncryptedMetrics::Get()->ReportEncryptionKeyStatus(
      key.encryption_key_status());
  if (rc != RESULT_SUCCESS) {
    return rc;
  }

  /* Log errors during sending seed to biod, but don't stop execution. */
  if (has_chromefw()) {
    if (!SendSecretToBiodTmpFile(key)) {
      LOG(ERROR) << "Failed to send TPM secret to biod.";
    }
  } else {
    LOG(ERROR) << "Failed to load system key, biod won't get a TPM seed.";
  }

  rc = encrypted_fs->Setup(key.encryption_key(), key.is_fresh());
  if (rc == RESULT_SUCCESS) {
    bool lockbox_valid = false;
    if (loader->CheckLockbox(&lockbox_valid) == RESULT_SUCCESS) {
      mount_encrypted::NvramSpace* lockbox_space = tpm.GetLockboxSpace();
      if (lockbox_valid && lockbox_space->is_valid()) {
        LOG(INFO) << "Lockbox is valid, exporting.";
        nvram_export(lockbox_space->contents());
      }
    } else {
      LOG(ERROR) << "Lockbox validity check error.";
    }
  }

  LOG(INFO) << "Done.";

  // Continue boot.
  return rc;
}

static void print_usage(const char process_name[]) {
  fprintf(stderr, "Usage: %s [info|finalize|umount|set|mount]\n", process_name);
}

int main(int argc, const char* argv[]) {
  DEFINE_bool(unsafe, false, "mount encrypt partition with well known secret.");
  brillo::FlagHelper::Init(argc, argv, "mount-encrypted");

  auto commandline = base::CommandLine::ForCurrentProcess();
  auto args = commandline->GetArgs();

  char* rootdir_env = getenv("MOUNT_ENCRYPTED_ROOT");
  base::FilePath rootdir = base::FilePath(rootdir_env ? rootdir_env : "/");
  cryptohome::Platform platform;
  brillo::LoopDeviceManager loopdev_manager;
  brillo::DeviceMapper device_mapper;
  mount_encrypted::EncryptedFs encrypted_fs(rootdir, &platform,
                                            &loopdev_manager, &device_mapper);

  LOG(INFO) << "Starting.";

  if (args.size() >= 1) {
    if (args[0] == "umount") {
      return encrypted_fs.Teardown();
    } else if (args[0] == "info") {
      // Report info from the encrypted mount.
      return report_info(encrypted_fs, rootdir);
    } else if (args[0] == "finalize") {
      return finalize_from_cmdline(encrypted_fs, rootdir,
                                   args.size() >= 2 ? args[1].c_str() : NULL);
    } else if (args[0] == "set") {
      return set_system_key(rootdir, args.size() >= 2 ? args[1].c_str() : NULL,
                            &platform);
    } else if (args[0] == "mount") {
      return mount_encrypted_partition(&encrypted_fs, rootdir, !FLAGS_unsafe);
    } else {
      print_usage(argv[0]);
      return RESULT_FAIL_FATAL;
    }
  }

  // default operation is mount encrypted partition.
  return mount_encrypted_partition(&encrypted_fs, rootdir, !FLAGS_unsafe);
}
