// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// PCA interface classes for cert_provision library.

#ifndef CRYPTOHOME_CERT_CERT_PROVISION_PCA_H_
#define CRYPTOHOME_CERT_CERT_PROVISION_PCA_H_

#include <memory>
#include <string>

#include <brillo/http/http_utils.h>
#include <brillo/secure_blob.h>

#include "cryptohome/cert/cert_provision_util.h"

namespace cert_provision {

// Proxy for exchanging messages with a PCA over network.
class PCAProxy {
 public:
  explicit PCAProxy(const std::string& pca_url) : pca_url_(pca_url) {}

  virtual ~PCAProxy() {}

  // Sends |request| to the PCA, waits for the |response|. |action| is appended
  // to the PCA base url to form the url for the POST request.
  virtual OpResult MakeRequest(const std::string& action,
                               const brillo::SecureBlob& request,
                               brillo::SecureBlob* reply) = 0;

  // Returns a new scoped default PCAProxy implementation unless
  // subst_obj is set. In that case, returns subst_obj.
  static Scoped<PCAProxy> Create(const std::string& pca_url);
  static PCAProxy* subst_obj;

 protected:
  std::string pca_url_;

 private:
  static std::unique_ptr<PCAProxy> GetDefault(const std::string& pca_url);
};

class PCAProxyImpl : public PCAProxy {
 public:
  explicit PCAProxyImpl(const std::string& pca_url);
  PCAProxyImpl(const PCAProxyImpl&) = delete;
  PCAProxyImpl& operator=(const PCAProxyImpl&) = delete;

  OpResult MakeRequest(const std::string& action,
                       const brillo::SecureBlob& request,
                       brillo::SecureBlob* reply) override;

 private:
  std::shared_ptr<brillo::http::Transport> http_transport_;
};

}  // namespace cert_provision

#endif  // CRYPTOHOME_CERT_CERT_PROVISION_PCA_H_
