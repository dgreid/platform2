// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_DELEGATE_IMPL_H_
#define DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_DELEGATE_IMPL_H_

#include <memory>

#include <base/macros.h>
#include <base/threading/thread.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "diagnostics/cros_healthd_mojo_adapter/cros_healthd_mojo_adapter_delegate.h"

namespace diagnostics {

// Production implementation of the CrosHealthdMojoAdapterDelegate interface.
class CrosHealthdMojoAdapterDelegateImpl final
    : public CrosHealthdMojoAdapterDelegate {
 public:
  CrosHealthdMojoAdapterDelegateImpl();
  ~CrosHealthdMojoAdapterDelegateImpl() override;

  // CrosHealthdMojoAdapterDelegate overrides:
  chromeos::cros_healthd::mojom::CrosHealthdServiceFactoryPtr
  GetCrosHealthdServiceFactory() override;

 private:
  // IPC threads.
  base::Thread mojo_thread_{"Mojo Thread"};
  base::Thread dbus_thread_{"D-Bus Thread"};

  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;

  DISALLOW_COPY_AND_ASSIGN(CrosHealthdMojoAdapterDelegateImpl);
};

}  // namespace diagnostics

#endif  // DIAGNOSTICS_CROS_HEALTHD_MOJO_ADAPTER_CROS_HEALTHD_MOJO_ADAPTER_DELEGATE_IMPL_H_
