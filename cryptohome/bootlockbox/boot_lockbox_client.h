// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_CLIENT_H_
#define CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_CLIENT_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <brillo/brillo_export.h>
#include <dbus/bus.h>

namespace base {
class FilePath;
}  // namespace base

// Uses forward declaration because b/120999677.
namespace org {
namespace chromium {
class BootLockboxInterfaceProxy;
}  // namespace chromium
}  // namespace org

namespace cryptohome {

// A class that that manages the communication with BootLockbox.
class BRILLO_EXPORT BootLockboxClient {
 public:
  // Creates BootLockboxClient. The factory should be called on the same thread
  // that will call ~BootLockboxClient();
  static std::unique_ptr<BootLockboxClient> CreateBootLockboxClient();

  virtual ~BootLockboxClient();

  // Store |digest| in NVRamBootLockbox with index |key|.
  virtual bool Store(const std::string& key, const std::string& digest);

  // Read digest from NVRamBootLockbox indexed by key.
  virtual bool Read(const std::string& key, std::string* digest);

  // Locks BootLockboxClient. Signing operation won't be available afterwards.
  virtual bool Finalize();

 protected:
  BootLockboxClient(
      std::unique_ptr<org::chromium::BootLockboxInterfaceProxy> bootlockbox,
      scoped_refptr<dbus::Bus> bus);

 private:
  std::unique_ptr<org::chromium::BootLockboxInterfaceProxy> bootlockbox_;
  scoped_refptr<dbus::Bus> bus_;

  DISALLOW_COPY_AND_ASSIGN(BootLockboxClient);
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_BOOTLOCKBOX_BOOT_LOCKBOX_CLIENT_H_
