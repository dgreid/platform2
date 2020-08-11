// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_STORAGE_MANAGER_H_
#define FEDERATED_STORAGE_MANAGER_H_

#include <string>

namespace federated {

// Singleton class providing storage to satisfy federated service interface
// which receives new examples and federated computation interface which
// consumes examples for training/analytics.
class StorageManager {
 public:
  static StorageManager* GetInstance();

  virtual bool OnExampleReceived(const std::string& client_name,
                                 const std::string& serialized_example) = 0;

  // Provide example streaming. We assume there're no parallel streamings.
  // Usage:
  // 1. call PrepareStreamingForClient(), if it returns true, then;
  // 2. call GetNextExample() to get examples, until it returns false, then;
  // 3. call CloseStreaming() to close the current streaming.
  virtual bool PrepareStreamingForClient(const std::string& client_name) = 0;
  virtual bool GetNextExample(std::string* example) = 0;
  virtual bool CloseStreaming() = 0;

 protected:
  StorageManager() = default;
  virtual ~StorageManager() = default;
};

}  // namespace federated

#endif  // FEDERATED_STORAGE_MANAGER_H_
