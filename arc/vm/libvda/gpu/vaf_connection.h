// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ARC_VM_LIBVDA_GPU_VAF_CONNECTION_H_
#define ARC_VM_LIBVDA_GPU_VAF_CONNECTION_H_

#include <memory>
#include <string>
#include <vector>

#include <base/threading/thread.h>
#include <base/threading/thread_checker.h>
#include <mojo/core/embedder/scoped_ipc_support.h>

#include "arc/vm/libvda/gpu/mojom/video.mojom.h"

namespace arc {

// VafConnection provides a connection to the mojo VideoAcceleratorFactory
// interface using the LibvdaService D-Bus service. Only a single instantiated
// VafConnection object should exist at a time. Callers can use the
// GetVafConnection() function to retrieve an instance.
class VafConnection {
 public:
  ~VafConnection();
  scoped_refptr<base::SingleThreadTaskRunner> GetIpcTaskRunner();
  void CreateDecodeAccelerator(arc::mojom::VideoDecodeAcceleratorPtr* vda_ptr);
  void CreateEncodeAccelerator(arc::mojom::VideoEncodeAcceleratorPtr* vea_ptr);

  // Returns a VafConnection instance.
  static VafConnection* Get();

 private:
  VafConnection();
  bool Initialize();
  void InitializeOnIpcThread(bool* init_success);
  void CleanupOnIpcThread();
  void OnFactoryError(uint32_t custom_reason, const std::string& description);
  void CreateDecodeAcceleratorOnIpcThread(
      arc::mojom::VideoDecodeAcceleratorPtr* vda_ptr);
  void CreateEncodeAcceleratorOnIpcThread(
      arc::mojom::VideoEncodeAcceleratorPtr* vea_ptr);

  base::Thread ipc_thread_;
  // TODO(alexlau): Use THREAD_CHECKER macro after libchrome uprev
  // (crbug.com/909719).
  base::ThreadChecker ipc_thread_checker_;
  std::unique_ptr<mojo::core::ScopedIPCSupport> ipc_support_;
  arc::mojom::VideoAcceleratorFactoryPtr factory_ptr_;
};

}  // namespace arc

#endif  // ARC_VM_LIBVDA_GPU_VAF_CONNECTION_H_
