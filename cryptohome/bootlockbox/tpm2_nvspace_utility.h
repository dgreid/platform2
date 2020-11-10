// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_BOOTLOCKBOX_TPM2_NVSPACE_UTILITY_H_
#define CRYPTOHOME_BOOTLOCKBOX_TPM2_NVSPACE_UTILITY_H_

#include <memory>
#include <string>

#include <openssl/sha.h>

#include <base/threading/thread.h>
#include <tpm_manager/client/tpm_nvram_dbus_proxy.h>
#include <tpm_manager/common/tpm_nvram_interface.h>
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

// Thread name of the thread that communicates with tpm_managerd.
constexpr char kTpmManagerThreadName[] = "tpm_manager_thread";

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
  TPM2NVSpaceUtility(tpm_manager::TpmNvramInterface* tpm_nvram,
                     trunks::TrunksFactory* trunks_factory);
  TPM2NVSpaceUtility(const TPM2NVSpaceUtility&) = delete;
  TPM2NVSpaceUtility& operator=(const TPM2NVSpaceUtility&) = delete;

  ~TPM2NVSpaceUtility() {}

  // Starts tpm_manager_thread_ and initializes tpm_nvram and trunks_factory
  // if necessary. Must be called before issuing and calls to this utility.
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
  // Tpm_manager communication thread class that cleans up after stopping.
  class TpmManagerThread : public base::Thread {
   public:
    explicit TpmManagerThread(TPM2NVSpaceUtility* tpm_utility)
        : base::Thread("tpm_manager_thread"), tpm_utility_(tpm_utility) {
      DCHECK(tpm_utility_);
    }
    TpmManagerThread(const TpmManagerThread&) = delete;
    TpmManagerThread& operator=(const TpmManagerThread&) = delete;

    ~TpmManagerThread() override { Stop(); }

   private:
    void CleanUp() override { tpm_utility_->ShutdownTask(); }

    TPM2NVSpaceUtility* const tpm_utility_;

  };

  void InitializationTask(base::WaitableEvent* completion, bool* result);
  void ShutdownTask();

  // Handles tpm_manager async calls.
  template <typename ReplyProtoType, typename MethodType>
  void SendTpmManagerRequestAndWait(const MethodType& method,
                                    ReplyProtoType* reply_proto);

  // Tpm manager interface that relays relays tpm request to tpm_managerd over
  // DBus. It is used for defining nvspace on the first boot. This object is
  // created in tpm_manager_thread_ and should only be used in
  // tpm_manager_thread_.
  std::unique_ptr<tpm_manager::TpmNvramDBusProxy> default_tpm_nvram_;
  tpm_manager::TpmNvramInterface* tpm_nvram_;
  // A thread that handles async calls to tpm_manager.
  TpmManagerThread tpm_manager_thread_{this};

  // Trunks interface.
  std::unique_ptr<trunks::TrunksFactoryImpl> default_trunks_factory_;
  trunks::TrunksFactory* trunks_factory_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_BOOTLOCKBOX_TPM2_NVSPACE_UTILITY_H_
