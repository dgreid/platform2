// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "oobe_config/usb_utils.h"

#include <string>
#include <vector>

#include <base/files/file_util.h>
#include <base/strings/string_util.h>
#include <brillo/process/process.h>
#include <crypto/scoped_openssl_types.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

using base::FilePath;
using crypto::ScopedEVP_PKEY;
using std::string;
using std::vector;

namespace oobe_config {

const char kStatefulDir[] = "/mnt/stateful_partition/";
const char kUnencryptedOobeConfigDir[] = "unencrypted/oobe_auto_config/";
const char kConfigFile[] = "config.json";
const char kDomainFile[] = "enrollment_domain";
const char kKeyFile[] = "validation_key.pub";
const char kDevDiskById[] = "/dev/disk/by-id/";
const char kUsbDevicePathSigFile[] = "usb_device_path.sig";
const char kStoreDir[] = "/var/lib/oobe_config_restore/";
const char kOobeConfigRestoreUser[] = "oobe_config_restore";

bool Sign(const FilePath& priv_key,
          const string& src_content,
          const FilePath& dst) {
  if (src_content.empty()) {
    LOG(ERROR) << "Input content string cannot be empty!";
    return false;
  }
  // Reading the private key.
  base::ScopedFILE pkf(base::OpenFile(priv_key, "r"));
  if (!pkf) {
    PLOG(ERROR) << "Failed to open the private key file " << priv_key.value();
    return false;
  }
  crypto::ScopedEVP_PKEY private_key(
      PEM_read_PrivateKey(pkf.get(), nullptr, nullptr, nullptr));
  if (!private_key) {
    PLOG(ERROR) << "Failed to read the PEM private key file "
                << priv_key.value();
    return false;
  }

  // Creating the context.
  crypto::ScopedEVP_MD_CTX mdctx(EVP_MD_CTX_new());
  if (!mdctx) {
    LOG(ERROR) << "Failed to create a EVP_MD context.";
    return false;
  }

  // Signing the |src_content|.
  size_t signature_len = 0;
  if (EVP_DigestSignInit(mdctx.get(), nullptr, EVP_sha256(), nullptr,
                         private_key.get()) != 1 ||
      EVP_DigestSignUpdate(mdctx.get(), src_content.c_str(),
                           src_content.length()) != 1 ||
      EVP_DigestSignFinal(mdctx.get(), nullptr, &signature_len) != 1) {
    LOG(ERROR) << "Failed to sign the content.";
  }
  // Now we know the size of the signature, let's get it.
  crypto::ScopedOpenSSLBytes signature(reinterpret_cast<unsigned char*>(
      OPENSSL_malloc(sizeof(unsigned char) * signature_len)));
  if (!signature) {
    PLOG(ERROR) << "Failed to malloc " << signature_len
                << " bytes for the signature.";
    return false;
  }
  if (EVP_DigestSignFinal(mdctx.get(), signature.get(), &signature_len) != 1) {
    LOG(ERROR) << "Getting the signature failed; however, technically this"
               << " should not happen!";
    return false;
  }
  if (!base::WriteFile(dst, reinterpret_cast<char*>(signature.get()),
                       signature_len)) {
    PLOG(ERROR) << "Failed to write the signature to file " << dst.value();
    return false;
  }

  return true;
}

bool Sign(const FilePath& priv_key, const FilePath& src, const FilePath& dst) {
  // Reading the source file.
  string src_content;
  if (!base::ReadFileToString(src, &src_content)) {
    PLOG(ERROR) << "Failed to read the source file " << src.value();
    return false;
  }

  return Sign(priv_key, src_content, dst);
}

bool ReadPublicKey(const FilePath& pub_key_file, ScopedEVP_PKEY* pub_key) {
  base::ScopedFILE pkf(base::OpenFile(pub_key_file, "r"));
  if (!pkf) {
    PLOG(ERROR) << "Failed to open the public key file "
                << pub_key_file.value();
    return false;
  }
  pub_key->reset(PEM_read_PUBKEY(pkf.get(), nullptr, nullptr, nullptr));
  if (!(*pub_key)) {
    LOG(ERROR) << "Failed to read the PEM public key file "
               << pub_key_file.value();
    return false;
  }
  return true;
}

bool VerifySignature(const string& message,
                     const string& signature,
                     const ScopedEVP_PKEY& pub_key) {
  crypto::ScopedEVP_MD_CTX mdctx(EVP_MD_CTX_new());
  if (!mdctx) {
    LOG(ERROR) << "Failed to create a EVP_MD context.";
    return false;
  }
  if (EVP_DigestVerifyInit(mdctx.get(), nullptr, EVP_sha256(), nullptr,
                           pub_key.get()) != 1 ||
      EVP_DigestVerifyUpdate(mdctx.get(), message.c_str(), message.length()) !=
          1 ||
      EVP_DigestVerifyFinal(
          mdctx.get(), reinterpret_cast<const unsigned char*>(signature.data()),
          signature.size()) != 1) {
    LOG(ERROR) << "Failed to verify the signature.";
    return false;
  }
  return true;
}

}  // namespace oobe_config
