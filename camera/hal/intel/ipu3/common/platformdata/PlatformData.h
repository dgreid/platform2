/*
 * Copyright (C) 2013-2019 Intel Corporation
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

#ifndef _CAMERA3_HAL_PLATFORMDATA_H_
#define _CAMERA3_HAL_PLATFORMDATA_H_

#include <expat.h>
#include <linux/media.h>
#include <string>
#include <vector>
#include "Camera3V4l2Format.h"
#include "CameraConf.h"
#include "CameraWindow.h"
#include "GraphConfigManager.h"
#include "Metadata.h"
#include "ipc/client/Intel3AClient.h"

#define DEFAULT_ENTRY_CAP 256
#define DEFAULT_DATA_CAP 2048

#define ENTRY_RESERVED 16
#define DATA_RESERVED 128

#define METERING_RECT_SIZE 5

/**
 * Platform capability: max num of in-flight requests
 * Limited by streams buffers number
 */
#define MAX_REQUEST_IN_PROCESS_NUM 10
#define SETTINGS_POOL_SIZE (MAX_REQUEST_IN_PROCESS_NUM * 2)

/**
 * Fake HAL pixel format that we define to use it as index in the table
 * that maps the Gfx HAL pixel formats to concrete V4L2 formats.
 * The original one is HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED, which is the
 * pixel format that goes to the display or Gfx.
 *
 * This one is the pixel format that is implementation defined but it goes
 * to the Video HW
 */
#define HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED_VIDEO   0x7fff5001

/**
 * Maximum number of CPF files cached by the HAL library.
 * On loading the HAL library we will detect all cameras in the system and try
 * to load the CPF files.
 * This define control the maximum number of cameras that we can keep the their
 * CPF loaded in memory.
 * This should  be always higher than the maximum number of cameras in the system
 *
 */
#define MAX_CPF_CACHED  16

/* Maximum number of subdev to lookup */
#define MAX_SUBDEV_ENUMERATE    256

/* These should be read from the platform configure file */
#define MAX_CAMERAS 2
#define BACK_CAMERA_ID 0
#define FRONT_CAMERA_ID 1

#define RESOLUTION_14MP_WIDTH   4352
#define RESOLUTION_14MP_HEIGHT  3264
#define RESOLUTION_8MP_WIDTH    3264
#define RESOLUTION_8MP_HEIGHT   2448
#define RESOLUTION_UHD_WIDTH    3840
#define RESOLUTION_UHD_HEIGHT   2160
#define RESOLUTION_5MP_WIDTH    2560
#define RESOLUTION_5MP_HEIGHT   1920
#define RESOLUTION_1_3MP_WIDTH    1280
#define RESOLUTION_1_3MP_HEIGHT   960
#define RESOLUTION_1080P_WIDTH  1920
#define RESOLUTION_1080P_HEIGHT 1080
#define RESOLUTION_720P_WIDTH   1280
#define RESOLUTION_720P_HEIGHT  720
#define RESOLUTION_480P_WIDTH   768
#define RESOLUTION_480P_HEIGHT  480
#define RESOLUTION_VGA_WIDTH   640
#define RESOLUTION_VGA_HEIGHT  480
#define RESOLUTION_POSTVIEW_WIDTH    320
#define RESOLUTION_POSTVIEW_HEIGHT   240

#define ALIGNED_128 128
#define ALIGNED_64  64

#define MAX_LSC_GRID_WIDTH  64
#define MAX_LSC_GRID_HEIGHT  64
#define MAX_LSC_GRID_SIZE (MAX_LSC_GRID_WIDTH * MAX_LSC_GRID_HEIGHT)

#define IPU3_EVENT_POLL_TIMEOUT 1000 //msecs
#define POLL_REQUEST_TRY_TIMES 2

namespace cros {
namespace intel {
typedef enum {
    SUPPORTED_HW_IPU3,
    SUPPORTED_HW_UNKNOWN
} CameraHwType;

enum SensorType {
    SENSOR_TYPE_NONE = 0,
    SENSOR_TYPE_RAW,           // Raw sensor
    SENSOR_TYPE_SOC            // SOC sensor
};

enum SensorFlip {
    SENSOR_FLIP_NA     = -1,   // Support Not-Available
    SENSOR_FLIP_OFF    = 0x00, // Both flip ctrls set to 0
    SENSOR_FLIP_H      = 0x01, // V4L2_CID_HFLIP 1
    SENSOR_FLIP_V      = 0x02, // V4L2_CID_VFLIP 1
};

class CameraProfiles;

enum ISP_PORT{
    PRIMARY = 0,
    SECONDARY,
    TERTIARY,
    UNKNOWN_PORT,
};

enum SensorDeviceType {
    SENSOR_DEVICE_MAIN, // Main device sensor
    SENSOR_DEVICE_MC    // Media controller sensor
};

struct SensorDriverDescriptor {
    std::string mSensorName;
    std::string mDeviceName;
    std::string mI2CAddress;
    enum ISP_PORT mIspPort;
    enum SensorDeviceType mSensorDevType;
    int csiPort;
};

enum ExtensionGroups {
    CAPABILITY_NONE = 0,
    CAPABILITY_CV = 1 << 0,
    CAPABILITY_STATISTICS = 1 << 1,
    CAPABILITY_ENHANCEMENT = 1 << 2,
    CAPABILITY_DEVICE = 1 << 3,
};

/*
  * aiqd: automatic image quality data
  * aiqd data is used by 3a libs.
  *
  * The main purpose of it is as below:
  * 1. manual AE
  * 2. LSC self-calibration
  * 3. Latest detected flicker detection mode and flicker frequency
  *
  * When the camera is starting, if there is aiqd data, ia_aiq_init() will
  * use it. The 3a also could work if there is no aiqd data.
  * When the camera is stopping, before the ia_aiq_deinit() is called,
  * the ia_aiq_get_aiqd_data() will get the aiqd data from 3a libs.
  *
  * The aiqd data will be read from file system to PlatformData
  * when the camera HAL is loaded by cros_camera_service.
  * When the camera is starting, the aiqd data will be passed to 3a libs.
  * When the camera is stopping, the aiqd data will be saved to PlatformData.
  * When the Chrome OS is shutting down, the aiqd data will be save into
  * the file system.
  */
struct AiqdDataInfo {
    int mDataSize; // the real size of the Data
    int mDataCapacity; // the total size of the Data buffer
    std::string mFileName;
    std::unique_ptr<char[]> mData;

    AiqdDataInfo(): mDataSize(0),
        mDataCapacity(0),
        mFileName("") {}
};

/**
 * \class CameraHWInfo
 *
 * this class is the one that stores the information
 * that comes from the common section in the XML
 * and that keeps the run-time generated list of sensor drivers registered.
 *
 */
typedef std::vector<std::pair<uint32_t, std::string>> SensorModeVector;

class CameraHWInfo {
public:
    CameraHWInfo(); // TODO: proper constructor with member variable initializations
    ~CameraHWInfo() {};
    status_t init(const std::string &mediaDevicePath);

    const char* boardName(void) const { return mBoardName.c_str(); }
    const char* productName(void);
    const char* manufacturerName(void);
    bool supportDualVideo(void) const { return mSupportDualVideo; }
    int getCameraDeviceAPIVersion(void) const { return mCameraDeviceAPIVersion; }
    bool supportExtendedMakernote(void) const { return mSupportExtendedMakernote; }
    bool supportFullColorRange(void) const { return mSupportFullColorRange; }
    bool supportIPUAcceleration(void) const { return mSupportIPUAcceleration; }
    status_t getAvailableSensorModes(const std::string &sensorName,
                                     SensorModeVector &sensorModes) const;
    void getMediaCtlElementNames(std::vector<std::string> &elementNames) const;
    std::string getFullMediaCtlElementName(const std::vector<std::string> elementNames,
                                           const char *value) const;

    std::string mProductName;
    std::string mManufacturerName;
    std::string mBoardName;
    std::string mMediaControllerPathName;
    std::string mMainDevicePathName;
    int mPreviewHALFormat;  // specify the preview format for multi configured streams
    int mCameraDeviceAPIVersion;
    bool mSupportDualVideo;
    bool mSupportExtendedMakernote;
    bool mSupportIPUAcceleration;
    bool mSupportFullColorRange;
    bool mHasMediaController; // TODO: REMOVE. WA to overcome BXT MC-related issue with camera ID <-> ISP port
    bool mPropertyRead;
    media_device_info mDeviceInfo;
    std::vector<struct SensorDriverDescriptor> mSensorInfo;
    struct AiqdDataInfo mAiqdDataInfo[MAX_CAMERAS];
private:
    // the below functions are used to init the mSensorInfo
    status_t initDriverList();
    status_t tryReadProperty();
    status_t findMediaControllerSensors();
    status_t findMediaDeviceInfo();
    status_t initDriverListHelper(unsigned major, unsigned minor, SensorDriverDescriptor& drvInfo);
    status_t getCSIPortID(const std::string &deviceName, int &portId);
};

/**
 * \class CameraCapInfo
 *
 * Base class for all PSL specific CameraCapInfo.
 * The PlatformData::getCameraCapInfo shall return a value of this type
 *
 * This class is used to retrieve the information stored in the XML sections
 * that are per sensor.
 *
 * The methods defined here are to retrieve common information across all
 * PSL's. They are stored in the XML section: HAL_TUNNING
 *
 * Each PSL subclass will  implement extra methods to expose the PSL specific
 * fields
 *
 */
class CameraCapInfo {
public:
    CameraCapInfo() : mSensorType(SENSOR_TYPE_NONE), mGCMNodes(nullptr) {};
    virtual ~CameraCapInfo() {};
    virtual int sensorType(void) const = 0;
    const GraphConfigNodes* getGraphConfigNodes() const { return mGCMNodes; }

protected:
    /* Common fields for all PSL's stored in the XML section HAL_tuning */
    SensorType mSensorType;    /*!> Whether is RAW or SOC */
    /**
     *  Table to map the Gfx HAL pixel formats to V4L2 pixel formats
     *  We need this because there are certain Gfx-HAL Pixel formats that do not
     *  concretely define a pixel layout. We use this table to resolve this
     *  ambiguity
     *  The current GfxHAL pixel formats that are not concrete enough are:
     *  HAL_PIXEL_FORMAT_RAW16
     *  HAL_PIXEL_FORMAT_RAW_OPAQUE
     *  HAL_PIXEL_FORMAT_BLOB
     *  HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED
     *  HAL_PIXEL_FORMAT_YCbCr_420_888
     *
     *  Also the implementation defined format may differ depending if
     *  it goes to Gfx of to video encoder. So one fake format are defined
     *  HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED_VIDEO to differentiate
     *  between both of them
     */
    std::map<int, int> mGfxHalToV4L2PixelFmtTable;
    GraphConfigNodes* mGCMNodes;
};

class GcssKeyMap {
public:
    GcssKeyMap();
    ~GcssKeyMap();
    void gcssKeyMapInsert(std::map<std::string, ia_uid>& customMap);
    int gcssKeyMapSize();
    const char* key2str(const ia_uid key);
    ia_uid str2key(const std::string& key_str);

private:
    std::map<std::string, ia_uid> mMap;
};

class PlatformData {
public:
    static void init();     // called when HAL is loaded
    static void deinit();   // called when HAL is unloaded
private:
    static bool mInitialized;

    static CameraProfiles* mInstance;
    static CameraProfiles* getInstance(void);
    static CameraHWInfo* mCameraHWInfo;
    static CpfStore* sKnownCPFConfigurations[MAX_CPF_CACHED];
    static GcssKeyMap* mGcssKeyMap;

    static Intel3AClient* mIntel3AClient;
public:

    static bool isInitialized() { return mInitialized; }

    static Intel3AClient* getIntel3AClient() { return mIntel3AClient; }

    static GcssKeyMap* getGcssKeyMap();

    static int numberOfCameras(void);
    static void getCameraInfo(int cameraId, struct camera_info* info);
    static const camera_metadata_t* getStaticMetadata(int cameraId);
    static camera_metadata_t* getDefaultMetadata(int cameraId, int requestType);
    static bool isCpfModeAvailable(int cameraId, std::string mode);
    static const AiqConf *getAiqConfiguration(int cameraId,
                                              std::string mode = std::string(CPF_MODE_DEFAULT));
    static const CameraCapInfo* getCameraCapInfo(int cameraId);
    static const CameraHWInfo* getCameraHWInfo() { return mCameraHWInfo; }
    static int getXmlCameraId(int cameraId);
    static status_t getDeviceIds(std::vector<std::string>& names);

    static const char* boardName(void);
    static const char* productName(void);
    static const char* manufacturerName(void);
    static std::string getAiqdFileName(const std::string& sensorName);
    static bool supportDualVideo(void);
    static int getCameraDeviceAPIVersion(void);
    static bool supportExtendedMakernote(void);
    static bool supportIPUAcceleration(void);
    static bool supportFullColorRange(void);
    static status_t getCpfAndCmc(ia_binary_data& cpfData,
                                 ia_cmc_t** cmcData,
                                 uintptr_t* cmcHandle,
                                 int cameraId,
                                 std::string mode = std::string(CPF_MODE_DEFAULT));
    /**
    * Utility methods to retrieve particular fields from the static metadata
    * (a.k.a. Camera Characteristics), Please do NOT add anything else here
    * without a very good reason.
    */
    static int facing(int cameraId);
    static int orientation(int cameraId);
    static float getStepEv(int cameraId);
    static int getPartialMetadataCount(int cameraId);
    static CameraWindow getActivePixelArray(int cameraId);

    static bool readAiqdData(int cameraId, ia_binary_data* data);
    static void saveAiqdData(int cameraId, const ia_binary_data& data);

    static bool isFaceAeEnabled(int cameraId);
    static int faceEngineRunningInterval(int cameraId);

    static int readNvmDataFromDevice(int cameraId);

private:
    static void initAiqdInfo(int cameraIdx);
    static unsigned int getAiqdFileSize(std::string fileName);
    static bool readAiqdDataFromFile(int cameraIdx, const std::string fileName, int fileSize);
    static bool saveAiqdDataToFile();
    static status_t readSpId(std::string& spIdName, unsigned int& spIdValue);
};
} /* namespace intel */
} /* namespace cros */
#endif // _CAMERA3_HAL_PLATFORMDATA_H_
