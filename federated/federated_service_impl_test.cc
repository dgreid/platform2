// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <utility>

#include <base/run_loop.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "chrome/knowledge/federated/example.pb.h"
#include "federated/federated_service_impl.h"
#include "federated/mock_storage_manager.h"
#include "federated/mojom/federated_service.mojom.h"
#include "federated/test_utils.h"
#include "federated/utils.h"

namespace federated {
namespace {

using chromeos::federated::mojom::FederatedService;
using testing::_;
using testing::Return;
using testing::StrictMock;

TEST(FederatedServiceImplTest, TestReportExample) {
  std::unique_ptr<MockStorageManager> storage_manager(
      new StrictMock<MockStorageManager>());

  const std::string mock_client_name = "client1";
  EXPECT_CALL(*storage_manager, OnExampleReceived(mock_client_name, _))
      .Times(1)
      .WillOnce(Return(true));

  mojo::Remote<FederatedService> federated_service;
  const FederatedServiceImpl federated_service_impl(
      federated_service.BindNewPipeAndPassReceiver().PassPipe(),
      base::Closure(), storage_manager.get());

  federated_service->ReportExample(mock_client_name, CreateExamplePtr());

  base::RunLoop().RunUntilIdle();
}

TEST(FederatedServiceImplTest, TestClone) {
  std::unique_ptr<MockStorageManager> storage_manager(
      new StrictMock<MockStorageManager>());

  const std::string mock_client_name = "client1";
  EXPECT_CALL(*storage_manager, OnExampleReceived(mock_client_name, _))
      .Times(1)
      .WillOnce(Return(true));

  mojo::Remote<FederatedService> federated_service;
  const FederatedServiceImpl federated_service_impl(
      federated_service.BindNewPipeAndPassReceiver().PassPipe(),
      base::Closure(), storage_manager.get());

  // Call Clone to bind another FederatedService.
  mojo::Remote<FederatedService> federated_service_2;
  federated_service->Clone(federated_service_2.BindNewPipeAndPassReceiver());

  federated_service_2->ReportExample(mock_client_name, CreateExamplePtr());

  base::RunLoop().RunUntilIdle();
}

}  // namespace
}  // namespace federated
