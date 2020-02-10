// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "arc/keymaster/context/context_adaptor.h"

#include <memory>

#include <base/logging.h>
#include <brillo/dbus/dbus_object.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/object_proxy.h>
#include <session_manager/dbus-proxies.h>

namespace arc {
namespace keymaster {
namespace context {

ContextAdaptor::ContextAdaptor(const scoped_refptr<::dbus::Bus>& bus)
    : bus_(bus), weak_ptr_factory_(this) {}

ContextAdaptor::~ContextAdaptor() = default;

base::Optional<std::string> ContextAdaptor::FetchPrimaryUserEmail() {
  // Short circuit if the results is already cached.
  if (cached_email_.has_value())
    return cached_email_.value();

  // Prepare output variables.
  std::string user_email;
  std::string sanitized_username;
  brillo::ErrorPtr error;

  // Make dbus call.
  org::chromium::SessionManagerInterfaceProxy session_manager_proxy(bus_);
  if (!session_manager_proxy.RetrievePrimarySession(
          &user_email, &sanitized_username, &error)) {
    std::string error_message = error ? error->GetMessage() : "Unknown error.";
    LOG(INFO) << "Failed to get primary session: " << error_message;
    return base::nullopt;
  }

  // Cache and return result.
  cached_email_ = user_email;
  return user_email;
}

base::Optional<CK_SLOT_ID> ContextAdaptor::FetchPrimaryUserSlot() {
  // Short circuit if the results is already cached.
  if (cached_slot_.has_value())
    return cached_slot_.value();

  // Fetch email of the primary signed in user.
  base::Optional<std::string> user_email = FetchPrimaryUserEmail();
  if (!user_email.has_value())
    return base::nullopt;

  // Create a dbus proxy.
  dbus::ObjectProxy* cryptohome_proxy = bus_->GetObjectProxy(
      cryptohome::kCryptohomeServiceName,
      dbus::ObjectPath(cryptohome::kCryptohomeServicePath));

  // Prepare a dbus method call.
  dbus::MethodCall method_call(
      cryptohome::kCryptohomeInterface,
      cryptohome::kCryptohomePkcs11GetTpmTokenInfoForUser);
  dbus::MessageWriter writer(&method_call);
  writer.AppendString(user_email.value());

  // Make dbus call.
  std::unique_ptr<dbus::Response> response =
      cryptohome_proxy->CallMethodAndBlock(
          &method_call, dbus::ObjectProxy::TIMEOUT_USE_DEFAULT);
  if (!response)
    return base::nullopt;

  // Parse response.
  dbus::MessageReader reader(response.get());
  std::string label;
  std::string user_pin;
  int32_t slot;
  reader.PopString(&label);
  reader.PopString(&user_pin);
  reader.PopInt32(&slot);

  // Cache and return result.
  cached_slot_ = slot;
  return slot;
}

}  // namespace context
}  // namespace keymaster
}  // namespace arc
