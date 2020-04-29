// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_KEY_CHALLENGE_SERVICE_FACTORY_IMPL_H_
#define CRYPTOHOME_KEY_CHALLENGE_SERVICE_FACTORY_IMPL_H_

#include <memory>
#include <string>

#include <brillo/dbus/dbus_connection.h>

#include "cryptohome/key_challenge_service.h"
#include "cryptohome/key_challenge_service_factory.h"

namespace cryptohome {

// Real implementation of the KeyChallengeServiceFactory interface that creates
// instances of KeyChallengeService that talk to the system D-Bus bus.
class KeyChallengeServiceFactoryImpl final : public KeyChallengeServiceFactory {
 public:
  // |system_dbus_connection| is an unowned pointer that must outlive |this|.
  explicit KeyChallengeServiceFactoryImpl(
      brillo::DBusConnection* system_dbus_connection);
  KeyChallengeServiceFactoryImpl(const KeyChallengeServiceFactoryImpl&) =
      delete;
  KeyChallengeServiceFactoryImpl& operator=(
      const KeyChallengeServiceFactoryImpl&) = delete;
  ~KeyChallengeServiceFactoryImpl() override;

  std::unique_ptr<KeyChallengeService> New(
      const std::string& key_delegate_dbus_service_name) override;

 private:
  // Unowned.
  brillo::DBusConnection* const system_dbus_connection_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_KEY_CHALLENGE_SERVICE_FACTORY_IMPL_H_
