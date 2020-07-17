// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cryptohome/key_challenge_service_factory_impl.h"

#include <memory>
#include <string>

#include <base/logging.h>
#include <base/memory/ref_counted.h>
#include <dbus/bus.h>

#include "cryptohome/key_challenge_service_impl.h"

namespace cryptohome {

KeyChallengeServiceFactoryImpl::KeyChallengeServiceFactoryImpl() = default;

KeyChallengeServiceFactoryImpl::~KeyChallengeServiceFactoryImpl() = default;

std::unique_ptr<KeyChallengeService> KeyChallengeServiceFactoryImpl::New(
    scoped_refptr<::dbus::Bus> bus,
    const std::string& key_delegate_dbus_service_name) {
  if (!bus) {
    LOG(ERROR) << "Cannot do challenge-response authentication without system "
                  "D-Bus bus";
    return nullptr;
  }
  return std::make_unique<KeyChallengeServiceImpl>(
      bus, key_delegate_dbus_service_name);
}

}  // namespace cryptohome
