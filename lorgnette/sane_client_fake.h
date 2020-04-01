// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_SANE_CLIENT_FAKE_H_
#define LORGNETTE_SANE_CLIENT_FAKE_H_

#include <map>
#include <string>

#include <base/synchronization/lock.h>

#include "lorgnette/manager.h"
#include "lorgnette/sane_client.h"

namespace lorgnette {

class SaneClientFake : public SaneClient {
 public:
  bool ListDevices(brillo::ErrorPtr* error,
                   Manager::ScannerInfo* info_out) override;

  void SetListDevicesResult(bool value);
  void AddDevice(const std::string& name,
                 const std::string& manufacturer,
                 const std::string& model,
                 const std::string& type);
  void RemoveDevice(const std::string& name);

 private:
  base::Lock lock_;

  bool list_devices_result_;
  Manager::ScannerInfo scanners_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_SANE_CLIENT_FAKE_H_
