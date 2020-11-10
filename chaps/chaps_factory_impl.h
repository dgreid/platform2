// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHAPS_CHAPS_FACTORY_IMPL_H_
#define CHAPS_CHAPS_FACTORY_IMPL_H_

#include "chaps/chaps_factory.h"

#include <base/macros.h>

namespace chaps {

class ChapsFactoryImpl : public ChapsFactory {
 public:
  ChapsFactoryImpl() {}
  ChapsFactoryImpl(const ChapsFactoryImpl&) = delete;
  ChapsFactoryImpl& operator=(const ChapsFactoryImpl&) = delete;
  ~ChapsFactoryImpl() override {}

  Session* CreateSession(int slot_id,
                         ObjectPool* token_object_pool,
                         TPMUtility* tpm_utility,
                         HandleGenerator* handle_generator,
                         bool is_read_only) override;
  ObjectPool* CreateObjectPool(HandleGenerator* handle_generator,
                               ObjectStore* store,
                               ObjectImporter* importer) override;
  ObjectStore* CreateObjectStore(const base::FilePath& file_name) override;
  Object* CreateObject() override;
  ObjectPolicy* CreateObjectPolicy(CK_OBJECT_CLASS type) override;
  ObjectImporter* CreateObjectImporter(int slot_id,
                                       const base::FilePath& path,
                                       TPMUtility* tpm_utility) override;

  static ObjectPolicy* GetObjectPolicyForType(CK_OBJECT_CLASS type);
};

}  // namespace chaps

#endif  // CHAPS_CHAPS_FACTORY_IMPL_H_
