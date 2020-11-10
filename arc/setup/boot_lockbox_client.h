// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_SETUP_BOOT_LOCKBOX_CLIENT_H_
#define ARC_SETUP_BOOT_LOCKBOX_CLIENT_H_

#include <memory>
#include <string>

#include <base/macros.h>
#include <base/memory/ref_counted.h>
#include <dbus/bus.h>

namespace base {
class FilePath;
}  // namespace base

namespace org {
namespace chromium {
class CryptohomeInterfaceProxyInterface;
}  // namespace chromium
}  // namespace org

namespace arc {

// TODO(xzhou): This class is NOT production-ready yet. arc-setup.cc does not
// call into this at all. Talk to xzhou/yusukes before using it.

// A class that that manages the communication with TPM.
class BootLockboxClient {
 public:
  // Creates BootLockboxClient. The factory should be called on the same thread
  // that will call ~BootLockboxClient();
  static std::unique_ptr<BootLockboxClient> CreateBootLockboxClient();

  ~BootLockboxClient();

  // Verifies the code integrity in |dalvik_cache_dir| using BootLockbox.
  bool CheckCodeIntegrity(const base::FilePath& dalvik_cache_dir);

  // Checks if cryptohomed is ready.
  bool IsServiceReady();

  // Checks if TPM is ready, meaning it is enabled, owned and not being owned.
  bool IsTpmReady();

  // Signs |digest| using BootLockbox key. The signature is stored in
  // |signature|.
  bool Sign(const std::string& digest, std::string* signature);

  // Verifies |digest| against |signature|. Returns true if signature
  // verification successed. Returns false if signature is invalid or the
  // operation failed.
  bool Verify(const std::string& digest, const std::string& signature);

  // Locks BootLockboxClient key. After calling this function, any access to
  // BootLockbox fails.
  bool Finalize();

 private:
  BootLockboxClient(
      std::unique_ptr<org::chromium::CryptohomeInterfaceProxyInterface>
          cryotohome,
      scoped_refptr<dbus::Bus> bus);
  BootLockboxClient(const BootLockboxClient&) = delete;
  BootLockboxClient& operator=(const BootLockboxClient&) = delete;

  std::unique_ptr<org::chromium::CryptohomeInterfaceProxyInterface> cryptohome_;
  scoped_refptr<dbus::Bus> bus_;
};

}  // namespace arc

#endif  // ARC_SETUP_BOOT_LOCKBOX_CLIENT_H_
