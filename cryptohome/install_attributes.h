// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// InstallAttributes - class for managing install-time system attributes.

#ifndef CRYPTOHOME_INSTALL_ATTRIBUTES_H_
#define CRYPTOHOME_INSTALL_ATTRIBUTES_H_

#include <memory>
#include <string>

#include <base/files/file_path.h>
#include <base/macros.h>
#include <base/observer_list.h>
#include <base/observer_list_types.h>
#include <base/values.h>
#include <brillo/secure_blob.h>

#include "cryptohome/crypto.h"
#include "cryptohome/install_attributes.pb.h"
#include "cryptohome/lockbox.h"
#include "cryptohome/platform.h"
#include "cryptohome/tpm.h"
#include "cryptohome/tpm_init.h"

namespace cryptohome {

// InstallAttributes - manages secure, install-time attributes
//
// Provides setting and getting of tamper-evident install-time
// attributes.  Upon finalization, the underlying tamper-evident
// store will "lock" the attributes such that they become read-only
// until the next install.
//
// InstallAttributes is not thread-safe and should not be accessed in parallel.
class InstallAttributes {
 public:
  enum class Status {
    kUnknown,       // Not initialized yet.
    kTpmNotOwned,   // TPM not owned yet.
    kFirstInstall,  // Allows writing.
    kValid,         // Validated successfully.
    kInvalid,       // Not valid, e.g. clobbered, absent.
    COUNT,          // This is unused, just for counting the number of elements.
                    // Note that COUNT should always be the last element.
  };

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnFinalized() = 0;
  };

  // Creates an instance of install attributes that will use the |tpm|. If |tpm|
  // is NULL, InstallAttributes will proceed insecurely (unless it is set with
  // SetTpm at a later time).
  explicit InstallAttributes(Tpm* tpm);
  virtual ~InstallAttributes();

  virtual Status status() const { return status_; }

  // Sets status (for testing).
  void set_status_for_testing(Status status) { status_ = status; }

  // Updates the TPM used by Lockbox or disables the use of the TPM.
  // This does NOT take ownership of the pointer.
  virtual void SetTpm(Tpm* tpm);

  // Prepares the class for use including instantiating a new environment
  // if needed. If initialization completes, |tpm_init| will be used to remove
  // this instance's dependency on the TPM ownership.
  virtual bool Init(TpmInit* tpm_init);

  // Populates |value| based on the content referenced by |name|.
  //
  // Parameters
  // - name: addressable name of the entry to retrieve
  // - value: pointer to a Blob to populate with the value, if found.
  // Returns true if |name| exists in the store and |value| will be populated.
  // Returns false if the |name| does not exist.
  virtual bool Get(const std::string& name, brillo::Blob* value) const;

  // Populates |name| and |value| based on the content referenced by |index|.
  //
  // Parameters
  // - index: 0-addressable index of the desired entry.
  // - name: addressable name of the entry to retrieve
  // - value: pointer to a Blob to populate with the value, if found.
  // Returns true if |index| exists in the store.
  // Returns false if the |index| does not exist.
  virtual bool GetByIndex(int index,
                          std::string* name,
                          brillo::Blob* value) const;

  // Appends |name| and |value| as an attribute pair to the internal store.
  //
  // Parameters
  // - name: attribute name to associate |value| with in the store
  // - value: Blob of data to store with |name|.
  // Returns true if the association can be stored, and false if it can't.
  // If the given |name| already exists, it will be replaced.
  virtual bool Set(const std::string& name, const brillo::Blob& value);

  // Finalizes the install-time attributes making them tamper-evident.
  virtual bool Finalize();

  // Returns the number of entries in the Lockbox.
  virtual int Count() const;

  // Return InstallAttributes version.
  // This is populated from the default value in install_attributes.proto and
  // should be incremented there when behavior vesioning is needed.
  virtual uint64_t version() const { return version_; }

  // Allows overriding the version, often for testing.
  virtual void set_version(uint64_t version) { version_ = version; }

  // Returns true if the attribute storage is securely stored.  It does not
  // indicate if the store has been finalized, just if the system TPM/Lockbox
  // is being used.
  virtual bool is_secure() const { return is_secure_; }

  virtual void set_is_secure(bool is_secure) { is_secure_ = is_secure; }

  // Allows replacement of the underlying lockbox.
  // This does NOT take ownership of the pointer.
  virtual void set_lockbox(Lockbox* lockbox) { lockbox_ = lockbox; }

  virtual Lockbox* lockbox() { return lockbox_; }

  // Replaces the platform implementation.
  // Does NOT take ownership of the pointer.
  virtual void set_platform(Platform* platform) { platform_ = platform; }

  virtual Platform* platform() { return platform_; }

  // Returns a description of the system's install attributes as a Value.
  //
  // The Value is a DictionaryValue, with keys "initialized", "version",
  // "lockbox_index", "secure", "invalid", "first_install" and "size".
  virtual std::unique_ptr<base::Value> GetStatus();

  void AddObserver(Observer* obs) {
    observer_list_.AddObserver(obs);
  }

  void RemoveObserver(Observer* obs) {
    observer_list_.RemoveObserver(obs);
  }

  void NotifyFinalized() {
    for (Observer& observer : observer_list_)
      observer.OnFinalized();
  }

  // Provides the TPM NVRAM index to be used by the underlying Lockbox instance.
  static const uint32_t kLockboxIndex;
  // Provides the default location for the attributes data file.
  static const char kDefaultDataFile[];
  // File permissions of attributes data file (modulo umask).
  static const mode_t kDataFilePermissions;
  // Provides the default location for the cache file.
  static const char kDefaultCacheFile[];
  // File permissions of cache file (modulo umask).
  static const mode_t kCacheFilePermissions;

 protected:
  // Helper to find a given entry index using its name.
  virtual int FindIndexByName(const std::string& name) const;
  // Convert the current attributes to a byte stream and write it
  // to |out_bytes|.
  virtual bool SerializeAttributes(brillo::Blob* out_bytes);
  // Remove the data file on disk if it exists.
  bool ClearData();

 private:
  Status status_ = Status::kUnknown;
  bool is_secure_ = false;   // Indicates if there is hardware protection (TPM).
  base::FilePath data_file_;     // Location data is persisted to.
  base::FilePath cache_file_;    // World-readable data cache file.
  uint64_t version_ = 0;         // Default implementation version.
  // Default implementations of dependencies
  std::unique_ptr<SerializedInstallAttributes> default_attributes_;
  std::unique_ptr<Lockbox> default_lockbox_;
  std::unique_ptr<Platform> default_platform_;
  // Overridable dependency pointer which allow for easy injection.
  SerializedInstallAttributes* attributes_ = nullptr;
  Lockbox* lockbox_ = nullptr;
  Platform* platform_ = nullptr;
  base::ObserverList<Observer> observer_list_;

  DISALLOW_COPY_AND_ASSIGN(InstallAttributes);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_INSTALL_ATTRIBUTES_H_
