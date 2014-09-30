// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/profile.h"

#include <set>
#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/stl_util.h>
#include <base/strings/string_split.h>
#include <base/strings/string_util.h>
#include <base/strings/stringprintf.h>
#include <chromeos/dbus/service_constants.h>

#include "shill/adaptor_interfaces.h"
#include "shill/control_interface.h"
#include "shill/key_file_store.h"
#include "shill/logging.h"
#include "shill/manager.h"
#include "shill/property_accessor.h"
#include "shill/service.h"
#include "shill/store_interface.h"
#include "shill/stub_storage.h"

using base::FilePath;
using std::set;
using std::string;
using std::vector;

namespace shill {

// static
const char Profile::kUserProfileListPathname[] =
    RUNDIR "/loaded_profile_list";

Profile::Profile(ControlInterface *control_interface,
                 Metrics *metrics,
                 Manager *manager,
                 const Identifier &name,
                 const string &user_storage_directory,
                 bool connect_to_rpc)
    : metrics_(metrics),
      manager_(manager),
      name_(name),
      storage_path_(user_storage_directory) {
  if (connect_to_rpc)
    adaptor_.reset(control_interface->CreateProfileAdaptor(this));

  // kCheckPortalListProperty: Registered in DefaultProfile
  // kCountryProperty: Registered in DefaultProfile
  store_.RegisterConstString(kNameProperty, &name_.identifier);
  store_.RegisterConstString(kUserHashProperty, &name_.user_hash);

  // kOfflineModeProperty: Registered in DefaultProfile
  // kPortalURLProperty: Registered in DefaultProfile

  HelpRegisterConstDerivedStrings(kServicesProperty,
                                  &Profile::EnumerateAvailableServices);
  HelpRegisterConstDerivedStrings(kEntriesProperty, &Profile::EnumerateEntries);
}

Profile::~Profile() {}

bool Profile::InitStorage(GLib *glib, InitStorageOption storage_option,
                          Error *error) {
  FilePath final_path;
  if (!GetStoragePath(&final_path)) {
    Error::PopulateAndLog(error, Error::kInvalidArguments,
        base::StringPrintf("Could not set up profile storage for %s:%s",
                           name_.user.c_str(), name_.identifier.c_str()));
    return false;
  }
  scoped_ptr<KeyFileStore> storage(new KeyFileStore(glib));
  storage->set_path(final_path);
  bool already_exists = storage->IsNonEmpty();
  if (!already_exists && storage_option != kCreateNew &&
      storage_option != kCreateOrOpenExisting) {
    Error::PopulateAndLog(error, Error::kNotFound,
        base::StringPrintf("Profile storage for %s:%s does not already exist",
                           name_.user.c_str(), name_.identifier.c_str()));
    return false;
  } else if (already_exists && storage_option != kOpenExisting &&
             storage_option != kCreateOrOpenExisting) {
    Error::PopulateAndLog(error, Error::kAlreadyExists,
        base::StringPrintf("Profile storage for %s:%s already exists",
                           name_.user.c_str(), name_.identifier.c_str()));
    return false;
  }
  if (!storage->Open()) {
    Error::PopulateAndLog(error, Error::kInternalError,
        base::StringPrintf("Could not open profile storage for %s:%s",
                           name_.user.c_str(), name_.identifier.c_str()));
    if (already_exists) {
      // The profile contents are corrupt, or we do not have access to
      // this file.  Move this file out of the way so a future open attempt
      // will succeed, assuming the failure reason was the former.
      storage->MarkAsCorrupted();
      metrics_->NotifyCorruptedProfile();
    }
    return false;
  }
  if (!already_exists) {
    // Add a descriptive header to the profile so even if nothing is stored
    // to it, it still has some content.  Completely empty keyfiles are not
    // valid for reading.
    storage->SetHeader(
        base::StringPrintf("Profile %s:%s", name_.user.c_str(),
                           name_.identifier.c_str()));
  }
  set_storage(storage.release());
  manager_->OnProfileStorageInitialized(this);
  return true;
}

void Profile::InitStubStorage() {
  set_storage(new StubStorage());
}

bool Profile::RemoveStorage(GLib *glib, Error *error) {
  FilePath path;

  CHECK(!storage_.get());

  if (!GetStoragePath(&path)) {
    Error::PopulateAndLog(
        error, Error::kInvalidArguments,
        base::StringPrintf("Could get the storage path for %s:%s",
                           name_.user.c_str(), name_.identifier.c_str()));
    return false;
  }

  if (!base::DeleteFile(path, false)) {
    Error::PopulateAndLog(
        error, Error::kOperationFailed,
        base::StringPrintf("Could not remove path %s", path.value().c_str()));
    return false;
  }

  return true;
}

string Profile::GetFriendlyName() {
  return (name_.user.empty() ? "" : name_.user + "/") + name_.identifier;
}

string Profile::GetRpcIdentifier() {
  if (!adaptor_.get()) {
    // NB: This condition happens in unit tests.
    return string();
  }
  return adaptor_->GetRpcIdentifier();
}

void Profile::set_storage(StoreInterface *storage) {
  storage_.reset(storage);
}

bool Profile::AdoptService(const ServiceRefPtr &service) {
  if (service->profile() == this) {
    return false;
  }
  service->SetProfile(this);
  return service->Save(storage_.get()) && storage_->Flush();
}

bool Profile::AbandonService(const ServiceRefPtr &service) {
  if (service->profile() == this)
    service->SetProfile(NULL);
  return storage_->DeleteGroup(service->GetStorageIdentifier()) &&
      storage_->Flush();
}

bool Profile::UpdateService(const ServiceRefPtr &service) {
  return service->Save(storage_.get()) && storage_->Flush();
}

bool Profile::LoadService(const ServiceRefPtr &service) {
  if (!ContainsService(service))
    return false;
  return service->Load(storage_.get());
}

bool Profile::ConfigureService(const ServiceRefPtr &service) {
  if (!LoadService(service))
    return false;
  service->SetProfile(this);
  return true;
}

bool Profile::ConfigureDevice(const DeviceRefPtr &device) {
  return device->Load(storage_.get());
}

bool Profile::ContainsService(const ServiceConstRefPtr &service) {
  return service->IsLoadableFrom(*storage_.get());
}

void Profile::DeleteEntry(const std::string &entry_name, Error *error) {
  if (!storage_->ContainsGroup(entry_name)) {
    Error::PopulateAndLog(error, Error::kNotFound,
        base::StringPrintf("Entry %s does not exist in profile",
                           entry_name.c_str()));
    return;
  }
  if (!manager_->HandleProfileEntryDeletion(this, entry_name)) {
    // If HandleProfileEntryDeletion() returns succeeds, DeleteGroup()
    // has already been called when AbandonService was called.
    // Otherwise, we need to delete the group ourselves.
    storage_->DeleteGroup(entry_name);
  }
  Save();
}

ServiceRefPtr Profile::GetServiceFromEntry(const std::string &entry_name,
                                           Error *error) {
  return manager_->GetServiceWithStorageIdentifier(this, entry_name, error);
}

bool Profile::IsValidIdentifierToken(const string &token) {
  if (token.empty()) {
    return false;
  }
  for (auto chr : token) {
    if (!IsAsciiAlpha(chr) && !IsAsciiDigit(chr)) {
      return false;
    }
  }
  return true;
}

// static
bool Profile::ParseIdentifier(const string &raw, Identifier *parsed) {
  if (raw.empty()) {
    return false;
  }
  if (raw[0] == '~') {
    // Format: "~user/identifier".
    size_t slash = raw.find('/');
    if (slash == string::npos) {
      return false;
    }
    string user(raw.begin() + 1, raw.begin() + slash);
    string identifier(raw.begin() + slash + 1, raw.end());
    if (!IsValidIdentifierToken(user) || !IsValidIdentifierToken(identifier)) {
      return false;
    }
    parsed->user = user;
    parsed->identifier = identifier;
    return true;
  }

  // Format: "identifier".
  if (!IsValidIdentifierToken(raw)) {
    return false;
  }
  parsed->user = "";
  parsed->identifier = raw;
  return true;
}

// static
string Profile::IdentifierToString(const Identifier &name) {
  if (name.user.empty()) {
    // Format: "identifier".
    return name.identifier;
  }

  // Format: "~user/identifier".
  return base::StringPrintf(
      "~%s/%s", name.user.c_str(), name.identifier.c_str());
}

// static
vector<Profile::Identifier> Profile::LoadUserProfileList(const FilePath &path) {
  vector<Identifier> profile_identifiers;
  string profile_data;
  if (!base::ReadFileToString(path, &profile_data)) {
    return profile_identifiers;
  }

  vector<string> profile_lines;
  base::SplitStringDontTrim(profile_data, '\n', &profile_lines);
  for (const auto &line : profile_lines) {
    if (line.empty()) {
      // This will be the case on the last line, so let's not complain about it.
      continue;
    }
    size_t space = line.find(' ');
    if (space == string::npos || space == 0) {
      LOG(ERROR) << "Invalid line found in " << path.value()
                 << ": " << line;
      continue;
    }
    string name(line.begin(), line.begin() + space);
    Identifier identifier;
    if (!ParseIdentifier(name, &identifier) || identifier.user.empty()) {
      LOG(ERROR) << "Invalid profile name found in " << path.value()
                 << ": " << name;
      continue;
    }
    identifier.user_hash = string(line.begin() + space + 1, line.end());
    profile_identifiers.push_back(identifier);
  }

  return profile_identifiers;
}

// static
bool Profile::SaveUserProfileList(const FilePath &path,
                                  const vector<ProfileRefPtr> &profiles) {
  vector<string> lines;
  for (const auto &profile : profiles) {
    Identifier &id = profile->name_;
    if (id.user.empty()) {
      continue;
    }
    lines.push_back(base::StringPrintf("%s %s\n",
                                       IdentifierToString(id).c_str(),
                                       id.user_hash.c_str()));
  }
  string content = JoinString(lines, "");
  size_t ret = base::WriteFile(path, content.c_str(), content.length());
  return ret == content.length();
}

bool Profile::MatchesIdentifier(const Identifier &name) const {
  return name.user == name_.user && name.identifier == name_.identifier;
}

bool Profile::Save() {
  return storage_->Flush();
}

bool Profile::GetStoragePath(FilePath *path) {
  if (name_.user.empty()) {
    LOG(ERROR) << "Non-default profiles cannot be stored globally.";
    return false;
  }
  // TODO(petkov): Validate the directory permissions, etc.
  *path = storage_path_.Append(
      base::StringPrintf("%s/%s.profile", name_.user.c_str(),
                         name_.identifier.c_str()));
  return true;
}

vector<string> Profile::EnumerateAvailableServices(Error *error) {
  // We should return the Manager's service list if this is the active profile.
  if (manager_->IsActiveProfile(this)) {
    return manager_->EnumerateAvailableServices(error);
  } else {
    return vector<string>();
  }
}

vector<string> Profile::EnumerateEntries(Error */*error*/) {
  vector<string> service_groups;

  // Filter this list down to only entries that correspond
  // to a technology.  (wifi_*, etc)
  for (const auto &group : storage_->GetGroups()) {
    if (Technology::IdentifierFromStorageGroup(group) != Technology::kUnknown)
      service_groups.push_back(group);
  }

  return service_groups;
}

bool Profile::UpdateDevice(const DeviceRefPtr &device) {
  return false;
}

bool Profile::UpdateWiFiProvider(const WiFiProvider &wifi_provider) {
  return false;
}

void Profile::HelpRegisterConstDerivedStrings(
    const string &name,
    Strings(Profile::*get)(Error *)) {
  store_.RegisterDerivedStrings(
      name,
      StringsAccessor(new CustomAccessor<Profile, Strings>(this, get, NULL)));
}

}  // namespace shill
