// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/federated_service_impl.h"

#include <utility>

#include "federated/mojom/example.mojom.h"
#include "federated/utils.h"

namespace federated {

FederatedServiceImpl::FederatedServiceImpl(mojo::ScopedMessagePipeHandle pipe,
                                           base::Closure disconnect_handler,
                                           StorageManager* storage_manager)
    : storage_manager_(storage_manager),
      receiver_(
          this,
          mojo::InterfaceRequest<chromeos::federated::mojom::FederatedService>(
              std::move(pipe))) {
  receiver_.set_disconnect_handler(std::move(disconnect_handler));
}

void FederatedServiceImpl::Clone(
    mojo::PendingReceiver<chromeos::federated::mojom::FederatedService>
        receiver) {
  clone_receivers_.Add(this, std::move(receiver));
}

void FederatedServiceImpl::ReportExample(
    const std::string& client_name,
    chromeos::federated::mojom::ExamplePtr example) {
  DCHECK(storage_manager_) << "storage_manager_ is not ready!";
  if (!example || !example->features || !example->features->feature.size()) {
    LOG(ERROR) << "Invalid/empty example received from client " << client_name;
    return;
  }
  if (!storage_manager_->OnExampleReceived(
          client_name,
          ConvertToTensorFlowExampleProto(example).SerializeAsString())) {
    // TODO(alanlxl): maybe a VLOG(1)
    LOG(ERROR) << "Failed to insert the example from client " << client_name;
  }
}

}  // namespace federated
