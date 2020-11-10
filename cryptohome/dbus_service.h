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
    // We need to cleanup the mount thread dbus, if any.
    base::WaitableEvent on_cleanup_done;
    service_->PostTaskToMountThread(
        FROM_HERE,
        base::Bind(&UserDataAuthDaemon::CleanupMountThreadDBus,
                   base::Unretained(this), base::Unretained(&on_cleanup_done)));
    on_cleanup_done.Wait();

    brillo::DBusServiceDaemon::OnShutdown(exit_code);
  }

  void RegisterDBusObjectsAsync(
      brillo::dbus_utils::AsyncEventSequencer* sequencer) override {
    // Initialize the UserDataAuth service.
    // Note that the initialization should be done after setting the options.
    CHECK(service_->Initialize());

    service_->set_dbus(bus_);

    service_->PostTaskToMountThread(
        FROM_HERE,
        base::Bind(&UserDataAuthDaemon::CreateMountThreadDBus,
                   base::Unretained(this),
                   sequencer->GetHandler("Failed to create dbus connection for "
                                         "UserDataAuth's mount thread",
                                         true)));

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

  // The dbus connection whose origin thread is UserDataAuth's mount thread.
  std::unique_ptr<brillo::DBusConnection> mount_thread_bus_connection_;
  scoped_refptr<::dbus::Bus> mount_thread_bus_;

  // This create a dbus connection whose origin thread is UserDataAuth's mount
  // thread.
  void CreateMountThreadDBus(
      brillo::dbus_utils::AsyncEventSequencer::Handler on_done) {
    // This should be run on UserDataAuth's Mount Thread.
    service_->AssertOnMountThread();

    // This shouldn't be called twice.
    DCHECK(!mount_thread_bus_connection_);
    DCHECK(!mount_thread_bus_);

    // Setup the connection.
    mount_thread_bus_connection_.reset(new brillo::DBusConnection);
    mount_thread_bus_ = mount_thread_bus_connection_->Connect();
    if (!mount_thread_bus_) {
      // Failed to create the mount thread dbus.
      LOG(WARNING) << "Failed to connect to dbus on UserDataAuth mount thread.";

      // Run the handler back on origin thread.
      service_->PostTaskToOriginThread(
          FROM_HERE,
          base::Bind(
              [](brillo::dbus_utils::AsyncEventSequencer::Handler on_done) {
                on_done.Run(false);
              },
              on_done));
      return;
    }

    service_->set_mount_thread_dbus(mount_thread_bus_);

    // Run the handler back on origin thread.
    service_->PostTaskToOriginThread(
        FROM_HERE,
        base::Bind(
            [](brillo::dbus_utils::AsyncEventSequencer::Handler on_done) {
              on_done.Run(true);
            },
            on_done));
  }

  void CleanupMountThreadDBus(base::WaitableEvent* on_done) {
    if (mount_thread_bus_) {
      mount_thread_bus_->ShutdownAndBlock();
      service_->set_mount_thread_dbus(nullptr);
      mount_thread_bus_.reset();
    }
    if (mount_thread_bus_connection_) {
      mount_thread_bus_connection_.reset();
    }
    on_done->Signal();
  }
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_DBUS_SERVICE_H_
