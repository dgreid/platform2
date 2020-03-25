// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMASTER_CONTEXT_ARC_KEYMASTER_CONTEXT_H_
#define ARC_KEYMASTER_CONTEXT_ARC_KEYMASTER_CONTEXT_H_

#include <base/macros.h>
#include <base/memory/scoped_refptr.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <dbus/bus.h>
#include <hardware/keymaster_defs.h>
#include <keymaster/authorization_set.h>
#include <keymaster/contexts/pure_soft_keymaster_context.h>
#include <keymaster/key.h>
#include <keymaster/key_factory.h>
#include <keymaster/UniquePtr.h>

#include "arc/keymaster/context/context_adaptor.h"
#include "arc/keymaster/key_data.pb.h"

namespace arc {
namespace keymaster {
namespace context {

// Defines specific behavior for ARC Keymaster in Chrome OS.
class ArcKeymasterContext : public ::keymaster::PureSoftKeymasterContext {
 public:
  explicit ArcKeymasterContext(const scoped_refptr<::dbus::Bus>& bus);
  ~ArcKeymasterContext() override;
  // Not copyable nor assignable.
  ArcKeymasterContext(const ArcKeymasterContext&) = delete;
  ArcKeymasterContext& operator=(const ArcKeymasterContext&) = delete;

  // PureSoftKeymasterContext overrides.
  keymaster_error_t CreateKeyBlob(
      const ::keymaster::AuthorizationSet& key_description,
      keymaster_key_origin_t origin,
      const ::keymaster::KeymasterKeyBlob& key_material,
      ::keymaster::KeymasterKeyBlob* key_blob,
      ::keymaster::AuthorizationSet* hw_enforced,
      ::keymaster::AuthorizationSet* sw_enforced) const override;
  keymaster_error_t ParseKeyBlob(
      const ::keymaster::KeymasterKeyBlob& key_blob,
      const ::keymaster::AuthorizationSet& additional_params,
      ::keymaster::UniquePtr<::keymaster::Key>* key) const override;
  keymaster_error_t UpgradeKeyBlob(
      const ::keymaster::KeymasterKeyBlob& key_to_upgrade,
      const ::keymaster::AuthorizationSet& upgrade_params,
      ::keymaster::KeymasterKeyBlob* upgraded_key) const override;

 private:
  // Deserialize the given |key_blob| into |key_material| and auth sets.
  keymaster_error_t DeserializeBlob(
      const ::keymaster::KeymasterKeyBlob& key_blob,
      const ::keymaster::AuthorizationSet& hidden,
      ::keymaster::KeymasterKeyBlob* key_material,
      ::keymaster::AuthorizationSet* hw_enforced,
      ::keymaster::AuthorizationSet* sw_enforced) const;

  // Serialize the given key data info the output |key_blob|.
  keymaster_error_t SerializeKeyDataBlob(
      const ::keymaster::KeymasterKeyBlob& key_material,
      const ::keymaster::AuthorizationSet& hidden,
      const ::keymaster::AuthorizationSet& hw_enforced,
      const ::keymaster::AuthorizationSet& sw_enforced,
      ::keymaster::KeymasterKeyBlob* key_blob) const;

  // Deserialize the given |key_blob| into |key_material| and auth sets.
  keymaster_error_t DeserializeKeyDataBlob(
      const ::keymaster::KeymasterKeyBlob& key_blob,
      const ::keymaster::AuthorizationSet& hidden,
      ::keymaster::KeymasterKeyBlob* key_material,
      ::keymaster::AuthorizationSet* hw_enforced,
      ::keymaster::AuthorizationSet* sw_enforced) const;

  // Serializes |key_data| into |key_blob|.
  bool SerializeKeyData(const KeyData& key_data,
                        const ::keymaster::AuthorizationSet& hidden,
                        ::keymaster::KeymasterKeyBlob* key_blob) const;

  // Deserializes the contents of |key_blob| into |key_data|.
  base::Optional<KeyData> DeserializeKeyData(
      const ::keymaster::KeymasterKeyBlob& key_blob,
      const ::keymaster::AuthorizationSet& hidden) const;

  mutable ContextAdaptor context_adaptor_;

  // Friend class for testing.
  friend class ContextTestPeer;
};

namespace internal {

// Expose SerializeAuthorizationSetToBlob for tests.
brillo::Blob TestSerializeAuthorizationSetToBlob(
    const ::keymaster::AuthorizationSet& authorization_set);

}  // namespace internal

}  // namespace context
}  // namespace keymaster
}  // namespace arc

#endif  // ARC_KEYMASTER_CONTEXT_ARC_KEYMASTER_CONTEXT_H_
