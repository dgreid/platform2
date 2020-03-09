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
  return base::WrapUnique<JpegDecodeAccelerator>(
      new JpegDecodeAcceleratorImpl());
}

JpegDecodeAcceleratorImpl::JpegDecodeAcceleratorImpl()
    : ipc_thread_("JdaIpcThread"),
      buffer_id_(0),
      camera_metrics_(CameraMetrics::New()) {
  VLOGF_ENTER();

  mojo_channel_manager_ = CameraMojoChannelManager::CreateInstance();
}

JpegDecodeAcceleratorImpl::~JpegDecodeAcceleratorImpl() {
  VLOGF_ENTER();
  if (ipc_thread_.IsRunning()) {
    ipc_thread_.task_runner()->PostTask(
        FROM_HERE, base::Bind(&JpegDecodeAcceleratorImpl::DestroyOnIpcThread,
                              base::Unretained(this)));
    ipc_thread_.Stop();
  }
  VLOGF_EXIT();
}

bool JpegDecodeAcceleratorImpl::Start() {
  VLOGF_ENTER();

  if (!mojo_channel_manager_) {
    return false;
  }

  if (!ipc_thread_.IsRunning()) {
    if (!ipc_thread_.Start()) {
      LOGF(ERROR) << "Failed to start IPC thread";
      return false;
    }
  }

  cancellation_relay_ = std::make_unique<CancellationRelay>();
  auto is_initialized = cros::Future<bool>::Create(cancellation_relay_.get());
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE, base::Bind(&JpegDecodeAcceleratorImpl::InitializeOnIpcThread,
                            base::Unretained(this),
                            cros::GetFutureCallback(is_initialized)));
  if (!is_initialized->Wait()) {
    return false;
  }

  VLOGF_EXIT();

  return is_initialized->Get();
}

void JpegDecodeAcceleratorImpl::InitializeOnIpcThread(
    base::Callback<void(bool)> callback) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();

  if (jda_ptr_.is_bound()) {
    callback.Run(true);
    return;
  }

  auto request = mojo::MakeRequest(&jda_ptr_);

  if (!mojo_channel_manager_->CreateMjpegDecodeAccelerator(
          std::move(request))) {
    callback.Run(false);
    DestroyOnIpcThread();
    return;
  }
  jda_ptr_.set_connection_error_handler(
      base::Bind(&JpegDecodeAcceleratorImpl::OnJpegDecodeAcceleratorError,
                 base::Unretained(this)));

  jda_ptr_->Initialize(callback);
  VLOGF_EXIT();
}

void JpegDecodeAcceleratorImpl::DestroyOnIpcThread() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  jda_ptr_.reset();
  inflight_buffer_ids_.clear();
  input_shm_map_.clear();
  VLOGF_EXIT();
}

void JpegDecodeAcceleratorImpl::OnJpegDecodeAcceleratorError() {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  VLOGF_ENTER();
  LOGF(ERROR) << "There is a mojo error for JpegDecodeAccelerator";
  jda_ptr_.reset();
  inflight_buffer_ids_.clear();
  input_shm_map_.clear();
  cancellation_relay_ = nullptr;
  VLOGF_EXIT();
}

JpegDecodeAccelerator::Error JpegDecodeAcceleratorImpl::DecodeSync(
    int input_fd,
    uint32_t input_buffer_size,
    uint32_t input_buffer_offset,
    buffer_handle_t output_buffer) {
  auto future = cros::Future<int>::Create(cancellation_relay_.get());

  Decode(input_fd, input_buffer_size, input_buffer_offset, output_buffer,
         base::Bind(&JpegDecodeAcceleratorImpl::DecodeSyncCallback,
                    base::Unretained(this), cros::GetFutureCallback(future)));

  if (!future->Wait()) {
    if (!jda_ptr_.is_bound()) {
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

  Decode(input_fd, input_buffer_size, coded_size_width, coded_size_height,
         output_fd, output_buffer_size,
         base::Bind(&JpegDecodeAcceleratorImpl::DecodeSyncCallback,
                    base::Unretained(this), cros::GetFutureCallback(future)));

  if (!future->Wait()) {
    if (!jda_ptr_.is_bound()) {
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

  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&JpegDecodeAcceleratorImpl::DecodeOnIpcThread,
                 base::Unretained(this), buffer_id, input_fd, input_buffer_size,
                 input_buffer_offset, output_buffer, std::move(callback)));
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

  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&JpegDecodeAcceleratorImpl::DecodeOnIpcThreadLegacy,
                 base::Unretained(this), buffer_id, input_fd, input_buffer_size,
                 coded_size_width, coded_size_height, output_fd,
                 output_buffer_size, std::move(callback)));
  return buffer_id;
}

void JpegDecodeAcceleratorImpl::DecodeOnIpcThread(int32_t buffer_id,
                                                  int input_fd,
                                                  uint32_t input_buffer_size,
                                                  uint32_t input_buffer_offset,
                                                  buffer_handle_t output_buffer,
                                                  DecodeCallback callback) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  DCHECK(!base::ContainsKey(inflight_buffer_ids_, buffer_id));

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
      base::BindRepeating(&JpegDecodeAcceleratorImpl::OnDecodeAck,
                          base::Unretained(this), callback, buffer_id));
}

void JpegDecodeAcceleratorImpl::DecodeOnIpcThreadLegacy(
    int32_t buffer_id,
    int input_fd,
    uint32_t input_buffer_size,
    int32_t coded_size_width,
    int32_t coded_size_height,
    int output_fd,
    uint32_t output_buffer_size,
    DecodeCallback callback) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  DCHECK_EQ(input_shm_map_.count(buffer_id), 0);

  if (!jda_ptr_.is_bound()) {
    callback.Run(buffer_id, static_cast<int>(Error::TRY_START_AGAIN));
    return;
  }

  std::unique_ptr<base::SharedMemory> input_shm =
      base::WrapUnique(new base::SharedMemory);
  if (!input_shm->CreateAndMapAnonymous(input_buffer_size)) {
    LOGF(WARNING) << "CreateAndMapAnonymous for input failed, size="
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
  memcpy(input_shm->memory(), mmap_buf, input_buffer_size);
  munmap(mmap_buf, input_buffer_size);

  int dup_input_fd = dup(input_shm->handle().GetHandle());
  int dup_output_fd = dup(output_fd);
  mojo::ScopedHandle input_handle = mojo::WrapPlatformFile(dup_input_fd);
  mojo::ScopedHandle output_handle = mojo::WrapPlatformFile(dup_output_fd);

  input_shm_map_[buffer_id] = std::move(input_shm);
  jda_ptr_->DecodeWithFD(
      buffer_id, std::move(input_handle), input_buffer_size, coded_size_width,
      coded_size_height, std::move(output_handle), output_buffer_size,
      base::BindRepeating(&JpegDecodeAcceleratorImpl::OnDecodeAckLegacy,
                          base::Unretained(this), callback));
}

void JpegDecodeAcceleratorImpl::DecodeSyncCallback(
    base::Callback<void(int)> callback, int32_t buffer_id, int error) {
  callback.Run(error);
}

void JpegDecodeAcceleratorImpl::OnDecodeAck(DecodeCallback callback,
                                            int32_t buffer_id,
                                            cros::mojom::DecodeError error) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  DCHECK(base::ContainsKey(inflight_buffer_ids_, buffer_id));
  inflight_buffer_ids_.erase(buffer_id);
  callback.Run(buffer_id, static_cast<int>(error));
}

void JpegDecodeAcceleratorImpl::OnDecodeAckLegacy(
    DecodeCallback callback,
    int32_t buffer_id,
    cros::mojom::DecodeError error) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  DCHECK_EQ(input_shm_map_.count(buffer_id), 1u);
  input_shm_map_.erase(buffer_id);
  callback.Run(buffer_id, static_cast<int>(error));
}

void JpegDecodeAcceleratorImpl::TestResetJDAChannel() {
  auto future = cros::Future<void>::Create(nullptr);
  ipc_thread_.task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&JpegDecodeAcceleratorImpl::TestResetJDAChannelOnIpcThread,
                 base::Unretained(this), base::RetainedRef(future)));
  future->Wait();
}

void JpegDecodeAcceleratorImpl::TestResetJDAChannelOnIpcThread(
    scoped_refptr<cros::Future<void>> future) {
  DCHECK(ipc_thread_.task_runner()->BelongsToCurrentThread());
  jda_ptr_.reset();
  future->Set();
}

}  // namespace cros
