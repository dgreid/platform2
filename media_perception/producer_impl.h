// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_PERCEPTION_PRODUCER_IMPL_H_
#define MEDIA_PERCEPTION_PRODUCER_IMPL_H_

#include <map>
#include <memory>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/receiver.h>

#include <base/time/time.h>
#include <base/memory/writable_shared_memory_region.h>
#include <mojo/public/cpp/bindings/binding.h>

#include "mojom/producer.mojom.h"
#include "mojom/video_source_provider.mojom.h"
#include "mojom/virtual_device.mojom.h"

namespace mri {

class ProducerImpl : public video_capture::mojom::Producer {
 public:
  ProducerImpl() : receiver_(this) {}

  // factory is owned by the caller.
  void RegisterVirtualDevice(
      video_capture::mojom::VideoSourceProviderPtr* provider,
      media::mojom::VideoCaptureDeviceInfoPtr info);

  void PushNextFrame(std::shared_ptr<ProducerImpl> producer_impl,
                     base::TimeDelta timestamp,
                     std::unique_ptr<const uint8_t[]> data,
                     int data_size,
                     media::mojom::VideoCapturePixelFormat pixel_format,
                     int width,
                     int height);

  // video_capture::mojom::Producer overrides.
  void OnNewBuffer(int32_t buffer_id,
                   media::mojom::VideoBufferHandlePtr buffer_handle,
                   OnNewBufferCallback callback) override;
  void OnBufferRetired(int32_t buffer_id) override;

 private:
  // Creates a Producer PendingRemote that is bound to this instance through
  // a message pipe. When calling this more than once, the previously return
  // PendingRemote will get unbound.
  mojo::PendingRemote<video_capture::mojom::Producer>
  CreateInterfacePendingRemote();

  void OnFrameBufferReceived(std::shared_ptr<ProducerImpl> producer_impl,
                             base::TimeDelta timestamp,
                             std::unique_ptr<const uint8_t[]> data,
                             int data_size,
                             media::mojom::VideoCapturePixelFormat pixel_format,
                             int width,
                             int height,
                             int32_t buffer_id);

  // Binding of the Producer interface to message pipe.
  mojo::Receiver<video_capture::mojom::Producer> receiver_;

  // Provides an interface to a created virtual device.
  video_capture::mojom::SharedMemoryVirtualDevicePtr virtual_device_;

  std::map<int32_t /*buffer_id*/, base::WritableSharedMemoryMapping>
      outgoing_buffer_id_to_buffer_map_;
};

}  // namespace mri

#endif  // MEDIA_PERCEPTION_PRODUCER_IMPL_H_
