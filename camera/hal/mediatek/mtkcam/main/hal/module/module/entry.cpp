/*
 * Copyright (C) 2019 MediaTek Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "MtkCam/module"
//
#include <cros-camera/cros_camera_hal.h>
#include <mtkcam/def/common.h>
#include <mtkcam/main/hal/ICamDeviceManager.h>
#include <mtkcam/main/hal/module.h>
#include <mtkcam/main/hal/module/module/module.h>
#include <mtkcam/utils/std/common.h>
#include <mtkcam/utils/std/Log.h>
#include <mtkcam/utils/std/Mojo.h>
#include <property_lib.h>
#include <string>

#define VISIBILITY __attribute__((visibility("default")))

/******************************************************************************
 * Implementation of camera_module
 ******************************************************************************/
NSCam::mtk_camera_module VISIBILITY HAL_MODULE_INFO_SYM = {
    .common = get_camera_module(),
};

////////////////////////////////////////////////////////////////////////////////
//  Implementation of hw_module_methods_t
////////////////////////////////////////////////////////////////////////////////
static int open_device(hw_module_t const* module,
                       const char* name,
                       hw_device_t** device) {
  return NSCam::getCamDeviceManager()->open(device, module, name,
                                            CAMERA_DEVICE_API_VERSION_3_2);
}

static hw_module_methods_t* get_module_methods() {
  static hw_module_methods_t methods = {.open = open_device};

  return &methods;
}

////////////////////////////////////////////////////////////////////////////////
//  Implementation of camera_module_t
////////////////////////////////////////////////////////////////////////////////
static int get_number_of_cameras(void) {
  return NSCam::getCamDeviceManager()->getNumberOfDevices();
}

static int get_camera_info(int cameraId, camera_info* info) {
  return NSCam::getCamDeviceManager()->getDeviceInfo(cameraId, info);
}

static int set_callbacks(camera_module_callbacks_t const* callbacks) {
  return NSCam::getCamDeviceManager()->setCallbacks(callbacks);
}

static int open_legacy(const struct hw_module_t* module,
                       const char* id,
                       uint32_t halVersion,
                       struct hw_device_t** device) {
  return NSCam::getCamDeviceManager()->open(device, module, id, halVersion);
}

static int hal_init(void) {
  if (NSCam::getCamDeviceManager()->getNumberOfDevices() == 0)
    return -ENODEV;
  else
    return 0;
}

static void set_up(cros::CameraMojoChannelManagerToken* token) {
  NSCam::Utils::setMojoManagerToken(token);
}

static void tear_down() {
  NSCam::Utils::setMojoManagerToken(nullptr);
}

static camera_module get_camera_module() {
  camera_module module = {
      .common =
          {
              .tag = HARDWARE_MODULE_TAG,
              .module_api_version = CAMERA_MODULE_API_VERSION_2_4,
              .hal_api_version = HARDWARE_HAL_API_VERSION,
              .id = CAMERA_HARDWARE_MODULE_ID,
              .name = "MediaTek Camera Module",
              .author = "MediaTek",
              .methods = get_module_methods(),
              .dso = NULL,
              .reserved = {0},
          },
      .get_number_of_cameras = get_number_of_cameras,
      .get_camera_info = get_camera_info,
      .set_callbacks = set_callbacks,
      .get_vendor_tag_ops =
          NULL,  // TODO(MTK): https://issuetracker.google.com/138623788
      .open_legacy = open_legacy,
      .set_torch_mode = NULL,
      .init = hal_init,
      .reserved = {0},
  };
  return module;
}

cros::cros_camera_hal_t VISIBILITY CROS_CAMERA_HAL_INFO_SYM = {
    .set_up = set_up, .tear_down = tear_down};
