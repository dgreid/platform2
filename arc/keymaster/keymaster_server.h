// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMASTER_KEYMASTER_SERVER_H_
#define ARC_KEYMASTER_KEYMASTER_SERVER_H_

#include <vector>

#include <base/macros.h>
#include <base/memory/scoped_refptr.h>
#include <keymaster/android_keymaster.h>
#include <mojo/keymaster.mojom.h>

#include "arc/keymaster/context/arc_keymaster_context.h"

namespace arc {
namespace keymaster {

// KeymasterServer is a Mojo implementation of the Keymaster 3 HIDL interface.
// It fulfills requests by forwarding them to the Android Keymaster.
class KeymasterServer : public arc::mojom::KeymasterServer {
 public:
  explicit KeymasterServer(const scoped_refptr<dbus::Bus>& bus);

  ~KeymasterServer() override = default;

  void SetSystemVersion(uint32_t osVersion, uint32_t osPatchLevel) override;

  void AddRngEntropy(const std::vector<uint8_t>& data,
                     const AddRngEntropyCallback& callback) override;

  void GetKeyCharacteristics(
      ::arc::mojom::GetKeyCharacteristicsRequestPtr request,
      const GetKeyCharacteristicsCallback& callback) override;

  void GenerateKey(std::vector<mojom::KeyParameterPtr> key_params,
                   const GenerateKeyCallback& callback) override;

  void ImportKey(arc::mojom::ImportKeyRequestPtr request,
                 const ImportKeyCallback& callback) override;

  void ExportKey(arc::mojom::ExportKeyRequestPtr request,
                 const ExportKeyCallback& callback) override;

  void AttestKey(arc::mojom::AttestKeyRequestPtr request,
                 const AttestKeyCallback& callback) override;

  void UpgradeKey(arc::mojom::UpgradeKeyRequestPtr request,
                  const UpgradeKeyCallback& callback) override;

  void DeleteKey(const std::vector<uint8_t>& key_blob,
                 const DeleteKeyCallback& callback) override;

  void DeleteAllKeys(const DeleteKeyCallback& callback) override;

  void Begin(arc::mojom::BeginRequestPtr request,
             const BeginCallback& callback) override;

  void Update(arc::mojom::UpdateRequestPtr request,
              const UpdateCallback& callback) override;

  void Finish(arc::mojom::FinishRequestPtr request,
              const FinishCallback& callback) override;

  void Abort(uint64_t operationHandle, const AbortCallback& callback) override;

 private:
  // Owned by |keymaster_|.
  context::ArcKeymasterContext* context_;
  ::keymaster::AndroidKeymaster keymaster_;

  DISALLOW_COPY_AND_ASSIGN(KeymasterServer);
};

}  // namespace keymaster
}  // namespace arc

#endif  // ARC_KEYMASTER_KEYMASTER_SERVER_H_
