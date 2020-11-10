// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRYPTOHOME_MOCK_CHAPS_CLIENT_FACTORY_H_
#define CRYPTOHOME_MOCK_CHAPS_CLIENT_FACTORY_H_

#include "cryptohome/chaps_client_factory.h"

namespace cryptohome {

class MockChapsClientFactory : public ChapsClientFactory {
 public:
  MockChapsClientFactory();
  MockChapsClientFactory(const MockChapsClientFactory&) = delete;
  MockChapsClientFactory& operator=(const MockChapsClientFactory&) = delete;

  virtual ~MockChapsClientFactory();
  virtual chaps::TokenManagerClient* New();
};

}  // namespace cryptohome

#endif  // CRYPTOHOME_MOCK_CHAPS_CLIENT_FACTORY_H_
