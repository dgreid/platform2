// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef OOBE_CONFIG_USB_UTILS_H_
#define OOBE_CONFIG_USB_UTILS_H_

#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <crypto/scoped_openssl_types.h>

namespace oobe_config {

extern const char kStatefulDir[];
extern const char kUnencryptedOobeConfigDir[];
extern const char kConfigFile[];
extern const char kDomainFile[];
extern const char kKeyFile[];
extern const char kDevDiskById[];
extern const char kUsbDevicePathSigFile[];
extern const char kStoreDir[];
extern const char kOobeConfigRestoreUser[];

// Use of this class removes a file after it goes out of scope. This means we do
// not have to worry about keeping tracking which files to delete when.
class ScopedPathUnlinker {
 public:
  explicit ScopedPathUnlinker(const base::FilePath& file) : file_(file) {}
  ScopedPathUnlinker(const ScopedPathUnlinker&) = delete;
  ScopedPathUnlinker& operator=(const ScopedPathUnlinker&) = delete;

  ~ScopedPathUnlinker() {
    if (!base::DeleteFile(file_, false)) {
      PLOG(ERROR) << "Unable to unlink path " << file_.value();
    }
  }

 private:
  const base::FilePath file_;
};

// Using |priv_key|, signs |src| file, and writes the digest into |dst|.
bool Sign(const base::FilePath& priv_key,
          const base::FilePath& src,
          const base::FilePath& dst);

// Using |priv_key|, signs |src_content|, and writes the digest into |dst|.
bool Sign(const base::FilePath& priv_key,
          const std::string& src_content,
          const base::FilePath& dst);

// Reads the |pub_key_file| into |pub_key| (a data structure usable by
// libcrypto.)
bool ReadPublicKey(const base::FilePath& pub_key_file,
                   crypto::ScopedEVP_PKEY* pub_key);

// Verifies the |signature| of a |message| using the default and already
// verified public key |pub_key|.
bool VerifySignature(const std::string& message,
                     const std::string& signature,
                     const crypto::ScopedEVP_PKEY& pub_key);

}  // namespace oobe_config

#endif  // OOBE_CONFIG_USB_UTILS_H_
