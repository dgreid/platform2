// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/storage_manager_impl.h"

#include <base/logging.h>
#include <base/no_destructor.h>

namespace federated {

bool StorageManagerImpl::OnExampleReceived(
    const std::string& client_name, const std::string& serialized_example) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TODO(alanlxl): Insert data into DB
  return true;
}

bool StorageManagerImpl::PrepareStreamingForClient(
    const std::string& client_name) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TODO(alanlxl): Query DB if there're examples for the given client.
  return true;
}

bool StorageManagerImpl::GetNextExample(std::string* example) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // TODO(alanlxl): query DB for the next example.
  return true;
}

bool StorageManagerImpl::CloseStreaming() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // TODO(alanlxl): we can clean the used examples here.
  return true;
}

StorageManager* StorageManager::GetInstance() {
  static base::NoDestructor<StorageManagerImpl> storage_manager;
  return storage_manager.get();
}

}  // namespace federated
