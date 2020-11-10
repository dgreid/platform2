// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_OBJECT_IMPORTER_MOCK_H_
#define CHAPS_OBJECT_IMPORTER_MOCK_H_

#include "chaps/object_importer.h"

#include <base/macros.h>
#include <gmock/gmock.h>

namespace chaps {

class ObjectImporterMock : public ObjectImporter {
 public:
  ObjectImporterMock();
  ObjectImporterMock(const ObjectImporterMock&) = delete;
  ObjectImporterMock& operator=(const ObjectImporterMock&) = delete;

  ~ObjectImporterMock() override;

  MOCK_METHOD1(ImportObjects, bool(ObjectPool*));  // NOLINT - 'unnamed' param
  MOCK_METHOD1(FinishImportAsync, bool(ObjectPool*));  // NOLINT - 'unnamed'
};

}  // namespace chaps

#endif  // CHAPS_OBJECT_IMPORTER_MOCK_H_
