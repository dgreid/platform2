// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_STORAGE_MANAGER_IMPL_H_
#define FEDERATED_STORAGE_MANAGER_IMPL_H_

#include "federated/storage_manager.h"

#include <string>
#include <base/sequence_checker.h>

namespace federated {

class StorageManagerImpl : public StorageManager {
 public:
  StorageManagerImpl() = default;
  ~StorageManagerImpl() override = default;

  bool OnExampleReceived(const std::string& client_name,
                         const std::string& serialized_example) override;

  bool PrepareStreamingForClient(const std::string& client_name) override;
  bool GetNextExample(std::string* example) override;
  bool CloseStreaming() override;

 private:
  SEQUENCE_CHECKER(sequence_checker_);
  DISALLOW_COPY_AND_ASSIGN(StorageManagerImpl);
};

}  // namespace federated

#endif  // FEDERATED_STORAGE_MANAGER_IMPL_H_
