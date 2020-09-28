/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_COMMON_UTILS_CROS_CAMERA_MOJO_UTILS_H_
#define CAMERA_COMMON_UTILS_CROS_CAMERA_MOJO_UTILS_H_

#include <map>
#include <memory>
#include <unordered_map>
#include <utility>

#include <hardware/camera3.h>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <base/single_thread_task_runner.h>
#include <base/synchronization/lock.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "common/utils/common_types.h"
#include "cros-camera/common.h"
#include "cros-camera/future.h"
#include "mojo/camera3.mojom.h"

namespace cros {
namespace internal {

// Serialize / deserialize helper functions.

// SerializeStreamBuffer is used in CameraDeviceAdapter::ProcessCaptureResult to
// pass a result buffer handle through Mojo.  For the input / output buffers, we
// do not need to serialize the whole native handle but instead we can simply
// return their corresponding handle IDs.  When the receiver gets the result it
// will restore using the handle ID the original buffer handles which were
// passed down when the frameworks called process_capture_request.
cros::mojom::Camera3StreamBufferPtr SerializeStreamBuffer(
    const camera3_stream_buffer_t* buffer,
    const ScopedStreams& streams,
    const std::unordered_map<uint64_t, std::unique_ptr<camera_buffer_handle_t>>&
        buffer_handles);

int DeserializeStreamBuffer(
    const cros::mojom::Camera3StreamBufferPtr& ptr,
    const ScopedStreams& streams,
    const std::unordered_map<uint64_t, std::unique_ptr<camera_buffer_handle_t>>&
        buffer_handles,
    camera3_stream_buffer_t* buffer);

cros::mojom::CameraMetadataPtr SerializeCameraMetadata(
    const camera_metadata_t* metadata);

ScopedCameraMetadata DeserializeCameraMetadata(
    const cros::mojom::CameraMetadataPtr& metadata);
// Template classes for Mojo IPC delegates

// A wrapper around a mojo::InterfacePtr<T>.  This template class represents a
// Mojo communication channel to a remote Mojo binding implementation of T.
template <typename InterfacePtrType>
class MojoChannelBase
    : public base::SupportsWeakPtr<MojoChannelBase<InterfacePtrType>> {
 public:
  using Self = MojoChannelBase<InterfacePtrType>;
  explicit MojoChannelBase(
      scoped_refptr<base::SingleThreadTaskRunner> task_runner)
      : task_runner_(task_runner) {
    VLOGF_ENTER();
  }

  void Bind(typename InterfacePtrType::PtrInfoType interface_ptr_info,
            const base::Closure& connection_error_handler) {
    VLOGF_ENTER();
    task_runner_->PostTask(
        FROM_HERE, base::Bind(&Self::BindOnThread, base::AsWeakPtr(this),
                              base::Passed(&interface_ptr_info),
                              connection_error_handler));
  }

  ~MojoChannelBase() {
    VLOGF_ENTER();
    // We need to wait for ResetInterfacePtrOnThread to finish before return
    // otherwise it would cause race condition in destruction of
    // |interface_ptr_| and may CHECK.
    auto future = cros::Future<void>::Create(nullptr);
    if (task_runner_->BelongsToCurrentThread()) {
      ResetInterfacePtrOnThread(cros::GetFutureCallback(future));
    } else {
      task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&Self::ResetInterfacePtrOnThread, base::AsWeakPtr(this),
                     cros::GetFutureCallback(future)));
    }
    future->Wait();
  }

 protected:
  // All the Mojo communication happens on |task_runner_|.
  const scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  InterfacePtrType interface_ptr_;

  // For the future objects in derived class.
  CancellationRelay relay_;

 private:
  void BindOnThread(typename InterfacePtrType::PtrInfoType interface_ptr_info,
                    const base::Closure& connection_error_handler) {
    VLOGF_ENTER();
    DCHECK(task_runner_->BelongsToCurrentThread());
    interface_ptr_.Bind(std::move(interface_ptr_info));
    if (!interface_ptr_.is_bound()) {
      LOGF(ERROR) << "Failed to bind interface_ptr_";
      return;
    }
    interface_ptr_.set_connection_error_handler(connection_error_handler);
    interface_ptr_.QueryVersion(
        base::Bind(&Self::OnQueryVersionOnThread, base::AsWeakPtr(this)));
  }

  void OnQueryVersionOnThread(uint32_t version) {
    VLOGF_ENTER();
    DCHECK(task_runner_->BelongsToCurrentThread());
    LOGF(INFO) << "Bridge ready (version=" << version << ")";
  }

  void ResetInterfacePtrOnThread(const base::Closure& callback) {
    VLOGF_ENTER();
    DCHECK(task_runner_->BelongsToCurrentThread());
    interface_ptr_.reset();
    callback.Run();
  }

  DISALLOW_IMPLICIT_CONSTRUCTORS(MojoChannelBase);
};

template <typename T>
using MojoChannel = MojoChannelBase<mojo::InterfacePtr<T>>;

template <typename T>
using MojoAssociatedChannel = MojoChannelBase<mojo::AssociatedInterfacePtr<T>>;

// A wrapper around a mojo::Binding<T>.  This template class represents an
// implementation of Mojo interface T.
template <typename T>
class MojoBinding : public T {
 public:
  explicit MojoBinding(scoped_refptr<base::SingleThreadTaskRunner> task_runner)
      : task_runner_(task_runner), binding_(this), weak_ptr_factory_(this) {
    VLOGF_ENTER();
  }

  ~MojoBinding() {
    VLOGF_ENTER();
    // We need to wait for CloseBindingOnThread to finish before return
    // otherwise it would cause race condition in destruction of |binding_| and
    // may CHECK.
    auto future = cros::Future<void>::Create(nullptr);
    if (task_runner_->BelongsToCurrentThread()) {
      CloseBindingOnThread(cros::GetFutureCallback(future));
    } else {
      task_runner_->PostTask(FROM_HERE,
                             base::Bind(&MojoBinding<T>::CloseBindingOnThread,
                                        weak_ptr_factory_.GetWeakPtr(),
                                        cros::GetFutureCallback(future)));
    }
    future->Wait();
  }

  mojo::InterfacePtr<T> CreateInterfacePtr(
      const base::Closure& connection_error_handler) {
    VLOGF_ENTER();
    auto future = cros::Future<mojo::InterfacePtr<T>>::Create(nullptr);
    if (task_runner_->BelongsToCurrentThread()) {
      CreateInterfacePtrOnThread(connection_error_handler,
                                 cros::GetFutureCallback(future));
    } else {
      task_runner_->PostTask(
          FROM_HERE,
          base::Bind(&MojoBinding<T>::CreateInterfacePtrOnThread,
                     weak_ptr_factory_.GetWeakPtr(), connection_error_handler,
                     cros::GetFutureCallback(future)));
    }
    return future->Get();
  }

  void Bind(mojo::ScopedMessagePipeHandle handle,
            const base::Closure& connection_error_handler) {
    VLOGF_ENTER();
    task_runner_->PostTask(
        FROM_HERE, base::Bind(&MojoBinding<T>::BindOnThread,
                              weak_ptr_factory_.GetWeakPtr(),
                              base::Passed(&handle), connection_error_handler));
  }

 protected:
  // All the methods of T that this class implements run on |task_runner_|.
  const scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

 private:
  void CloseBindingOnThread(const base::Closure& callback) {
    VLOGF_ENTER();
    DCHECK(task_runner_->BelongsToCurrentThread());
    if (binding_.is_bound()) {
      binding_.Close();
    }
    callback.Run();
  }

  void CreateInterfacePtrOnThread(
      const base::Closure& connection_error_handler,
      const base::Callback<void(mojo::InterfacePtr<T>)>& cb) {
    // Call CreateInterfacePtrAndBind() on thread_ to serve the RPC.
    VLOGF_ENTER();
    DCHECK(task_runner_->BelongsToCurrentThread());
    mojo::InterfacePtr<T> interfacePtr = binding_.CreateInterfacePtrAndBind();
    binding_.set_connection_error_handler(connection_error_handler);
    cb.Run(std::move(interfacePtr));
  }

  void BindOnThread(mojo::ScopedMessagePipeHandle handle,
                    const base::Closure& connection_error_handler) {
    VLOGF_ENTER();
    DCHECK(task_runner_->BelongsToCurrentThread());
    binding_.Bind(mojo::InterfaceRequest<T>(std::move(handle)));
    binding_.set_connection_error_handler(connection_error_handler);
  }

  mojo::Binding<T> binding_;

  base::WeakPtrFactory<MojoBinding<T>> weak_ptr_factory_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(MojoBinding);
};

}  // namespace internal
}  // namespace cros

#endif  // CAMERA_COMMON_UTILS_CROS_CAMERA_MOJO_UTILS_H_
