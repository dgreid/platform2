// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_BOOTLOCKBOX_TPM2_NVSPACE_UTILITY_H_
#define CRYPTOHOME_BOOTLOCKBOX_TPM2_NVSPACE_UTILITY_H_

#include <memory>
#include <string>

#include <openssl/sha.h>

#include <tpm_manager/proto_bindings/tpm_manager.pb.h>
#include <tpm_manager-client/tpm_manager/dbus-proxies.h>
#include <trunks/tpm_constants.h>
#include <trunks/tpm_utility.h>
#include <trunks/trunks_factory_impl.h>

#include "cryptohome/bootlockbox/tpm_nvspace_interface.h"

namespace cryptohome {

struct BootLockboxNVSpace {
  uint16_t version;
  uint16_t flags;
  uint8_t digest[SHA256_DIGEST_LENGTH];
} __attribute__((packed));
constexpr uint8_t kNVSpaceVersion = 1;
constexpr uint32_t kNVSpaceSize = sizeof(BootLockboxNVSpace);

// The index of the nv space for bootlockboxd. Refer to README.lockbox
// for how the index is selected.
constexpr uint32_t kBootLockboxNVRamIndex = 0x800006;

// Empty password is used for bootlockbox nvspace. Confidentiality
// is not required and the nvspace is write locked after user logs in.
constexpr char kWellKnownPassword[] = "";

// This class handles tpm operations to read, write, lock and define nv spaces.
// DefineNVSpace is implemented using tpm_managerd to avoid blocking cryptohome
// from starting for the first time boot. An alternative interface to define
// nv sapce via trunks is also provided and must be called before tpm_mangerd
// starts. ReadNVSpace is implemented using trunksd for better reading
// performance.
// Usage:
//   auto nvspace_utility = TPM2NVSpaceUtility();
//   nvspace_utility.Initialize();
//   nvspace_utility.WriteNVSpace(...);
class TPM2NVSpaceUtility : public TPMNVSpaceUtilityInterface {
 public:
  TPM2NVSpaceUtility() = default;

  // Constructor that does not take ownership of tpm_nvram and trunks_factory.
  TPM2NVSpaceUtility(org::chromium::TpmNvramProxyInterface* tpm_nvram,
                     trunks::TrunksFactory* trunks_factory);
  TPM2NVSpaceUtility(const TPM2NVSpaceUtility&) = delete;
  TPM2NVSpaceUtility& operator=(const TPM2NVSpaceUtility&) = delete;

  ~TPM2NVSpaceUtility() {}

  // Initializes tpm_nvram if necessary.
  // Must be called before issuing and calls to this utility.
  bool Initialize() override;

  // This method defines a non-volatile storage area in TPM for bootlockboxd
  // via tpm_managerd.
  bool DefineNVSpace() override;

  // Defines nvspace via trunksd. This function must be called before
  // tpm_managerd starts.
  bool DefineNVSpaceBeforeOwned() override;

  // This method writes |digest| to nvram space for bootlockboxd.
  bool WriteNVSpace(const std::string& digest) override;

  // Reads nvspace and extract |digest|.
  bool ReadNVSpace(std::string* digest, NVSpaceState* state) override;

  // Locks the bootlockbox nvspace for writing.
  bool LockNVSpace() override;

 private:
  scoped_refptr<dbus::Bus> bus_;

  // Tpm manager interface that relays relays tpm request to tpm_managerd over
  // DBus. It is used for defining nvspace on the first boot. This object is
  // created in Initialize and should only be used in the same thread.
  std::unique_ptr<org::chromium::TpmNvramProxyInterface> default_tpm_nvram_;
  org::chromium::TpmNvramProxyInterface* tpm_nvram_;

  // Trunks interface.
  std::unique_ptr<trunks::TrunksFactoryImpl> default_trunks_factory_;
  trunks::TrunksFactory* trunks_factory_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_BOOTLOCKBOX_TPM2_NVSPACE_UTILITY_H_
