// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/vpn/vpn_driver.h"

#include <string>
#include <vector>

#include <base/strings/string_util.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/accessor_interface.h"
#include "shill/connection.h"
#include "shill/event_dispatcher.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/property_accessor.h"
#include "shill/property_store.h"
#include "shill/store_interface.h"

using std::string;
using std::vector;

namespace shill {

namespace Logging {
static auto kModuleLogScope = ScopeLogger::kVPN;
static string ObjectID(const VPNDriver* v) {
  return "(vpn_driver)";
}
}  // namespace Logging

// TODO(crbug.com/1084279) Migrate back to storing property names after crypto
// code is removed.
const char VPNDriver::kCredentialPrefix[] = "Credential.";

VPNDriver::VPNDriver(Manager* manager,
                     ProcessManager* process_manager,
                     const Property* properties,
                     size_t property_count)
    : manager_(manager),
      process_manager_(process_manager),
      properties_(properties),
      property_count_(property_count),
      connect_timeout_seconds_(0),
      weak_ptr_factory_(this) {}

VPNDriver::~VPNDriver() = default;

bool VPNDriver::Load(const StoreInterface* storage, const string& storage_id) {
  SLOG(this, 2) << __func__;
  for (size_t i = 0; i < property_count_; i++) {
    if ((properties_[i].flags & Property::kEphemeral)) {
      continue;
    }
    const string property = properties_[i].property;
    if (properties_[i].flags & Property::kArray) {
      CHECK(!(properties_[i].flags & Property::kCredential))
          << "Property cannot be both an array and a credential";
      vector<string> value;
      if (storage->GetStringList(storage_id, property, &value)) {
        args_.Set<Strings>(property, value);
      } else {
        args_.Remove(property);
      }
    } else {
      string value;
      bool loaded = (properties_[i].flags & Property::kCredential)
                        ? storage->GetCryptedString(
                              storage_id, property,
                              string(kCredentialPrefix) + property, &value)
                        : storage->GetString(storage_id, property, &value);
      if (loaded) {
        args_.Set<string>(property, value);
      } else {
        args_.Remove(property);
      }
    }
  }
  return true;
}

void VPNDriver::MigrateDeprecatedStorage(StoreInterface* storage,
                                         const string& storage_id) {
  SLOG(this, 2) << __func__;
  // Migrate from ROT47 to plaintext.
  // TODO(crbug.com/1084279) Migrate back to not using kCredentialPrefix once
  // ROT47 migration is complete.
  for (size_t i = 0; i < property_count_; i++) {
    if (!(properties_[i].flags & Property::kCredential)) {
      continue;
    }

    CHECK(!(properties_[i].flags & Property::kArray))
        << "Property cannot be both an array and a credential";
    string deprecated_key = properties_[i].property;
    string credentials_key = string(kCredentialPrefix) + deprecated_key;

    if (storage->DeleteKey(storage_id, deprecated_key)) {
      string value = args_.Get<string>(properties_[i].property);
      storage->SetString(storage_id, credentials_key, value);
    }
  }
}

bool VPNDriver::Save(StoreInterface* storage,
                     const string& storage_id,
                     bool save_credentials) {
  SLOG(this, 2) << __func__;
  for (size_t i = 0; i < property_count_; i++) {
    if ((properties_[i].flags & Property::kEphemeral)) {
      continue;
    }
    bool credential = (properties_[i].flags & Property::kCredential);
    const string property = properties_[i].property;
    if (properties_[i].flags & Property::kArray) {
      CHECK(!credential) << "Property cannot be both an array and a credential";
      if (!args_.Contains<Strings>(property)) {
        storage->DeleteKey(storage_id, property);
        continue;
      }
      Strings value = args_.Get<Strings>(property);
      storage->SetStringList(storage_id, property, value);
    } else {
      string storage_key = property;
      if (credential) {
        storage_key = string(kCredentialPrefix) + storage_key;
      }

      if (!args_.Contains<string>(property) ||
          (credential && !save_credentials)) {
        storage->DeleteKey(storage_id, storage_key);
        continue;
      }
      string value = args_.Get<string>(property);
      storage->SetString(storage_id, storage_key, value);
    }
  }
  return true;
}

void VPNDriver::UnloadCredentials() {
  SLOG(this, 2) << __func__;
  for (size_t i = 0; i < property_count_; i++) {
    if ((properties_[i].flags &
         (Property::kEphemeral | Property::kCredential))) {
      args_.Remove(properties_[i].property);
    }
  }
}

void VPNDriver::InitPropertyStore(PropertyStore* store) {
  SLOG(this, 2) << __func__;
  for (size_t i = 0; i < property_count_; i++) {
    if (properties_[i].flags & Property::kArray) {
      store->RegisterDerivedStrings(
          properties_[i].property,
          StringsAccessor(new CustomMappedAccessor<VPNDriver, Strings, size_t>(
              this, &VPNDriver::ClearMappedStringsProperty,
              &VPNDriver::GetMappedStringsProperty,
              &VPNDriver::SetMappedStringsProperty, i)));
    } else {
      store->RegisterDerivedString(
          properties_[i].property,
          StringAccessor(new CustomMappedAccessor<VPNDriver, string, size_t>(
              this, &VPNDriver::ClearMappedStringProperty,
              &VPNDriver::GetMappedStringProperty,
              &VPNDriver::SetMappedStringProperty, i)));
    }
  }

  store->RegisterDerivedKeyValueStore(
      kProviderProperty,
      KeyValueStoreAccessor(new CustomAccessor<VPNDriver, KeyValueStore>(
          this, &VPNDriver::GetProvider, nullptr)));
}

void VPNDriver::ClearMappedStringProperty(const size_t& index, Error* error) {
  CHECK(index < property_count_);
  if (args_.Contains<string>(properties_[index].property)) {
    args_.Remove(properties_[index].property);
  } else {
    error->Populate(Error::kNotFound, "Property is not set");
  }
}

void VPNDriver::ClearMappedStringsProperty(const size_t& index, Error* error) {
  CHECK(index < property_count_);
  if (args_.Contains<Strings>(properties_[index].property)) {
    args_.Remove(properties_[index].property);
  } else {
    error->Populate(Error::kNotFound, "Property is not set");
  }
}

string VPNDriver::GetMappedStringProperty(const size_t& index, Error* error) {
  // Provider properties are set via SetProperty calls to "Provider.XXX",
  // however, they are retrieved via a GetProperty call, which returns all
  // properties in a single "Provider" dict.  Therefore, none of the individual
  // properties in the kProperties are available for enumeration in
  // GetProperties.  Instead, they are retrieved via GetProvider below.
  error->Populate(Error::kInvalidArguments,
                  "Provider properties are not read back in this manner");
  return string();
}

Strings VPNDriver::GetMappedStringsProperty(const size_t& index, Error* error) {
  // Provider properties are set via SetProperty calls to "Provider.XXX",
  // however, they are retrieved via a GetProperty call, which returns all
  // properties in a single "Provider" dict.  Therefore, none of the individual
  // properties in the kProperties are available for enumeration in
  // GetProperties.  Instead, they are retrieved via GetProvider below.
  error->Populate(Error::kInvalidArguments,
                  "Provider properties are not read back in this manner");
  return Strings();
}

bool VPNDriver::SetMappedStringProperty(const size_t& index,
                                        const string& value,
                                        Error* error) {
  CHECK(index < property_count_);
  if (args_.Contains<string>(properties_[index].property) &&
      args_.Get<string>(properties_[index].property) == value) {
    return false;
  }
  args_.Set<string>(properties_[index].property, value);
  return true;
}

bool VPNDriver::SetMappedStringsProperty(const size_t& index,
                                         const Strings& value,
                                         Error* error) {
  CHECK(index < property_count_);
  if (args_.Contains<Strings>(properties_[index].property) &&
      args_.Get<Strings>(properties_[index].property) == value) {
    return false;
  }
  args_.Set<Strings>(properties_[index].property, value);
  return true;
}

KeyValueStore VPNDriver::GetProvider(Error* error) {
  SLOG(this, 2) << __func__;
  string provider_prefix = string(kProviderProperty) + ".";
  KeyValueStore provider_properties;

  for (size_t i = 0; i < property_count_; i++) {
    if ((properties_[i].flags & Property::kWriteOnly)) {
      continue;
    }
    string prop = properties_[i].property;

    // Chomp off leading "Provider." from properties that have this prefix.
    string chopped_prop;
    if (base::StartsWith(prop, provider_prefix,
                         base::CompareCase::INSENSITIVE_ASCII)) {
      chopped_prop = prop.substr(provider_prefix.length());
    } else {
      chopped_prop = prop;
    }

    if (properties_[i].flags & Property::kArray) {
      if (!args_.Contains<Strings>(prop)) {
        continue;
      }
      Strings value = args_.Get<Strings>(prop);
      provider_properties.Set<Strings>(chopped_prop, value);
    } else {
      if (!args_.Contains<string>(prop)) {
        continue;
      }
      string value = args_.Get<string>(prop);
      provider_properties.Set<string>(chopped_prop, value);
    }
  }

  return provider_properties;
}

void VPNDriver::StartConnectTimeout(int timeout_seconds) {
  if (IsConnectTimeoutStarted()) {
    return;
  }
  LOG(INFO) << "Schedule VPN connect timeout: " << timeout_seconds
            << " seconds.";
  connect_timeout_seconds_ = timeout_seconds;
  connect_timeout_callback_.Reset(
      Bind(&VPNDriver::OnConnectTimeout, weak_ptr_factory_.GetWeakPtr()));
  dispatcher()->PostDelayedTask(FROM_HERE, connect_timeout_callback_.callback(),
                                timeout_seconds * 1000);
}

void VPNDriver::StopConnectTimeout() {
  SLOG(this, 2) << __func__;
  connect_timeout_callback_.Cancel();
  connect_timeout_seconds_ = 0;
}

bool VPNDriver::IsConnectTimeoutStarted() const {
  return !connect_timeout_callback_.IsCancelled();
}

void VPNDriver::OnConnectTimeout() {
  LOG(INFO) << "VPN connect timeout.";
  StopConnectTimeout();
}

void VPNDriver::OnBeforeSuspend(const ResultCallback& callback) {
  // Nothing to be done in the general case, so immediately report success.
  callback.Run(Error(Error::kSuccess));
}

void VPNDriver::OnAfterResume() {}

void VPNDriver::OnDefaultPhysicalServiceEvent(
    DefaultPhysicalServiceEvent event) {}

string VPNDriver::GetHost() const {
  return args_.Lookup<string>(kProviderHostProperty, "");
}

ControlInterface* VPNDriver::control_interface() const {
  return manager_->control_interface();
}

EventDispatcher* VPNDriver::dispatcher() const {
  return manager_->dispatcher();
}

Metrics* VPNDriver::metrics() const {
  return manager_->metrics();
}

}  // namespace shill
