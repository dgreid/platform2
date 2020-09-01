// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <base/message_loop/message_loop_current.h>
#include <base/task/single_thread_task_executor.h>

#include "sealed_storage/sealed_storage.h"
#include "sealed_storage/wrapper.h"

namespace sealed_storage {

namespace wrapper {

bool Unseal(bool verified_boot_mode,
            int additional_pcr,
            const uint8_t* sealed_buf,
            size_t sealed_size,
            uint8_t* plain_buf,
            size_t* plain_size) {
  DCHECK(sealed_buf);
  DCHECK(plain_buf);
  DCHECK(plain_size);

  std::unique_ptr<base::SingleThreadTaskExecutor> task_executor;
  if (!base::MessageLoopCurrent::IsSet()) {
    VLOG(2) << "Creating local SingleThreadTaskExector";
    task_executor.reset(
        new base::SingleThreadTaskExecutor(base::MessagePumpType::IO));
  }

  sealed_storage::Policy policy;
  if (verified_boot_mode) {
    policy.pcr_map.insert(Policy::BootModePCR(kVerifiedBootMode));
  }
  if (additional_pcr > 0) {
    policy.pcr_map.insert(Policy::UnchangedPCR(additional_pcr));
  }

  SealedStorage storage(policy);

  Data input(sealed_buf, sealed_buf + sealed_size);
  auto output = storage.Unseal(input);
  if (!output.has_value()) {
    return false;
  }
  if (output->size() > *plain_size) {
    LOG(ERROR) << "Too small buffer for plaintext data: " << *plain_size
               << " < " << output->size();
    return false;
  }
  memcpy(plain_buf, output->data(), output->size());
  *plain_size = output->size();
  return true;
}

}  // namespace wrapper

}  // namespace sealed_storage
