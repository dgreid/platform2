// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_PROFILE_H_
#define SHILL_PROFILE_H_

#include <map>
#include <string>
#include <vector>

#include <base/memory/scoped_ptr.h>
#include <base/files/file_path.h>
#include <gtest/gtest_prod.h>  // for FRIEND_TEST

#include "shill/event_dispatcher.h"
#include "shill/property_store.h"
#include "shill/refptr_types.h"

namespace base {

class FilePath;

}  // namespace base

namespace shill {

class ControlInterface;
class Error;
class GLib;
class Manager;
class Metrics;
class ProfileAdaptorInterface;
class StoreInterface;
class WiFiProvider;

class Profile : public base::RefCounted<Profile> {
 public:
  enum InitStorageOption {
    kOpenExisting,
    kCreateNew,
    kCreateOrOpenExisting
  };
  struct Identifier {
    Identifier() {}
    explicit Identifier(const std::string &i) : identifier(i) {}
    Identifier(const std::string &u, const std::string &i)
        : user(u),
          identifier(i) {
    }
    std::string user;  // Empty for global.
    std::string identifier;
    std::string user_hash;
  };

  // Path to the cached list of inserted user profiles to be loaded at
  // startup.
  static const char kUserProfileListPathname[];

  Profile(ControlInterface *control_interface,
          Metrics *metrics,
          Manager *manager,
          const Identifier &name,
          const std::string &user_storage_directory,
          bool connect_to_rpc);

  virtual ~Profile();

  // Set up persistent storage for this Profile.
  bool InitStorage(GLib *glib,
                   InitStorageOption storage_option,
                   Error *error);

  // Set up stub storage for this Profile. The data will NOT be
  // persisted. In most cases, you should prefer InitStorage.
  void InitStubStorage();

  // Remove the persistent storage for this Profile.  It is an error to
  // do so while the underlying storage is open via InitStorage() or
  // set_storage().
  bool RemoveStorage(GLib *glib, Error *error);

  virtual std::string GetFriendlyName();

  virtual std::string GetRpcIdentifier();

  PropertyStore *mutable_store() { return &store_; }
  const PropertyStore &store() const { return store_; }

  // Set the storage inteface.  This is used for testing purposes.  It
  // takes ownership of |storage|.
  void set_storage(StoreInterface *storage);

  // Begin managing the persistence of |service|.
  // Returns true if |service| is new to this profile and was added,
  // false if the |service| already existed.
  virtual bool AdoptService(const ServiceRefPtr &service);

  // Cease managing the persistence of the Service |service|.
  // Returns true if |service| was found and abandoned, or not found.
  // Returns false if can't be abandoned.
  virtual bool AbandonService(const ServiceRefPtr &service);

  // Clobbers persisted notion of |service| with data from |service|.
  // Returns true if |service| was found and updated, false if not found.
  virtual bool UpdateService(const ServiceRefPtr &service);

  // Ask |service| if it can configure itself from the profile.  If it can,
  // ask |service| to perform the configuration and return true.  If not,
  // return false.
  virtual bool LoadService(const ServiceRefPtr &service);

  // Perform LoadService() on |service|.  If this succeeds, change
  // the service to point at this profile and return true.  If not, return
  // false.
  virtual bool ConfigureService(const ServiceRefPtr &service);

  // Allow the device to configure itself from this profile.  Returns
  // true if the device succeeded in finding its configuration.  If not,
  // return false.
  virtual bool ConfigureDevice(const DeviceRefPtr &device);

  // Remove a named entry from the profile.  This includes detaching
  // any service that uses this profile entry.
  virtual void DeleteEntry(const std::string &entry_name, Error *error);

  // Return a service configured from the given profile entry.
  virtual ServiceRefPtr GetServiceFromEntry(const std::string &entry_name,
                                            Error *error);

  // Return whether |service| can configure itself from the profile.
  bool ContainsService(const ServiceConstRefPtr &service);

  std::vector<std::string> EnumerateAvailableServices(Error *error);
  std::vector<std::string> EnumerateEntries(Error *error);

  // Clobbers persisted notion of |device| with data from |device|. Returns true
  // if |device| was found and updated, false otherwise. The base implementation
  // always returns false -- currently devices are persisted only in
  // DefaultProfile.
  virtual bool UpdateDevice(const DeviceRefPtr &device);

  // Clobbers persisted notion of |wifi_provider| with data from
  // |wifi_provider|. Returns true if |wifi_provider| was found and updated,
  // false otherwise. The base implementation always returns false -- currently
  // wifi_provider is persisted only in DefaultProfile.
  virtual bool UpdateWiFiProvider(const WiFiProvider &wifi_provider);

  // Write all in-memory state to disk via |storage_|.
  virtual bool Save();

  // Parses a profile identifier. There're two acceptable forms of the |raw|
  // identifier: "identifier" and "~user/identifier". Both "user" and
  // "identifier" must be suitable for use in a D-Bus object path. Returns true
  // on success.
  static bool ParseIdentifier(const std::string &raw, Identifier *parsed);

  // Returns the composite string identifier for a profile, as would have
  // been used in an argument to Manager::PushProfile() in creating this
  // profile.  It returns a string in the form "identifier", or
  // "~user/identifier" depending on whether this profile has a user
  // component.
  static std::string IdentifierToString(const Identifier &name);

  // Load a list of user profile identifiers from a cache file |path|.
  // The profiles themselves are not loaded.
  static std::vector<Identifier> LoadUserProfileList(
      const base::FilePath &path);

  // Save a list of user profile identifiers |profiles| to a cache file |path|.
  // Returns true if successful, false otherwise.
  static bool SaveUserProfileList(const base::FilePath &path,
                                  const std::vector<ProfileRefPtr> &profiles);

  // Returns whether |name| matches this Profile's |name_|.
  virtual bool MatchesIdentifier(const Identifier &name) const;

  // Returns the username component of the profile identifier.
  const std::string &GetUser() const { return name_.user; }

  // Returns the user_hash component of the profile identifier.
  const std::string &GetUserHash() const { return name_.user_hash; }

  virtual StoreInterface *GetStorage() {
    return storage_.get();
  }

  // Returns a read-only copy of the backing storage of the profile.
  virtual const StoreInterface *GetConstStorage() const {
    return storage_.get();
  }

  virtual bool IsDefault() const { return false; }

 protected:
  // Protected getters
  Metrics *metrics() const { return metrics_; }
  Manager *manager() const { return manager_; }
  StoreInterface *storage() { return storage_.get(); }

  // Sets |path| to the persistent store file path for a profile identified by
  // |name_|. Returns true on success, and false if unable to determine an
  // appropriate file location. |name_| must be a valid identifier,
  // possibly parsed and validated through Profile::ParseIdentifier.
  //
  // In the default implementation, |name_.user| cannot be empty, because
  // all regular profiles should be associated with a user.
  virtual bool GetStoragePath(base::FilePath *path);

 private:
  friend class ProfileAdaptorInterface;
  FRIEND_TEST(ProfileTest, DeleteEntry);
  FRIEND_TEST(ProfileTest, GetStoragePath);
  FRIEND_TEST(ProfileTest, IsValidIdentifierToken);

  static bool IsValidIdentifierToken(const std::string &token);

  void HelpRegisterConstDerivedStrings(
      const std::string &name,
      Strings(Profile::*get)(Error *error));

  // Data members shared with subclasses via getter/setters above in the
  // protected: section
  Metrics *metrics_;
  Manager *manager_;

  // Shared with |adaptor_| via public getter.
  PropertyStore store_;

  // Properties to be gotten via PropertyStore calls.
  Identifier name_;

  // Path to user profile directory.
  const base::FilePath storage_path_;

  // Allows this profile to be backed with on-disk storage.
  scoped_ptr<StoreInterface> storage_;

  scoped_ptr<ProfileAdaptorInterface> adaptor_;

  DISALLOW_COPY_AND_ASSIGN(Profile);
};

}  // namespace shill

#endif  // SHILL_PROFILE_H_
