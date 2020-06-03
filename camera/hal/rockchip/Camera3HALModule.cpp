/*
 * Copyright (C) 2013-2017 Intel Corporation
 * Copyright (c) 2017, Fuzhou Rockchip Electronics Co., Ltd
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
#define LOG_TAG "Camera3HALModule"

#include <dlfcn.h>
#include <stdlib.h>
#include <mutex>

#include <cros-camera/cros_camera_hal.h>
#include <hardware/camera3.h>

#include "LogHelper.h"
#include "PlatformData.h"
#include "PerformanceTraces.h"

#include "Camera3HAL.h"

#ifdef REMOTE_3A_SERVER
#include "Rk3aCommon.h"
#endif

using namespace android;
USING_DECLARED_NAMESPACE;

/**
 * \macro VISIBILITY_PUBLIC
 *
 * Controls the visibility of symbols in the shared library.
 * In production builds all symbols in the shared library are hidden
 * except the ones using this linker attribute.
 */
#define VISIBILITY_PUBLIC __attribute__ ((visibility ("default")))

static int hal_dev_close(hw_device_t* device);

/**********************************************************************
 * Camera Module API (C API)
 **********************************************************************/

static bool sInstances[MAX_CAMERAS] = {false, false};
static int sInstanceCount = 0;

/**
 * Global mutex used to protect sInstanceCount and sInstances
 */
static std::mutex sCameraHalMutex;

cros::CameraMojoChannelManager* android::camera2::g_mojo_manager = nullptr;

int openCameraHardware(int id, const hw_module_t* module, hw_device_t** device)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    if (sInstances[id])
        return 0;

    Camera3HAL* halDev = new Camera3HAL(id, module);

    if (halDev->init() != NO_ERROR) {
        LOGE("HAL initialization fail!");
        delete halDev;
        return -EINVAL;
    }
    camera3_device_t *cam3Device = halDev->getDeviceStruct();

    cam3Device->common.close = hal_dev_close;
    *device = &cam3Device->common;

    sInstanceCount++;
    sInstances[id] = true;

    return 0;
}

static int hal_get_number_of_cameras(void)
{
    LogHelper::setDebugLevel();
    PerformanceTraces::reset();
    PerformanceTraces::HalAtrace::reset();

    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    PERFORMANCE_HAL_ATRACE();

    return PlatformData::numberOfCameras();
}

static int hal_get_camera_info(int cameraId, struct camera_info *cameraInfo)
{
    PERFORMANCE_HAL_ATRACE();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    if (cameraId < 0 || !cameraInfo ||
          cameraId >= hal_get_number_of_cameras())
        return -EINVAL;

    PlatformData::getCameraInfo(cameraId, cameraInfo);

    return 0;
}

static int hal_set_callbacks(const camera_module_callbacks_t *callbacks)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    UNUSED(callbacks);
    return 0;
}

static int hal_dev_open(const hw_module_t* module, const char* name,
                        hw_device_t** device)
{
    int status = -EINVAL;
    int camera_id;

    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);
    LogHelper::setDebugLevel();
    PerformanceTraces::reset();
    PerformanceTraces::HalAtrace::reset();

    PERFORMANCE_HAL_ATRACE();

    if (!name || !module || !device) {
        LOGE("Camera name is nullptr");
        return status;
    }

    LOG1("%s, camera id: %s", __FUNCTION__, name);
    camera_id = atoi(name);

#ifdef REMOTE_3A_SERVER
    if (PlatformData::getRockchip3AClient()
        && !PlatformData::getRockchip3AClient()->isIPCFine()) {
        PlatformData::deinit();
        LOGW("@%s, remote 3A IPC fails", __FUNCTION__);
    }
#endif

    if (!PlatformData::isInitialized()) {
        PlatformData::init(); // try to init the PlatformData again.
        if (!PlatformData::isInitialized()) {
            LOGE("%s: open Camera id %d fails due to PlatformData init fails",
                __func__, camera_id);
            return -ENODEV;
        }
    }

    if (camera_id < 0 || camera_id >= hal_get_number_of_cameras()) {
        LOGE("%s: Camera id %d is out of bounds, num. of cameras (%d)",
             __func__, camera_id, hal_get_number_of_cameras());
        return -ENODEV;
    }

    std::lock_guard<std::mutex> l(sCameraHalMutex);

    if ((!PlatformData::supportDualVideo()) &&
         sInstanceCount > 0 && !sInstances[camera_id]) {
        LOGE("Don't support front/primary open at the same time");
        return -EUSERS;
    }

    return openCameraHardware(camera_id, module, device);
}

static int hal_dev_close(hw_device_t* device)
{
    PERFORMANCE_HAL_ATRACE();
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    if (!device || sInstanceCount == 0) {
        LOGW("hal close, instance count %d", sInstanceCount);
        return -EINVAL;
    }

    camera3_device_t *camera3_dev = (struct camera3_device *)device;
    Camera3HAL* camera_priv = (Camera3HAL*)(camera3_dev->priv);

    if (camera_priv != nullptr) {
        std::lock_guard<std::mutex> l(sCameraHalMutex);
        camera_priv->deinit();
        int id = camera_priv->getCameraId();
        delete camera_priv;
        sInstanceCount--;
        sInstances[id] = false;
    }

    LOG1("%s, instance count %d", __FUNCTION__, sInstanceCount);

    return 0;
}

static int hal_open_legacy(
    const struct hw_module_t* module,
    const char* id,
    uint32_t halVersion,
    struct hw_device_t** device)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    UNUSED(module);
    UNUSED(id);
    UNUSED(halVersion);
    UNUSED(device);
    return -ENOSYS;
}

static int hal_set_torch_mode(const char* camera_id, bool enabled)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    UNUSED(camera_id);
    UNUSED(enabled);
    return -ENOSYS;
}

static int hal_init()
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL1);

    if (PlatformData::numberOfCameras() == 0) {
        LOGE("Init failed bacause no camera device was found.");
        return -ENODEV;
    }
    return 0;
}

static void set_up(cros::CameraMojoChannelManager* mojo_manager)
{
    android::camera2::g_mojo_manager = mojo_manager;

    LogHelper::setDebugLevel();
    PerformanceTraces::reset();
    PlatformData::init();
    int ret = PlatformData::numberOfCameras();
    if (ret == 0) {
      LOGE("No camera device was found!");
      return;
    }
}

static void tear_down()
{
    PlatformData::deinit();

    android::camera2::g_mojo_manager = nullptr;
}

static struct hw_module_methods_t hal_module_methods = {
    NAMED_FIELD_INITIALIZER(open) hal_dev_open
};

camera_module_t VISIBILITY_PUBLIC HAL_MODULE_INFO_SYM = {
    NAMED_FIELD_INITIALIZER(common) {
        NAMED_FIELD_INITIALIZER(tag) HARDWARE_MODULE_TAG,
        NAMED_FIELD_INITIALIZER(module_api_version) CAMERA_MODULE_API_VERSION_2_4,
        NAMED_FIELD_INITIALIZER(hal_api_version) 0,
        NAMED_FIELD_INITIALIZER(id) CAMERA_HARDWARE_MODULE_ID,
        NAMED_FIELD_INITIALIZER(name) "Rockchip Camera3HAL Module",
        NAMED_FIELD_INITIALIZER(author) "Rockchip",
        NAMED_FIELD_INITIALIZER(methods) &hal_module_methods,
        NAMED_FIELD_INITIALIZER(dso) nullptr,
        NAMED_FIELD_INITIALIZER(reserved) {0},
    },
    NAMED_FIELD_INITIALIZER(get_number_of_cameras) hal_get_number_of_cameras,
    NAMED_FIELD_INITIALIZER(get_camera_info) hal_get_camera_info,
    NAMED_FIELD_INITIALIZER(set_callbacks) hal_set_callbacks,
    NAMED_FIELD_INITIALIZER(open_legacy) hal_open_legacy,
    NAMED_FIELD_INITIALIZER(set_torch_mode) hal_set_torch_mode,
    NAMED_FIELD_INITIALIZER(init) hal_init,
    NAMED_FIELD_INITIALIZER(reserved) {0}
};

cros::cros_camera_hal_t VISIBILITY_PUBLIC CROS_CAMERA_HAL_INFO_SYM = {
    NAMED_FIELD_INITIALIZER(set_up) set_up,
    NAMED_FIELD_INITIALIZER(tear_down) tear_down
};
