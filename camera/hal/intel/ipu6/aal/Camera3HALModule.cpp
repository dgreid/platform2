/*
 * Copyright (C) 2017-2020 Intel Corporation
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

#include <cros-camera/cros_camera_hal.h>

#include <hardware/camera3.h>
#include <hardware/hardware.h>

#include <mutex>

#include "Camera3HAL.h"
#include "HALv3Utils.h"
#include "ICamera.h"
#include "MetadataConvert.h"
#include "PlatformData.h"
#include "Utils.h"
#include "iutils/CameraDump.h"

namespace camera3 {

#define MAX_CAMERAS 2

/**
 * \macro VISIBILITY_PUBLIC
 *
 * Controls the visibility of symbols in the shared library.
 * In production builds all symbols in the shared library are hidden
 * except the ones using this linker attribute.
 */
#define VISIBILITY_PUBLIC __attribute__((visibility("default")))

static int hal_dev_close(hw_device_t* device);

/**********************************************************************
 * Camera Module API (C API)
 **********************************************************************/

static bool sInstances[MAX_CAMERAS] = {false, false};
static int sInstanceCount = 0;
// sCameraMetadata buffer won't be free in CAL
static android::CameraMetadata* sCameraMetadata[MAX_CAMERAS] = {nullptr};

static int sCameraNumber = 0;
/**
 * Global mutex used to protect sInstanceCount and sInstances
 */
static std::mutex sCameraHalMutex;

int openCameraHardware(int id, const hw_module_t* module, hw_device_t** device) {
    LOG1("@%s", __func__);

    if (sInstances[id]) return 0;

    Camera3HAL* halDev = new Camera3HAL(id, module);
    if (!halDev->isInitialized()) {
        LOGE("HAL initialization fail!");
        delete halDev;
        return -EINVAL;
    }
    camera3_device_t* cam3Device = halDev->getDeviceStruct();

    cam3Device->common.close = hal_dev_close;
    *device = &cam3Device->common;

    sInstanceCount++;
    sInstances[id] = true;

    LOG1("@%s end", __func__);
    return 0;
}

static int hal_get_number_of_cameras(void) {
    LOG1("@%s", __func__);

    return sCameraNumber;
}

static int hal_get_camera_info(int cameraId, struct camera_info* cameraInfo) {
    LOG1("@%s", __func__);

    if (cameraId < 0 || !cameraInfo || cameraId >= hal_get_number_of_cameras()) return -EINVAL;

    icamera::camera_info_t info;
    icamera::get_camera_info(cameraId, info);

    if (sCameraMetadata[cameraId] == nullptr) {
        sCameraMetadata[cameraId] = new android::CameraMetadata;
        MetadataConvert::HALCapabilityToStaticMetadata(*(info.capability),
                                                       sCameraMetadata[cameraId]);
    }
    int32_t tag = ANDROID_LENS_FACING;
    camera_metadata_entry entry = sCameraMetadata[cameraId]->find(tag);
    if (entry.count == 1) {
        info.facing = entry.data.u8[0];
    }
    tag = ANDROID_SENSOR_ORIENTATION;
    entry = sCameraMetadata[cameraId]->find(tag);
    if (entry.count == 1) {
        info.orientation = entry.data.u8[0];
    }
    memset(cameraInfo, 0, sizeof(camera_info));
    cameraInfo->facing = info.facing ? CAMERA_FACING_BACK : CAMERA_FACING_FRONT;
    cameraInfo->device_version = CAMERA_DEVICE_API_VERSION_3_3;
    cameraInfo->orientation = info.orientation;
    const camera_metadata_t* settings = sCameraMetadata[cameraId]->getAndLock();
    cameraInfo->static_camera_characteristics = settings;
    sCameraMetadata[cameraId]->unlock(settings);

    return 0;
}

static int hal_set_callbacks(const camera_module_callbacks_t* callbacks) {
    LOG1("@%s", __func__);

    UNUSED(callbacks);
    return 0;
}

static int hal_dev_open(const hw_module_t* module, const char* name, hw_device_t** device) {
    icamera::Log::setDebugLevel();
    icamera::CameraDump::setDumpLevel();

    LOG1("@%s", __func__);

    int status = -EINVAL;

    if (!name || !module || !device) {
        LOGE("Camera name is nullptr");
        return status;
    }
    LOG1("%s, camera id: %s", __func__, name);

#ifdef ENABLE_SANDBOXING
    if (!icamera::IntelAlgoClient::getInstance()->isIPCFine()) {
        CheckError(icamera::IntelAlgoClient::getInstance()->initialize() != icamera::OK, -EINVAL,
                   "%s, Connect to algo service fails", __func__);
    }
#endif

    int camera_id = atoi(name);
    if (camera_id < 0 || camera_id >= hal_get_number_of_cameras()) {
        LOGE("%s: Camera id %d is out of bounds, num. of cameras (%d)", __func__, camera_id,
             hal_get_number_of_cameras());
        return -ENODEV;
    }

    std::lock_guard<std::mutex> l(sCameraHalMutex);

    if (sInstanceCount > 0 && sInstances[camera_id]) {
        LOGW("Camera already has been opened!");
        return -EUSERS;
    }

    return openCameraHardware(camera_id, module, device);
}

static int hal_dev_close(hw_device_t* device) {
    LOG1("@%s", __func__);

    if (!device || sInstanceCount == 0) {
        LOGW("hal close, instance count %d", sInstanceCount);
        return -EINVAL;
    }

    camera3_device_t* camera3_dev = (struct camera3_device*)device;
    Camera3HAL* camera_priv = static_cast<Camera3HAL*>(camera3_dev->priv);

    if (camera_priv != nullptr) {
        std::lock_guard<std::mutex> l(sCameraHalMutex);
        int id = camera_priv->getCameraId();
        delete camera_priv;
        sInstanceCount--;
        sInstances[id] = false;
    }

    LOG1("%s, instance count %d", __func__, sInstanceCount);

    return 0;
}

static int hal_init(void) {
    LOG1("@%s", __func__);

    /*
     * Check the connection status with algo service and the detected
     * camera number. Then the service decides whether to restart or
     * not based on the return value
     */
#ifdef ENABLE_SANDBOXING
    CheckError(icamera::IntelAlgoClient::getInstance()->initialize() != icamera::OK, -EINVAL,
               "%s, Connect to algo service fails", __func__);
#endif

    int crosCameraNum = camera3::HalV3Utils::getCrosConfigCameraNumber();
    int xmlCameraNum = icamera::PlatformData::getXmlCameraNumber();
    int currentCameraNum = icamera::PlatformData::numberOfCameras();

    if (xmlCameraNum == -1 && crosCameraNum == -1) {
        LOGW("static camera number is not available");
        sCameraNumber = currentCameraNum;
    } else {
        sCameraNumber = (xmlCameraNum != -1) ? xmlCameraNum : crosCameraNum;
        CheckError(currentCameraNum < sCameraNumber, -EINVAL,
                   "@%s, expected cameras number: %d, found: %d", __func__,
                   sCameraNumber, currentCameraNum);
    }

    if (sCameraNumber != 0) {
        // Initialize PlatformData
        int ret = icamera::camera_hal_init();
        CheckError(ret != icamera::OK, -EINVAL, "@%s, camera_hal_init fails, ret:%d",
                   __func__, ret);
    }

    return 0;
}

static int hal_set_torch_mode(const char* camera_id, bool enabled) {
    LOG1("@%s", __func__);

    UNUSED(camera_id);
    UNUSED(enabled);
    return -ENOSYS;
}

/*
 * The setup sequence for camera hal module
 *  1. dlopen()
 *  2. set_up() : for chrome camera service only
 *  3. init()
 *  4. get_number_of_cameras()
 *  ......
 */
static void hal_set_up(cros::CameraMojoChannelManager* mojoManager) {
    LOG1("@%s", __func__);

    icamera::Log::setDebugLevel();
    icamera::CameraDump::setDumpLevel();

#ifdef ENABLE_SANDBOXING
    // Create IntelAlgoClient and set the mojo manager
    icamera::IntelAlgoClient::getInstance()->setMojoManager(mojoManager);
#endif
}

/*
 * The close sequence for camera hal module
 *  ......
 *  1. tear_down() : for chrome camera service only
 *  2. dlclose()
 */
static void hal_tear_down() {
    LOG1("@%s", __func__);

    int ret = icamera::camera_hal_deinit();
    CheckError(ret != icamera::OK, VOID_VALUE, "@%s, camera_hal_deinit fails, ret:%d", __func__,
               ret);
#ifdef ENABLE_SANDBOXING
    icamera::IntelAlgoClient::releaseInstance();
#endif
}

static struct hw_module_methods_t hal_module_methods = {.open = hal_dev_open};

static hw_module_t camera_common = {.tag = HARDWARE_MODULE_TAG,
                                    .module_api_version = CAMERA_MODULE_API_VERSION_2_3,
                                    .hal_api_version = HARDWARE_HAL_API_VERSION,
                                    .id = CAMERA_HARDWARE_MODULE_ID,
                                    .name = "Intel Camera3HAL Module",
                                    .author = "Intel",
                                    .methods = &hal_module_methods,
                                    .dso = nullptr,
                                    .reserved = {0}};

extern "C" {
camera_module_t VISIBILITY_PUBLIC HAL_MODULE_INFO_SYM = {
    .common = camera_common,
    .get_number_of_cameras = hal_get_number_of_cameras,
    .get_camera_info = hal_get_camera_info,
    .set_callbacks = hal_set_callbacks,
    .get_vendor_tag_ops = nullptr,
    .open_legacy = nullptr,
    .set_torch_mode = hal_set_torch_mode,
    .init = hal_init,
    .reserved = {0}};

// For Chrome OS
cros::cros_camera_hal_t VISIBILITY_PUBLIC CROS_CAMERA_HAL_INFO_SYM = {
    .set_up = hal_set_up,
    .tear_down = hal_tear_down,
};
}

}  // namespace camera3
