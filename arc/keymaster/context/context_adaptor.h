// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_KEYMASTER_CONTEXT_CONTEXT_ADAPTOR_H_
#define ARC_KEYMASTER_CONTEXT_CONTEXT_ADAPTOR_H_

#include <string>

#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/optional.h>
#include <brillo/secure_blob.h>
#include <chaps/pkcs11/cryptoki.h>
#include <dbus/bus.h>

namespace arc {
namespace keymaster {
namespace context {

// Helper class for general utilities in the context. It serves two main
// purposes:
// * Implement DBus methods to communicate with other daemons.
// * Offer a simple cache for commonly used data so it doesn't have to be
//   fetched multiple times.
class ContextAdaptor {
 public:
  ContextAdaptor();
  // Not copyable nor assignable.
  ContextAdaptor(const ContextAdaptor&) = delete;
  ContextAdaptor& operator=(const ContextAdaptor&) = delete;
  ~ContextAdaptor();

  base::WeakPtr<ContextAdaptor> GetWeakPtr() {
    return weak_ptr_factory_.GetWeakPtr();
  }

  // Returns the slot id of the security token for the primary user, or
  // base::nullopt if there's an error in the DBus call.
  base::Optional<CK_SLOT_ID> FetchPrimaryUserSlot();

  const base::Optional<brillo::SecureBlob>& encryption_key() {
    return cached_encryption_key_;
  }

  void set_encryption_key(const base::Optional<brillo::SecureBlob>& key) {
    cached_encryption_key_ = key;
  }

  void set_slot_for_tests(CK_SLOT_ID slot) { cached_slot_ = slot; }

 private:
  // Returns the email of the primary signed in user, or base::nullopt if
  // there's an error in the DBus call
  base::Optional<std::string> FetchPrimaryUserEmail();

  scoped_refptr<::dbus::Bus> GetBus();

  scoped_refptr<::dbus::Bus> bus_;
  // Initially nullopt, then populated in the corresponding fetch operation.
  base::Optional<CK_SLOT_ID> cached_slot_;
  base::Optional<std::string> cached_email_;
  // Initially nullopt, then populated in the corresponding setter.
  base::Optional<brillo::SecureBlob> cached_encryption_key_;

  // Must be last member to ensure weak pointers are invalidated first.
  base::WeakPtrFactory<ContextAdaptor> weak_ptr_factory_;
};

}  // namespace context
}  // namespace keymaster
}  // namespace arc

#endif  // ARC_KEYMASTER_CONTEXT_CONTEXT_ADAPTOR_H_
