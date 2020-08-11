// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEDERATED_FEDERATED_SERVICE_IMPL_H_
#define FEDERATED_FEDERATED_SERVICE_IMPL_H_

#include <string>

#include <mojo/public/cpp/bindings/pending_receiver.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "federated/mojom/federated_service.mojom.h"
#include "federated/storage_manager.h"

namespace federated {

class FederatedServiceImpl
    : public chromeos::federated::mojom::FederatedService {
 public:
  // Creates an instance bound to `pipe`. The specified `disconnect_handler`
  // will be invoked if the binding encounters a connection error or is closed.
  FederatedServiceImpl(mojo::ScopedMessagePipeHandle pipe,
                       base::Closure disconnect_handler,
                       StorageManager* storage_manager);

 private:
  // chromeos::federated::mojom::FederatedService:
  void Clone(mojo::PendingReceiver<chromeos::federated::mojom::FederatedService>
                 receiver) override;
  void ReportExample(const std::string& client_name,
                     chromeos::federated::mojom::ExamplePtr example) override;

  StorageManager* const storage_manager_;

  // Primordial receiver bootstrapped over D-Bus. Once opened, is never closed.
  mojo::Receiver<chromeos::federated::mojom::FederatedService> receiver_;

  // Additional receivers bound via `Clone`.
  mojo::ReceiverSet<chromeos::federated::mojom::FederatedService>
      clone_receivers_;

  DISALLOW_COPY_AND_ASSIGN(FederatedServiceImpl);
};

}  // namespace federated

#endif  // FEDERATED_FEDERATED_SERVICE_IMPL_H_
