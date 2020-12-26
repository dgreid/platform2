// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_DBUS_SERVICE_H_
#define CRYPTOHOME_DBUS_SERVICE_H_

#include <memory>

#include <brillo/daemons/dbus_daemon.h>

#include "cryptohome/service_userdataauth.h"
#include "cryptohome/userdataauth.h"

namespace cryptohome {

class UserDataAuthDaemon : public brillo::DBusServiceDaemon {
 public:
  UserDataAuthDaemon()
      : DBusServiceDaemon(kUserDataAuthServiceName),
        service_(new cryptohome::UserDataAuth()) {}
  UserDataAuthDaemon(const UserDataAuthDaemon&) = delete;
  UserDataAuthDaemon& operator=(const UserDataAuthDaemon&) = delete;

  // Retrieve the UserDataAuth object, it holds the service's state and provides
  // a good chunk of functionality.
  UserDataAuth* GetUserDataAuth() { return service_.get(); }

 protected:
  void OnShutdown(int* exit_code) override {
    brillo::DBusServiceDaemon::OnShutdown(exit_code);
  }

  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    // Initialize the UserDataAuth service.
    // Note that the initialization should be done after setting the options.
    CHECK(service_->Initialize());

    service_->set_dbus(bus_);

    DCHECK(!dbus_object_);
    dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
        nullptr, bus_, dbus::ObjectPath(kUserDataAuthServicePath));

    userdataauth_adaptor_.reset(
        new UserDataAuthAdaptor(bus_, dbus_object_.get(), service_.get()));
    userdataauth_adaptor_->RegisterAsync();

    arc_quota_adaptor_.reset(
        new ArcQuotaAdaptor(bus_, dbus_object_.get(), service_.get()));
    arc_quota_adaptor_->RegisterAsync();

    pkcs11_adaptor_.reset(
        new Pkcs11Adaptor(bus_, dbus_object_.get(), service_.get()));
    pkcs11_adaptor_->RegisterAsync();

    install_attributes_adaptor_.reset(
        new InstallAttributesAdaptor(bus_, dbus_object_.get(), service_.get()));
    install_attributes_adaptor_->RegisterAsync();

    misc_adaptor_.reset(
        new CryptohomeMiscAdaptor(bus_, dbus_object_.get(), service_.get()));
    misc_adaptor_->RegisterAsync();

    service_->PostDBusInitialize();

    dbus_object_->RegisterAsync(
        sequencer->GetHandler("RegisterAsync() for UserDataAuth failed", true));
  }

 private:
  std::unique_ptr<UserDataAuthAdaptor> userdataauth_adaptor_;
  std::unique_ptr<ArcQuotaAdaptor> arc_quota_adaptor_;
  std::unique_ptr<Pkcs11Adaptor> pkcs11_adaptor_;
  std::unique_ptr<InstallAttributesAdaptor> install_attributes_adaptor_;
  std::unique_ptr<CryptohomeMiscAdaptor> misc_adaptor_;

  std::unique_ptr<UserDataAuth> service_;

  std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object_;
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_DBUS_SERVICE_H_
