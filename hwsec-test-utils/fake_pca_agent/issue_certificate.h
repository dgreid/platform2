// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HWSEC_TEST_UTILS_FAKE_PCA_AGENT_ISSUE_CERTIFICATE_H_
#define HWSEC_TEST_UTILS_FAKE_PCA_AGENT_ISSUE_CERTIFICATE_H_

#include <string>

#include <base/optional.h>
#include <crypto/scoped_openssl_types.h>

namespace hwsec_test_utils {

// Creates a certificate signed by a random RSA key to |subject|, with none of
// the information in the certificate makes real sense.
crypto::ScopedX509 IssueTestCertificate(const crypto::ScopedEVP_PKEY& subject);

// Creates a certificate signed by a random RSA key to |subject| and convert it
// to DER-encoded string.
base::Optional<std::string> IssueTestCertificateDer(
    const crypto::ScopedEVP_PKEY& subject);

}  // namespace hwsec_test_utils

#endif  // HWSEC_TEST_UTILS_FAKE_PCA_AGENT_ISSUE_CERTIFICATE_H_
