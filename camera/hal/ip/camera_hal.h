/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_HAL_IP_CAMERA_HAL_H_
#define CAMERA_HAL_IP_CAMERA_HAL_H_

#include <base/macros.h>
#include <base/synchronization/atomic_flag.h>
#include <base/synchronization/lock.h>
#include <base/synchronization/waitable_event.h>
#include <mojo/public/cpp/system/isolated_connection.h>

#include <map>
#include <memory>
#include <string>
#include <sys/types.h>

#include "camera/camera_metadata.h"
#include "cros-camera/camera_mojo_channel_manager.h"
#include "cros-camera/future.h"
#include "hal/ip/camera_device.h"
#include "mojo/ip/ip_camera.mojom.h"

namespace cros {

class CameraHal : public mojom::IpCameraConnectionListener {
 public:
  CameraHal();
  ~CameraHal();

  static CameraHal& GetInstance();

  CameraMojoChannelManager* GetMojoManagerInstance();

  // Implementations of camera_module_t
  int OpenDevice(int id, const hw_module_t* module, hw_device_t** hw_device);
  int GetNumberOfCameras() const;
  int GetCameraInfo(int id, camera_info* info);
  int SetCallbacks(const camera_module_callbacks_t* callbacks);
  int Init();

  // Implementations for cros_camera_hal_t.
  void SetUp(CameraMojoChannelManager* mojo_manager);
  void TearDown();

 private:
  // IpCameraConnectionListener interface
  void OnDeviceConnected(const std::string& ip,
                         const std::string& name,
                         mojom::IpCameraDevicePtr device_ptr,
                         mojom::IpCameraStreamPtr default_stream) override;
  void OnDeviceDisconnected(const std::string& ip) override;

  void InitOnIpcThread(scoped_refptr<Future<int>> return_val);
  void DestroyOnIpcThread(scoped_refptr<Future<void>> return_val);
  void OnConnectionError();

  base::AtomicFlag initialized_;
  std::unique_ptr<mojo::IsolatedConnection> isolated_connection_;
  mojom::IpCameraDetectorPtr detector_;
  mojo::Binding<IpCameraConnectionListener> binding_;

  // The maps, as well as |next_camera_id_| are protected by this lock
  base::Lock camera_map_lock_;
  // Maps from IP to HAL camera id
  std::map<const std::string, int> ip_to_id_;
  std::map<int, std::unique_ptr<CameraDevice>> cameras_;
  int next_camera_id_;

  // Any calls to OnDeviceConnected/OnDeviceDisconnected will block until
  // SetCallbacks has been called
  base::WaitableEvent callbacks_set_;
  const camera_module_callbacks_t* callbacks_;

  // Mojo manager which is used for Mojo communication.
  CameraMojoChannelManager* mojo_manager_;

  DISALLOW_COPY_AND_ASSIGN(CameraHal);
};

}  // namespace cros

extern camera_module_t HAL_MODULE_INFO_SYM;

#endif  // CAMERA_HAL_IP_CAMERA_HAL_H_
