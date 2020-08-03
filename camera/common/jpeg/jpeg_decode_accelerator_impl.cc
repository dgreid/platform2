/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common/jpeg/jpeg_decode_accelerator_impl.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/logging.h>
#include <base/memory/ptr_util.h>
#include <base/posix/eintr_wrapper.h>
#include <base/run_loop.h>
#include <base/stl_util.h>
#include <base/timer/elapsed_timer.h>
#include <mojo/public/c/system/buffer.h>
#include <mojo/public/cpp/system/buffer.h>
#include <mojo/public/cpp/system/platform_handle.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"
#include "cros-camera/ipc_util.h"

#define STATIC_ASSERT_ENUM(name)                                        \
  static_assert(static_cast<int>(JpegDecodeAccelerator::Error::name) == \
                    static_cast<int>(mojom::DecodeError::name),         \
                "mismatching enum: " #name)

namespace cros {

STATIC_ASSERT_ENUM(NO_ERRORS);
STATIC_ASSERT_ENUM(INVALID_ARGUMENT);
STATIC_ASSERT_ENUM(UNREADABLE_INPUT);
STATIC_ASSERT_ENUM(PARSE_JPEG_FAILED);
STATIC_ASSERT_ENUM(UNSUPPORTED_JPEG);
STATIC_ASSERT_ENUM(PLATFORM_FAILURE);

namespace {

static mojom::VideoPixelFormat V4L2PixelFormatToMojoFormat(
    uint32_t v4l2_format) {
  switch (v4l2_format) {
    case V4L2_PIX_FMT_YUV420:
    case V4L2_PIX_FMT_YUV420M:
      return mojom::VideoPixelFormat::PIXEL_FORMAT_I420;
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV12M:
      return mojom::VideoPixelFormat::PIXEL_FORMAT_NV12;
    default:
      return mojom::VideoPixelFormat::PIXEL_FORMAT_UNKNOWN;
  }
}

}  // namespace

// static
std::unique_ptr<JpegDecodeAccelerator> JpegDecodeAccelerator::CreateInstance() {
  return JpegDecodeAccelerator::CreateInstance(
      CameraMojoChannelManager::GetInstance());
}

// static
std::unique_ptr<JpegDecodeAccelerator> JpegDecodeAccelerator::CreateInstance(
    CameraMojoChannelManager* mojo_manager) {
  return base::WrapUnique<JpegDecodeAccelerator>(
      new JpegDecodeAcceleratorImpl(mojo_manager));
}

JpegDecodeAcceleratorImpl::JpegDecodeAcceleratorImpl(
    CameraMojoChannelManager* mojo_manager)
    : buffer_id_(0),
      mojo_manager_(mojo_manager),
      cancellation_relay_(new CancellationRelay),
      ipc_bridge_(new IPCBridge(mojo_manager, cancellation_relay_.get())),
      camera_metrics_(CameraMetrics::New()) {
  VLOGF_ENTER();
}

JpegDecodeAcceleratorImpl::~JpegDecodeAcceleratorImpl() {
  VLOGF_ENTER();

  bool result = mojo_manager_->GetIpcTaskRunner()->DeleteSoon(
      FROM_HERE, std::move(ipc_bridge_));
  DCHECK(result);
  cancellation_relay_ = nullptr;

  VLOGF_EXIT();
}

bool JpegDecodeAcceleratorImpl::Start() {
  VLOGF_ENTER();

  auto is_initialized = cros::Future<bool>::Create(cancellation_relay_.get());

  mojo_manager_->GetIpcTaskRunner()->PostTask(
      FROM_HERE, base::Bind(&JpegDecodeAcceleratorImpl::IPCBridge::Start,
                            ipc_bridge_->GetWeakPtr(),
                            cros::GetFutureCallback(is_initialized)));
  if (!is_initialized->Wait()) {
    return false;
  }

  VLOGF_EXIT();

  return is_initialized->Get();
}

JpegDecodeAccelerator::Error JpegDecodeAcceleratorImpl::DecodeSync(
    int input_fd,
    uint32_t input_buffer_size,
    uint32_t input_buffer_offset,
    buffer_handle_t output_buffer) {
  auto future = cros::Future<int>::Create(cancellation_relay_.get());

  Decode(
      input_fd, input_buffer_size, input_buffer_offset, output_buffer,
      base::Bind(&JpegDecodeAcceleratorImpl::IPCBridge::DecodeSyncCallback,
                 ipc_bridge_->GetWeakPtr(), cros::GetFutureCallback(future)));

  if (!future->Wait()) {
    if (!ipc_bridge_->IsReady()) {
      LOGF(WARNING) << "There may be an mojo channel error.";
      return Error::TRY_START_AGAIN;
    }
    LOGF(WARNING) << "There is no decode response from JDA mojo channel.";
    return Error::NO_DECODE_RESPONSE;
  }
  VLOGF_EXIT();
  return static_cast<Error>(future->Get());
}

JpegDecodeAccelerator::Error JpegDecodeAcceleratorImpl::DecodeSync(
    int input_fd,
    uint32_t input_buffer_size,
    int32_t coded_size_width,
    int32_t coded_size_height,
    int output_fd,
    uint32_t output_buffer_size) {
  auto future = cros::Future<int>::Create(cancellation_relay_.get());

  base::ElapsedTimer timer;

  Decode(
      input_fd, input_buffer_size, coded_size_width, coded_size_height,
      output_fd, output_buffer_size,
      base::Bind(&JpegDecodeAcceleratorImpl::IPCBridge::DecodeSyncCallback,
                 ipc_bridge_->GetWeakPtr(), cros::GetFutureCallback(future)));

  if (!future->Wait()) {
    if (!ipc_bridge_->IsReady()) {
      LOGF(WARNING) << "There may be an mojo channel error.";
      return Error::TRY_START_AGAIN;
    }
    LOGF(WARNING) << "There is no decode response from JDA mojo channel.";
    return Error::NO_DECODE_RESPONSE;
  }
  camera_metrics_->SendJpegProcessLatency(
      JpegProcessType::kDecode, JpegProcessMethod::kHardware, timer.Elapsed());
  camera_metrics_->SendJpegResolution(JpegProcessType::kDecode,
                                      JpegProcessMethod::kHardware,
                                      coded_size_width, coded_size_height);

  VLOGF_EXIT();
  return static_cast<Error>(future->Get());
}

int32_t JpegDecodeAcceleratorImpl::Decode(int input_fd,
                                          uint32_t input_buffer_size,
                                          uint32_t input_buffer_offset,
                                          buffer_handle_t output_buffer,
                                          DecodeCallback callback) {
  int32_t buffer_id = buffer_id_;

  // Mask against 30 bits, to avoid (undefined) wraparound on signed integer.
  buffer_id_ = (buffer_id_ + 1) & 0x3FFFFFFF;

  mojo_manager_->GetIpcTaskRunner()->PostTask(
      FROM_HERE, base::Bind(&JpegDecodeAcceleratorImpl::IPCBridge::Decode,
                            ipc_bridge_->GetWeakPtr(), buffer_id, input_fd,
                            input_buffer_size, input_buffer_offset,
                            output_buffer, std::move(callback)));
  return buffer_id;
}

int32_t JpegDecodeAcceleratorImpl::Decode(int input_fd,
                                          uint32_t input_buffer_size,
                                          int32_t coded_size_width,
                                          int32_t coded_size_height,
                                          int output_fd,
                                          uint32_t output_buffer_size,
                                          DecodeCallback callback) {
  int32_t buffer_id = buffer_id_;

  // Mask against 30 bits, to avoid (undefined) wraparound on signed integer.
  buffer_id_ = (buffer_id_ + 1) & 0x3FFFFFFF;

  mojo_manager_->GetIpcTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&JpegDecodeAcceleratorImpl::IPCBridge::DecodeLegacy,
                 ipc_bridge_->GetWeakPtr(), buffer_id, input_fd,
                 input_buffer_size, coded_size_width, coded_size_height,
                 output_fd, output_buffer_size, std::move(callback)));
  return buffer_id;
}

JpegDecodeAcceleratorImpl::IPCBridge::IPCBridge(
    CameraMojoChannelManager* mojo_manager,
    CancellationRelay* cancellation_relay)
    : mojo_manager_(mojo_manager),
      cancellation_relay_(cancellation_relay),
      ipc_task_runner_(mojo_manager_->GetIpcTaskRunner()) {}

JpegDecodeAcceleratorImpl::IPCBridge::~IPCBridge() {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();

  Destroy();
}

void JpegDecodeAcceleratorImpl::IPCBridge::Start(
    base::Callback<void(bool)> callback) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();

  if (jda_ptr_.is_bound()) {
    std::move(callback).Run(true);
    return;
  }

  auto request = mojo::MakeRequest(&jda_ptr_);
  jda_ptr_.set_connection_error_handler(base::Bind(
      &JpegDecodeAcceleratorImpl::IPCBridge::OnJpegDecodeAcceleratorError,
      GetWeakPtr()));
  mojo_manager_->CreateMjpegDecodeAccelerator(
      std::move(request),
      base::Bind(&JpegDecodeAcceleratorImpl::IPCBridge::Initialize,
                 GetWeakPtr(), std::move(callback)),
      base::Bind(
          &JpegDecodeAcceleratorImpl::IPCBridge::OnJpegDecodeAcceleratorError,
          GetWeakPtr()));
  VLOGF_EXIT();
}

void JpegDecodeAcceleratorImpl::IPCBridge::Destroy() {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();
  jda_ptr_.reset();
  inflight_buffer_ids_.clear();
}

void JpegDecodeAcceleratorImpl::IPCBridge::Decode(int32_t buffer_id,
                                                  int input_fd,
                                                  uint32_t input_buffer_size,
                                                  uint32_t input_buffer_offset,
                                                  buffer_handle_t output_buffer,
                                                  DecodeCallback callback) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  DCHECK(!base::Contains(inflight_buffer_ids_, buffer_id));

  if (!jda_ptr_.is_bound()) {
    callback.Run(buffer_id, static_cast<int>(Error::TRY_START_AGAIN));
    return;
  }

  // Wrap output buffer into mojom::DmaBufVideoFrame.
  CameraBufferManager* buffer_manager = CameraBufferManager::GetInstance();
  mojom::VideoPixelFormat mojo_format = V4L2PixelFormatToMojoFormat(
      buffer_manager->GetV4L2PixelFormat(output_buffer));
  if (mojo_format == mojom::VideoPixelFormat::PIXEL_FORMAT_UNKNOWN) {
    callback.Run(buffer_id, static_cast<int>(Error::INVALID_ARGUMENT));
    return;
  }
  const uint32_t num_planes = buffer_manager->GetNumPlanes(output_buffer);
  std::vector<mojom::DmaBufPlanePtr> planes(num_planes);
  for (uint32_t i = 0; i < num_planes; ++i) {
    mojo::ScopedHandle fd_handle =
        mojo::WrapPlatformFile(HANDLE_EINTR(dup(output_buffer->data[i])));
    const int32_t stride = base::checked_cast<int32_t>(
        buffer_manager->GetPlaneStride(output_buffer, i));
    const uint32_t offset = base::checked_cast<uint32_t>(
        buffer_manager->GetPlaneOffset(output_buffer, i));
    const uint32_t size = base::checked_cast<uint32_t>(
        buffer_manager->GetPlaneSize(output_buffer, i));
    planes[i] =
        mojom::DmaBufPlane::New(std::move(fd_handle), stride, offset, size);
  }
  auto output_frame = mojom::DmaBufVideoFrame::New(
      mojo_format, buffer_manager->GetWidth(output_buffer),
      buffer_manager->GetHeight(output_buffer), std::move(planes));

  mojo::ScopedHandle input_handle =
      mojo::WrapPlatformFile(HANDLE_EINTR(dup(input_fd)));

  inflight_buffer_ids_.insert(buffer_id);
  jda_ptr_->DecodeWithDmaBuf(
      buffer_id, std::move(input_handle), input_buffer_size,
      input_buffer_offset, std::move(output_frame),
      base::BindRepeating(&JpegDecodeAcceleratorImpl::IPCBridge::OnDecodeAck,
                          GetWeakPtr(), callback, buffer_id));
}

void JpegDecodeAcceleratorImpl::IPCBridge::DecodeLegacy(
    int32_t buffer_id,
    int input_fd,
    uint32_t input_buffer_size,
    int32_t coded_size_width,
    int32_t coded_size_height,
    int output_fd,
    uint32_t output_buffer_size,
    DecodeCallback callback) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());

  if (!jda_ptr_.is_bound()) {
    callback.Run(buffer_id, static_cast<int>(Error::TRY_START_AGAIN));
    return;
  }

  base::WritableSharedMemoryRegion input_shm_region =
      base::WritableSharedMemoryRegion::Create(input_buffer_size);
  if (!input_shm_region.IsValid()) {
    LOGF(WARNING) << "Create shared memory region for input failed, size="
                  << input_buffer_size;
    callback.Run(buffer_id,
                 static_cast<int>(Error::CREATE_SHARED_MEMORY_FAILED));
    return;
  }
  base::WritableSharedMemoryMapping input_shm_mapping = input_shm_region.Map();
  if (!input_shm_mapping.IsValid()) {
    LOGF(WARNING) << "Create mapping for input failed, size="
                  << input_buffer_size;
    callback.Run(buffer_id,
                 static_cast<int>(Error::CREATE_SHARED_MEMORY_FAILED));
    return;
  }
  // Copy content from input file descriptor to shared memory.
  uint8_t* mmap_buf = static_cast<uint8_t*>(
      mmap(NULL, input_buffer_size, PROT_READ, MAP_SHARED, input_fd, 0));

  if (mmap_buf == MAP_FAILED) {
    LOGF(WARNING) << "MMAP for input_fd:" << input_fd << " Failed.";
    callback.Run(buffer_id, static_cast<int>(Error::MMAP_FAILED));
    return;
  }
  memcpy(input_shm_mapping.memory(), mmap_buf, input_buffer_size);
  munmap(mmap_buf, input_buffer_size);

  base::subtle::PlatformSharedMemoryRegion input_platform_shm =
      base::WritableSharedMemoryRegion::TakeHandleForSerialization(
          std::move(input_shm_region));
  mojo::ScopedHandle input_handle = mojo::WrapPlatformFile(
      input_platform_shm.PassPlatformHandle().fd.release());

  int dup_output_fd = HANDLE_EINTR(dup(output_fd));
  mojo::ScopedHandle output_handle = mojo::WrapPlatformFile(dup_output_fd);

  jda_ptr_->DecodeWithFD(
      buffer_id, std::move(input_handle), input_buffer_size, coded_size_width,
      coded_size_height, std::move(output_handle), output_buffer_size,
      base::BindRepeating(
          &JpegDecodeAcceleratorImpl::IPCBridge::OnDecodeAckLegacy,
          GetWeakPtr(), callback));
}

void JpegDecodeAcceleratorImpl::IPCBridge::DecodeSyncCallback(
    base::Callback<void(int)> callback, int32_t buffer_id, int error) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  callback.Run(error);
}

void JpegDecodeAcceleratorImpl::IPCBridge::TestResetJDAChannel(
    scoped_refptr<cros::Future<void>> future) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  jda_ptr_.reset();
  future->Set();
}

base::WeakPtr<JpegDecodeAcceleratorImpl::IPCBridge>
JpegDecodeAcceleratorImpl::IPCBridge::GetWeakPtr() {
  return weak_ptr_factory_.GetWeakPtr();
}

bool JpegDecodeAcceleratorImpl::IPCBridge::IsReady() {
  return jda_ptr_.is_bound();
}

void JpegDecodeAcceleratorImpl::IPCBridge::Initialize(
    base::Callback<void(bool)> callback) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();

  jda_ptr_->Initialize(std::move(callback));
}

void JpegDecodeAcceleratorImpl::IPCBridge::OnJpegDecodeAcceleratorError() {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  VLOGF_ENTER();
  LOGF(ERROR) << "There is a mojo error for JpegDecodeAccelerator";
  cancellation_relay_->CancelAllFutures();
  Destroy();
  VLOGF_EXIT();
}

void JpegDecodeAcceleratorImpl::IPCBridge::OnDecodeAck(
    DecodeCallback callback,
    int32_t buffer_id,
    cros::mojom::DecodeError error) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  DCHECK(base::Contains(inflight_buffer_ids_, buffer_id));
  inflight_buffer_ids_.erase(buffer_id);
  callback.Run(buffer_id, static_cast<int>(error));
}

void JpegDecodeAcceleratorImpl::IPCBridge::OnDecodeAckLegacy(
    DecodeCallback callback,
    int32_t buffer_id,
    cros::mojom::DecodeError error) {
  DCHECK(ipc_task_runner_->BelongsToCurrentThread());
  callback.Run(buffer_id, static_cast<int>(error));
}

void JpegDecodeAcceleratorImpl::TestResetJDAChannel() {
  auto future = cros::Future<void>::Create(nullptr);

  mojo_manager_->GetIpcTaskRunner()->PostTask(
      FROM_HERE,
      base::Bind(&JpegDecodeAcceleratorImpl::IPCBridge::TestResetJDAChannel,
                 ipc_bridge_->GetWeakPtr(), base::RetainedRef(future)));
  future->Wait();
}

}  // namespace cros
