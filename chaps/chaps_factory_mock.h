// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_CHAPS_FACTORY_MOCK_H_
#define CHAPS_CHAPS_FACTORY_MOCK_H_

#include "chaps/chaps_factory.h"

#include <base/macros.h>
#include <gmock/gmock.h>

namespace chaps {

class ChapsFactoryMock : public ChapsFactory {
 public:
  ChapsFactoryMock();
  ChapsFactoryMock(const ChapsFactoryMock&) = delete;
  ChapsFactoryMock& operator=(const ChapsFactoryMock&) = delete;

  ~ChapsFactoryMock() override;

  MOCK_METHOD5(CreateSession,
               Session*(int, ObjectPool*, TPMUtility*, HandleGenerator*, bool));
  MOCK_METHOD3(CreateObjectPool,
               ObjectPool*(HandleGenerator*, ObjectStore*, ObjectImporter*));
  MOCK_METHOD1(CreateObjectStore, ObjectStore*(const base::FilePath&));
  MOCK_METHOD0(CreateObject, Object*());
  MOCK_METHOD1(CreateObjectPolicy, ObjectPolicy*(CK_OBJECT_CLASS));
  MOCK_METHOD3(CreateObjectImporter,
               ObjectImporter*(int, const base::FilePath&, TPMUtility*));
};

}  // namespace chaps

#endif  // CHAPS_CHAPS_FACTORY_MOCK_H_
