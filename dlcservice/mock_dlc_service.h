// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DLCSERVICE_MOCK_DLC_SERVICE_H_
#define DLCSERVICE_MOCK_DLC_SERVICE_H_

#include <string>

#include <base/macros.h>

#include "dlcservice/dlc_service.h"

namespace dlcservice {

class MockDlcService : public DlcServiceInterface {
 public:
  MockDlcService() = default;

  MOCK_METHOD(void, Initialize, (), (override));
  MOCK_METHOD(bool,
              Install,
              (const DlcIdList&, const std::string&, brillo::ErrorPtr*),
              (override));
  MOCK_METHOD(bool,
              Uninstall,
              (const std::string& id_in, brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(bool,
              Purge,
              (const std::string& id_in, brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(DlcIdList, GetInstalled, (), (override));
  MOCK_METHOD((const DlcBase*), GetDlc, (const DlcId& id), (override));
  MOCK_METHOD(bool,
              GetState,
              (const std::string& id_in,
               DlcState* dlc_state_out,
               brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(bool,
              InstallCompleted,
              (const DlcIdList& ids_in, brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(bool,
              UpdateCompleted,
              (const DlcIdList& ids_in, brillo::ErrorPtr* err),
              (override));
  MOCK_METHOD(void, AddObserver, (Observer * observer), (override));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockDlcService);
};

}  // namespace dlcservice

#endif  // DLCSERVICE_MOCK_DLC_SERVICE_H_
