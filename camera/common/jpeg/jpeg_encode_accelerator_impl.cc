/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/jpeg/jpeg_encode_accelerator_impl.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <utility>

#include <algorithm>

#include <base/bind.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/posix/eintr_wrapper.h>
#include <base/run_loop.h>
#include <mojo/public/c/system/buffer.h>
#include <mojo/public/cpp/system/buffer.h>
#include <mojo/public/cpp/system/platform_handle.h>

#include "cros-camera/common.h"
#include "cros-camera/future.h"
#include "cros-camera/ipc_util.h"

#define STATIC_ASSERT_ENUM(name)                                 \
  static_assert(static_cast<int>(JpegEncodeAccelerator::name) == \
                    static_cast<int>(mojom::EncodeStatus::name), \
                "mismatching enum: " #name)

namespace cros {

STATIC_ASSERT_ENUM(ENCODE_OK);
STATIC_ASSERT_ENUM(HW_JPEG_ENCODE_NOT_SUPPORTED);
STATIC_ASSERT_ENUM(THREAD_CREATION_FAILED);
STATIC_ASSERT_ENUM(INVALID_ARGUMENT);
STATIC_ASSERT_ENUM(INACCESSIBLE_OUTPUT_BUFFER);
STATIC_ASSERT_ENUM(PARSE_IMAGE_FAILED);
STATIC_ASSERT_ENUM(PLATFORM_FAILURE);

std::unique_ptr<JpegEncodeAccelerator> JpegEncodeAccelerator::CreateInstance() {
  return base::WrapUnique<JpegEncodeAccelerator>(
      new JpegEncodeAcceleratorImpl());
}

JpegEncodeAcceleratorImpl::JpegEncodeAcceleratorImpl()
    : ipc_thread_("JeaIpcThread"), task_id_(0) {
  VLOGF_ENTER();

  mojo_channel_manager_ = CameraMojoChannelManager::CreateInstance();
}

JpegEncodeAcceleratorImpl::~JpegEncodeAcceleratorImpl() {
  VLOGF_ENTER();
  if (ipc_thread_.IsRunning()) {
    ipc_thread_.task_runner()->PostTask(
        FROM_HERE, base::Bind(&JpegEncodeAcceleratorImpl::DestroyOnIpcThread,
                              base::Unretained(this)));
    ipc_thread_.Stop();
  }
  VLOGF_EXIT();
}

bool JpegEncodeAcceleratorImpl::Start() {
  VLOGF_ENTER();

  if (!ipc_thread_.IsRunning()) {
    if (!ipc_thread_.Start()) {
      LOGF(ERROR) << "Failed to start IPC thread";
      return false;
    }
  }

  cancellation_relay_ = std::make_unique<CancellationRelay>();
  auto is_initialized = Future<bool>::Create(cancellation_relay_.get());
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&JpegEncodeAcceleratorImpl::InitializeOnIpcThread,
                 base::Unretained(this), GetFutureCallback(is_initialized)));
  if (!is_initialized->Wait()) {
    return false;
  }

  VLOGF_EXIT();

  return is_initialized->Get();
}

void JpegEncodeAcceleratorImpl::InitializeOnIpcThread(
    base::Callback<void(bool)> callback) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();

  if (jea_ptr_.is_bound()) {
    callback.Run(true);
    return;
  }

  auto request = mojo::MakeRequest(&jea_ptr_);

  if (!mojo_channel_manager_->CreateJpegEncodeAccelerator(std::move(request))) {
    callback.Run(false);
    DestroyOnIpcThread();
    return;
  }

  jea_ptr_.set_connection_error_handler(
      base::Bind(&JpegEncodeAcceleratorImpl::OnJpegEncodeAcceleratorError,
                 base::Unretained(this)));

  jea_ptr_->Initialize(callback);
  VLOGF_EXIT();
}

void JpegEncodeAcceleratorImpl::DestroyOnIpcThread() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  jea_ptr_.reset();
  input_shm_map_.clear();
  exif_shm_map_.clear();
  cancellation_relay_ = nullptr;
  VLOGF_EXIT();
}

void JpegEncodeAcceleratorImpl::OnJpegEncodeAcceleratorError() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  LOGF(ERROR) << "There is a mojo error for JpegEncodeAccelerator";
  VLOGF_EXIT();
  DestroyOnIpcThread();
}

int JpegEncodeAcceleratorImpl::EncodeSync(int input_fd,
                                          const uint8_t* input_buffer,
                                          uint32_t input_buffer_size,
                                          int32_t coded_size_width,
                                          int32_t coded_size_height,
                                          const uint8_t* exif_buffer,
                                          uint32_t exif_buffer_size,
                                          int output_fd,
                                          uint32_t output_buffer_size,
                                          uint32_t* output_data_size) {
  int32_t task_id = task_id_;
  // Mask against 30 bits, to avoid (undefined) wraparound on signed integer.
  task_id_ = (task_id_ + 1) & 0x3FFFFFFF;

  auto future = Future<int>::Create(cancellation_relay_.get());
  auto callback = base::Bind(&JpegEncodeAcceleratorImpl::EncodeSyncCallback,
                             base::Unretained(this), GetFutureCallback(future),
                             output_data_size, task_id);
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&JpegEncodeAcceleratorImpl::EncodeOnIpcThreadLegacy,
                 base::Unretained(this), task_id, input_fd, input_buffer,
                 input_buffer_size, coded_size_width, coded_size_height,
                 exif_buffer, exif_buffer_size, output_fd, output_buffer_size,
                 std::move(callback)));

  if (!future->Wait()) {
    if (!jea_ptr_.is_bound()) {
      LOGF(WARNING) << "There may be an mojo channel error.";
      return TRY_START_AGAIN;
    }
    LOGF(WARNING) << "There is no encode response from JEA mojo channel.";
    return NO_ENCODE_RESPONSE;
  }
  VLOGF_EXIT();
  return future->Get();
}

int JpegEncodeAcceleratorImpl::EncodeSync(
    uint32_t input_format,
    const std::vector<JpegCompressor::DmaBufPlane>& input_planes,
    const std::vector<JpegCompressor::DmaBufPlane>& output_planes,
    const uint8_t* exif_buffer,
    uint32_t exif_buffer_size,
    int coded_size_width,
    int coded_size_height,
    uint32_t* output_data_size) {
  int32_t task_id = task_id_;
  // Mask against 30 bits, to avoid (undefined) wraparound on signed integer.
  task_id_ = (task_id_ + 1) & 0x3FFFFFFF;

  auto future = Future<int>::Create(cancellation_relay_.get());
  auto callback = base::Bind(&JpegEncodeAcceleratorImpl::EncodeSyncCallback,
                             base::Unretained(this), GetFutureCallback(future),
                             output_data_size, task_id);

  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&JpegEncodeAcceleratorImpl::EncodeOnIpcThread,
                            base::Unretained(this), task_id, input_format,
                            std::move(input_planes), std::move(output_planes),
                            exif_buffer, exif_buffer_size, coded_size_width,
                            coded_size_height, std::move(callback)));

  if (!future->Wait()) {
    if (!jea_ptr_.is_bound()) {
      LOGF(WARNING) << "There may be an mojo channel error.";
      return TRY_START_AGAIN;
    }
    LOGF(WARNING) << "There is no encode response from JEA mojo channel.";
    return NO_ENCODE_RESPONSE;
  }
  VLOGF_EXIT();
  return future->Get();
}

void JpegEncodeAcceleratorImpl::EncodeOnIpcThreadLegacy(
    int32_t task_id,
    int input_fd,
    const uint8_t* input_buffer,
    uint32_t input_buffer_size,
    int32_t coded_size_width,
    int32_t coded_size_height,
    const uint8_t* exif_buffer,
    uint32_t exif_buffer_size,
    int output_fd,
    uint32_t output_buffer_size,
    EncodeWithFDCallback callback) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  DCHECK_EQ(input_shm_map_.count(task_id), 0);
  DCHECK_EQ(exif_shm_map_.count(task_id), 0);

  if (!jea_ptr_.is_bound()) {
    callback.Run(0, TRY_START_AGAIN);
  }

  std::unique_ptr<base::SharedMemory> input_shm =
      base::WrapUnique(new base::SharedMemory);
  if (!input_shm->CreateAndMapAnonymous(input_buffer_size)) {
    LOGF(WARNING) << "CreateAndMapAnonymous for input failed, size="
                  << input_buffer_size;
    callback.Run(0, SHARED_MEMORY_FAIL);
    return;
  }
  // Copy content from input buffer or file descriptor to shared memory.
  if (input_buffer) {
    memcpy(input_shm->memory(), input_buffer, input_buffer_size);
  } else {
    uint8_t* mmap_buf = static_cast<uint8_t*>(
        mmap(NULL, input_buffer_size, PROT_READ, MAP_SHARED, input_fd, 0));

    if (mmap_buf == MAP_FAILED) {
      LOGF(WARNING) << "MMAP for input_fd:" << input_fd << " Failed.";
      callback.Run(0, MMAP_FAIL);
      return;
    }

    memcpy(input_shm->memory(), mmap_buf, input_buffer_size);
    munmap(mmap_buf, input_buffer_size);
  }

  // Create SharedMemory for Exif buffer and copy data into it.
  std::unique_ptr<base::SharedMemory> exif_shm =
      base::WrapUnique(new base::SharedMemory);
  // Create a dummy |exif_shm| even if |exif_buffer_size| is 0.
  uint32_t exif_shm_size = std::max(exif_buffer_size, 1u);
  if (!exif_shm->CreateAndMapAnonymous(exif_shm_size)) {
    LOGF(WARNING) << "CreateAndMapAnonymous for exif failed, size="
                  << exif_shm_size;
    callback.Run(0, SHARED_MEMORY_FAIL);
    return;
  }
  if (exif_buffer_size) {
    memcpy(exif_shm->memory(), exif_buffer, exif_buffer_size);
  }

  int dup_input_fd = HANDLE_EINTR(dup(input_shm->handle().GetHandle()));
  int dup_exif_fd = HANDLE_EINTR(dup(exif_shm->handle().GetHandle()));
  int dup_output_fd = HANDLE_EINTR(dup(output_fd));

  mojo::ScopedHandle input_handle = mojo::WrapPlatformFile(dup_input_fd);
  mojo::ScopedHandle exif_handle = mojo::WrapPlatformFile(dup_exif_fd);
  mojo::ScopedHandle output_handle = mojo::WrapPlatformFile(dup_output_fd);

  input_shm_map_[task_id] = std::move(input_shm);
  exif_shm_map_[task_id] = std::move(exif_shm);

  jea_ptr_->EncodeWithFD(task_id, std::move(input_handle), input_buffer_size,
                         coded_size_width, coded_size_height,
                         std::move(exif_handle), exif_buffer_size,
                         std::move(output_handle), output_buffer_size,
                         base::Bind(&JpegEncodeAcceleratorImpl::OnEncodeAck,
                                    base::Unretained(this), callback));
}

void JpegEncodeAcceleratorImpl::EncodeOnIpcThread(
    int32_t task_id,
    uint32_t input_format,
    const std::vector<JpegCompressor::DmaBufPlane>& input_planes,
    const std::vector<JpegCompressor::DmaBufPlane>& output_planes,
    const uint8_t* exif_buffer,
    uint32_t exif_buffer_size,
    int coded_size_width,
    int coded_size_height,
    EncodeWithDmaBufCallback callback) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  DCHECK_EQ(exif_shm_map_.count(task_id), 0);

  if (!jea_ptr_.is_bound()) {
    callback.Run(0, TRY_START_AGAIN);
  }

  // Create SharedMemory for Exif buffer and copy data into it.
  std::unique_ptr<base::SharedMemory> exif_shm =
      base::WrapUnique(new base::SharedMemory);
  // Create a dummy |exif_shm| even if |exif_buffer_size| is 0.
  uint32_t exif_shm_size = std::max(exif_buffer_size, 1u);
  if (!exif_shm->CreateAndMapAnonymous(exif_shm_size)) {
    LOGF(WARNING) << "CreateAndMapAnonymous for exif failed, size="
                  << exif_shm_size;
    callback.Run(0, SHARED_MEMORY_FAIL);
    return;
  }
  if (exif_buffer_size) {
    memcpy(exif_shm->memory(), exif_buffer, exif_buffer_size);
  }

  int dup_exif_fd = HANDLE_EINTR(dup(exif_shm->handle().GetHandle()));
  mojo::ScopedHandle exif_handle = mojo::WrapPlatformFile(dup_exif_fd);

  auto WrapToMojoPlanes =
      [](const std::vector<JpegCompressor::DmaBufPlane>& planes) {
        std::vector<cros::mojom::DmaBufPlanePtr> mojo_planes;
        for (auto plane : planes) {
          auto mojo_plane = cros::mojom::DmaBufPlane::New();
          mojo_plane->fd_handle =
              mojo::WrapPlatformFile(HANDLE_EINTR(dup(plane.fd)));
          mojo_plane->stride = plane.stride;
          mojo_plane->offset = plane.offset;
          mojo_plane->size = plane.size;
          mojo_planes.push_back(std::move(mojo_plane));
        }
        return mojo_planes;
      };

  std::vector<cros::mojom::DmaBufPlanePtr> mojo_input_planes =
      WrapToMojoPlanes(input_planes);
  std::vector<cros::mojom::DmaBufPlanePtr> mojo_output_planes =
      WrapToMojoPlanes(output_planes);

  jea_ptr_->EncodeWithDmaBuf(
      task_id, input_format, std::move(mojo_input_planes),
      std::move(mojo_output_planes), std::move(exif_handle), exif_buffer_size,
      coded_size_width, coded_size_height,
      base::Bind(&JpegEncodeAcceleratorImpl::OnEncodeDmaBufAck,
                 base::Unretained(this), callback));
}

void JpegEncodeAcceleratorImpl::EncodeSyncCallback(
    base::Callback<void(int)> callback,
    uint32_t* output_data_size,
    int32_t task_id,
    uint32_t output_size,
    int status) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  *output_data_size = output_size;
  callback.Run(status);
}

void JpegEncodeAcceleratorImpl::OnEncodeAck(EncodeWithFDCallback callback,
                                            int32_t task_id,
                                            uint32_t output_size,
                                            mojom::EncodeStatus status) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  DCHECK_EQ(input_shm_map_.count(task_id), 1u);
  DCHECK_EQ(exif_shm_map_.count(task_id), 1u);
  input_shm_map_.erase(task_id);
  exif_shm_map_.erase(task_id);
  callback.Run(output_size, static_cast<int>(status));
}

void JpegEncodeAcceleratorImpl::OnEncodeDmaBufAck(
    EncodeWithDmaBufCallback callback,
    uint32_t output_size,
    mojom::EncodeStatus status) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  callback.Run(output_size, static_cast<int>(status));
}

}  // namespace cros
