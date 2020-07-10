// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include <base/synchronization/waitable_event.h>
#include <base/message_loop/message_pump_type.h>
#include <base/threading/thread.h>
#include <brillo/dbus/dbus_connection.h>
#include <brillo/dbus/introspectable_helper.h>
#include <dbus/cryptohome/dbus-constants.h>
#include <dbus/tpm_manager/dbus-constants.h>

#include "cryptohome/proxy/dbus_proxy_service.h"

namespace cryptohome {

using brillo::dbus_utils::AsyncEventSequencer;

constexpr char kBlockerThreadName[] = "BlockerDBusThread";

class ServiceBlocker {
 public:
  ServiceBlocker()
      : dbus_thread_(kBlockerThreadName),
        cryptohome_online(base::WaitableEvent::ResetPolicy::MANUAL,
                          base::WaitableEvent::InitialState::NOT_SIGNALED),
        tpm_manager_online(base::WaitableEvent::ResetPolicy::MANUAL,
                           base::WaitableEvent::InitialState::NOT_SIGNALED) {}

  // Calling this will block until both cryptohome and tpm_manager is online.
  // This should be called from the caller's origin thread.
  // Note that the reason why we'll need to wait for both cryptohome and
  // tpm_manager is because some users of cryptohome's legacy API expects all
  // cryptohome APIs are available when any one of them is available, but that
  // is not the case with cryptohome-proxy, whereby some APIs handled by
  // tpm_manager could be available earlier than those handled by
  // cryptohome/UserDataAuth.
  void BlockUntilDestinationIsOnline() {
    // Start the dbus thread. Note that this will need to be an I/O thread
    // because ListenForServiceOwnerChange() needs it.
    base::Thread::Options options;
    options.message_loop_type = base::MessagePumpType::IO;
    dbus_thread_.StartWithOptions(options);

    dbus_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&ServiceBlocker::SetupBlockUntilDestinationIsOnline,
                   base::Unretained(this)));

    cryptohome_online.Wait();
    tpm_manager_online.Wait();

    dbus_thread_.task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&ServiceBlocker::Cleanup, base::Unretained(this)));
    dbus_thread_.Stop();
  }

 private:
  // The thread on which we'll establish the dbus connection and wait for
  // services to be online.
  base::Thread dbus_thread_;

  // These events will be signaled once cryptohome/tpm_manager is online.
  base::WaitableEvent cryptohome_online;
  base::WaitableEvent tpm_manager_online;

  // We need to keep these in order to unlisten/unregister the events.
  ::dbus::Bus::GetServiceOwnerCallback on_cryptohome_online_;
  ::dbus::Bus::GetServiceOwnerCallback on_tpm_manager_online_;

  // The separate dbus connection that we'll use to monitor the service status.
  scoped_refptr<::dbus::Bus> bus_;
  std::unique_ptr<brillo::DBusConnection> bus_connection_;

  // This setup the callbacks that listen for service owner change. This is
  // should be called on this class's dbus_thread_
  void SetupBlockUntilDestinationIsOnline() {
    // Note that the reason why another MessageLoop/DBus connection is needed
    // because we are currently blocking the other (original) connection's dbus
    // thread, and thus we'll not be able to wait for services to come online as
    // no messages are delivered there while blocked.

    // Create another connection to DBus.
    bus_connection_.reset(new brillo::DBusConnection);
    bus_ = bus_connection_->Connect();
    CHECK(bus_);

    on_cryptohome_online_ = base::Bind(
        &ServiceBlocker::OnCryptohomeServiceChange, base::Unretained(this));
    on_tpm_manager_online_ = base::Bind(
        &ServiceBlocker::OnTpmManagerServiceChange, base::Unretained(this));

    // Setup the callbacks.
    bus_->ListenForServiceOwnerChange(kUserDataAuthServiceName,
                                      on_cryptohome_online_);

    bus_->GetServiceOwner(kUserDataAuthServiceName, on_cryptohome_online_);

    bus_->ListenForServiceOwnerChange(tpm_manager::kTpmManagerServiceName,
                                      on_tpm_manager_online_);
    bus_->GetServiceOwner(tpm_manager::kTpmManagerServiceName,
                          on_tpm_manager_online_);
  }

  // This is called by dbus when cryptohome's owner changes.
  void OnCryptohomeServiceChange(const std::string& service_owner) {
    if (!service_owner.empty()) {
      bus_->UnlistenForServiceOwnerChange(kUserDataAuthServiceName,
                                          on_cryptohome_online_);
      cryptohome_online.Signal();
    }
  }

  // This is called by dbus when cryptohome's owner changes.
  void OnTpmManagerServiceChange(const std::string& service_owner) {
    if (!service_owner.empty()) {
      bus_->UnlistenForServiceOwnerChange(tpm_manager::kTpmManagerServiceName,
                                          on_tpm_manager_online_);
      tpm_manager_online.Signal();
    }
  }

  void Cleanup() {
    // Shutdown dbus.
    bus_->ShutdownAndBlock();

    // Destructor of bus_ needs to run on dbus thread.
    bus_connection_.reset();
    bus_ = nullptr;
  }
};

CryptohomeProxyService::CryptohomeProxyService(scoped_refptr<dbus::Bus> bus)
    : bus_(bus) {}

void CryptohomeProxyService::OnInit() {
  scoped_refptr<AsyncEventSequencer> sequencer(new AsyncEventSequencer());

  DCHECK(!dbus_object_);
  dbus_object_ = std::make_unique<brillo::dbus_utils::DBusObject>(
      nullptr, bus_, dbus::ObjectPath(kCryptohomeServicePath));

  adaptor_.reset(
      new LegacyCryptohomeInterfaceAdaptor(bus_, dbus_object_.get()));
  adaptor_->RegisterAsync();

  brillo::dbus_utils::IntrospectableInterfaceHelper introspection;
  introspection.AddInterfaceXml(adaptor_->GetIntrospectionXml());
  introspection.RegisterWithDBusObject(dbus_object_.get());

  dbus_object_->RegisterAsync(
      sequencer->GetHandler("RegisterAsync() failed", true));

  sequencer->OnAllTasksCompletedCall({base::Bind(
      &CryptohomeProxyService::TakeServiceOwnership, base::Unretained(this))});
}

void CryptohomeProxyService::TakeServiceOwnership(bool success) {
  CHECK(success) << "Init of one or more DBus objects has failed.";
  CHECK(bus_->RequestOwnershipAndBlock(kCryptohomeServiceName,
                                       dbus::Bus::REQUIRE_PRIMARY))
      << "Unable to take ownership of " << kCryptohomeServiceName;
  // Note that since we've RequestOwnershipAndBlock(), the cryptohome's DBus
  // service is now online, and all incoming requests will be queued on current
  // thread's MessageLoop. However, it is possible for either tpm_manager or
  // cryptohome to be still initializing, so we'll now use the ServiceBlocker to
  // wait until they are both online. The
  // ServiceBlocker::BlockUntilDestinationIsOnline() will block the original
  // thread, thus causing all incoming dbus method calls to be blocked. They'll
  // be unblocked once both services are up, and then it'll be forwarded and
  // successfully serviced, as opposed to forwarding them when the services are
  // still initializing, causing an error.

  // Once the service is online, wait for our destination service (cryptohome
  // and tpm_manager) to be online.
  ServiceBlocker blocker;
  blocker.BlockUntilDestinationIsOnline();
}

}  // namespace cryptohome
