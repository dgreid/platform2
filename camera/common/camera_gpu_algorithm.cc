// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
#include "common/camera_gpu_algorithm.h"

#include <utility>

#include <base/bind.h>
#include <base/logging.h>
#include <base/numerics/safe_conversions.h>

#include "cros-camera/common.h"
#include "cros-camera/export.h"

namespace cros {

// static
CameraGPUAlgorithm* CameraGPUAlgorithm::GetInstance() {
  static CameraGPUAlgorithm* impl = new CameraGPUAlgorithm();
  return impl;
}

int32_t CameraGPUAlgorithm::Initialize(
    const camera_algorithm_callback_ops_t* callback_ops) {
  if (!callback_ops) {
    return -EINVAL;
  }
  if (!thread_.Start()) {
    LOGF(ERROR) << "Failed to start thread";
    return -EINVAL;
  }

  callback_ops_ = callback_ops;
  // Initialize the algorithms asynchronously
  thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&CameraGPUAlgorithm::InitializeOnThread,
                            base::Unretained(this)));
  return 0;
}

int32_t CameraGPUAlgorithm::RegisterBuffer(int buffer_fd) {
  base::AutoLock auto_lock(map_lock_);
  if (base::Contains(shm_region_map_, buffer_fd)) {
    LOGF(ERROR) << "Buffer already registered";
    return -EINVAL;
  }

  const int fd(HANDLE_EINTR(dup(buffer_fd)));
  DCHECK_GE(fd, 0) << "Failed to dup fd to get size";
  const int64_t file_size = base::File(fd).GetLength();
  DCHECK_GE(file_size, 0) << "Failed to get size";

  auto shm_region = base::UnsafeSharedMemoryRegion::Deserialize(
      base::subtle::PlatformSharedMemoryRegion::Take(
          base::ScopedFD(buffer_fd),
          base::subtle::PlatformSharedMemoryRegion::Mode::kUnsafe, file_size,
          base::UnguessableToken::Create()));
  if (!shm_region.IsValid()) {
    LOGF(ERROR) << "Failed to build shared memory region with size "
                << file_size;
    return -EINVAL;
  }
  shm_region_map_.insert(std::make_pair(buffer_fd, std::move(shm_region)));
  return buffer_fd;
}

void CameraGPUAlgorithm::Request(uint32_t req_id,
                                 const uint8_t req_header[],
                                 uint32_t size,
                                 int32_t buffer_handle) {
  thread_.task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          &CameraGPUAlgorithm::RequestOnThread, base::Unretained(this), req_id,
          std::vector<uint8_t>(req_header, req_header + size), buffer_handle));
}

void CameraGPUAlgorithm::DeregisterBuffers(const int32_t buffer_handles[],
                                           uint32_t size) {
  base::AutoLock auto_lock(map_lock_);
  for (uint32_t i = 0; i < size; i++) {
    if (!base::Contains(shm_region_map_, buffer_handles[i])) {
      LOGF(ERROR) << "Invalid buffer handle (" << buffer_handles[i] << ")";
      continue;
    }
    shm_region_map_.erase(buffer_handles[i]);
  }
}

CameraGPUAlgorithm::CameraGPUAlgorithm()
    : thread_("Camera Algorithm Thread"),
      callback_ops_(nullptr),
      is_initialized_(false) {}

void CameraGPUAlgorithm::InitializeOnThread() {
  VLOGF_ENTER();
  if (!portrait_processor_.Init()) {
    LOGF(ERROR) << "Failed to initialize portrait processor";
    return;
  }
  is_initialized_ = true;
  VLOGF_EXIT();
}

void CameraGPUAlgorithm::RequestOnThread(uint32_t req_id,
                                         std::vector<uint8_t> req_header,
                                         int32_t buffer_handle) {
  VLOGF_ENTER();
  auto* header =
      reinterpret_cast<const CameraGPUAlgoCmdHeader*>(req_header.data());
  auto callback = [&](uint32_t status) {
    (*callback_ops_->return_callback)(callback_ops_, req_id, status,
                                      buffer_handle);
  };
  if (!is_initialized_) {
    LOGF(ERROR) << "Algorithm is not initialized yet";
    callback(EAGAIN);
    return;
  }
  if (req_header.size() < sizeof(CameraGPUAlgoCmdHeader)) {
    LOGF(ERROR) << "Invalid command header";
    callback(EINVAL);
    return;
  }
  if (header->command == CameraGPUAlgoCommand::PORTRAIT_MODE) {
    auto& params = header->params.portrait_mode;
    const uint32_t kChannels = 3;
    size_t buffer_size = params.width * params.height * kChannels;
    base::AutoLock auto_lock(map_lock_);
    if (!base::Contains(shm_region_map_, params.input_buffer_handle) ||
        !base::Contains(shm_region_map_, params.output_buffer_handle) ||
        shm_region_map_.at(params.input_buffer_handle).GetSize() <
            buffer_size ||
        shm_region_map_.at(params.output_buffer_handle).GetSize() <
            buffer_size) {
      LOGF(ERROR) << "Invalid buffer handle";
      callback(EINVAL);
      return;
    }
    const creative_camera::PortraitCrosWrapper::Request portrait_request{
        .width = base::checked_cast<int>(params.width),
        .height = base::checked_cast<int>(params.height),
        .orientation = base::checked_cast<int>(params.orientation),
    };
    base::WritableSharedMemoryMapping input_shm_mapping =
        shm_region_map_.at(params.input_buffer_handle).Map();
    base::WritableSharedMemoryMapping output_shm_mapping =
        shm_region_map_.at(params.output_buffer_handle).Map();
    if (!input_shm_mapping.IsValid() || !output_shm_mapping.IsValid() ||
        !portrait_processor_.Process(
            req_id, portrait_request,
            input_shm_mapping.GetMemoryAs<const uint8_t>(),
            output_shm_mapping.GetMemoryAs<uint8_t>())) {
      LOGF(ERROR) << "Run portrait processor failed";
      callback(EINVAL);
      return;
    }
    callback(0);
  } else {
    LOGF(ERROR) << "Invalid command: "
                << static_cast<std::underlying_type_t<CameraGPUAlgoCommand>>(
                       header->command);
    callback(EINVAL);
  }
  VLOGF_EXIT();
}

static int32_t Initialize(const camera_algorithm_callback_ops_t* callback_ops) {
  return CameraGPUAlgorithm::GetInstance()->Initialize(callback_ops);
}

static int32_t RegisterBuffer(int32_t buffer_fd) {
  return CameraGPUAlgorithm::GetInstance()->RegisterBuffer(buffer_fd);
}

static void Request(uint32_t req_id,
                    const uint8_t req_header[],
                    uint32_t size,
                    int32_t buffer_handle) {
  CameraGPUAlgorithm::GetInstance()->Request(req_id, req_header, size,
                                             buffer_handle);
}

static void DeregisterBuffers(const int32_t buffer_handles[], uint32_t size) {
  CameraGPUAlgorithm::GetInstance()->DeregisterBuffers(buffer_handles, size);
}

}  // namespace cros

extern "C" {
camera_algorithm_ops_t CAMERA_ALGORITHM_MODULE_INFO_SYM CROS_CAMERA_EXPORT = {
    .initialize = cros::Initialize,
    .register_buffer = cros::RegisterBuffer,
    .request = cros::Request,
    .deregister_buffers = cros::DeregisterBuffers};
}
