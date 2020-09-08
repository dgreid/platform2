// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_PERCEPTION_PRODUCER_IMPL_H_
#define MEDIA_PERCEPTION_PRODUCER_IMPL_H_

#include <base/bind.h>
#include <map>
#include <memory>

#include "media_perception/shared_memory_provider.h"
#include "mojom/producer.mojom.h"
#include "mojom/video_source_provider.mojom.h"
#include "mojom/virtual_device.mojom.h"

namespace mri {

class ProducerImpl : public video_capture::mojom::Producer {
 public:
  ProducerImpl() : binding_(this) {}

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
  // Creates a ProducerPtr that is bound to this instance through a message
  // pipe. When calling this more than once, the previously return ProducerPtr
  // will get unbound.
  video_capture::mojom::ProducerPtr CreateInterfacePtr();

  void OnFrameBufferReceived(std::shared_ptr<ProducerImpl> producer_impl,
                             base::TimeDelta timestamp,
                             std::unique_ptr<const uint8_t[]> data,
                             int data_size,
                             media::mojom::VideoCapturePixelFormat pixel_format,
                             int width,
                             int height,
                             int32_t buffer_id);

  // Binding of the Producer interface to message pipe.
  mojo::Binding<video_capture::mojom::Producer> binding_;

  // Provides an interface to a created virtual device.
  video_capture::mojom::SharedMemoryVirtualDevicePtr virtual_device_;

  std::map<int32_t /*buffer_id*/, std::unique_ptr<SharedMemoryProvider>>
      outgoing_buffer_id_to_buffer_map_;
};

}  // namespace mri

#endif  // MEDIA_PERCEPTION_PRODUCER_IMPL_H_
