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

#define LOG_TAG "SWNRPlugin"

#include <camera_custom_capture_nr.h>
#include <map>
#include <memory>
#include <mtkcam/3rdparty/mtk/swnr/SWNRImpl.h>
#include <mtkcam/drv/IHalSensor.h>
#include <mtkcam/utils/metadata/client/mtk_metadata_tag.h>
#include <mtkcam/utils/metadata/hal/mtk_platform_metadata_tag.h>
#include <mtkcam/utils/metastore/IMetadataProvider.h>
#include <mtkcam/utils/metastore/ITemplateRequest.h>
#include <mtkcam/utils/std/Format.h>
#include <mtkcam/utils/std/Log.h>
#include <mtkcam/utils/std/Trace.h>
#include <utility>
#include <vector>

using NSCam::NSPipelinePlugin::Interceptor;
using NSCam::NSPipelinePlugin::PipelinePlugin;
using NSCam::NSPipelinePlugin::PluginRegister;
using NSCam::NSPipelinePlugin::SwnrPluginProviderImp;
using NSCam::NSPipelinePlugin::Yuv;

using NSCam::IMetadata;
using NSCam::Type2Type;
using std::map;
using std::vector;

/******************************************************************************
 *
 ******************************************************************************/
#define DEBUG_MODE (1)

template <class T>
inline bool tryGetMetadata(IMetadata const* pMetadata, MUINT32 tag, T* pVal) {
  if (pMetadata == nullptr) {
    return MFALSE;
  }

  IMetadata::IEntry entry = pMetadata->entryFor(tag);
  if (!entry.isEmpty()) {
    *pVal = entry.itemAt(0, Type2Type<T>());
    return true;
  } else {
#define var(v) #v
#define type(t) #t
    MY_LOGI("no metadata %s in %s", var(tag), type(pMetadata));
#undef type
#undef var
  }
  return false;
}
/******************************************************************************
 *
 ******************************************************************************/
REGISTER_PLUGIN_PROVIDER(Yuv, SwnrPluginProviderImp);

/******************************************************************************
 *
 ******************************************************************************/
bool SwnrPluginProviderImp::queryNrThreshold(int const scenario,
                                             int* pHw_threshold,
                                             int* pSwnr_threshold) {
  FUNCTION_IN;
  NSCam::IHalSensorList* pHalSensorList = GET_HalSensorList();
  if (pHalSensorList->queryType(mOpenId) == NSCam::NSSensorType::eYUV) {
    // yuv sensor not support multi-pass NR
    return false;
  }
  MUINT const sensorDev = pHalSensorList->querySensorDevIdx(mOpenId);
  //
  *pHw_threshold = -1;
  *pSwnr_threshold = -1;
  // get threshold from custom folder
  get_capture_nr_th(sensorDev, eShotMode_NormalShot,
                    scenario & MTK_PLUGIN_MODE_MFNR, pHw_threshold,
                    pSwnr_threshold);
  MY_LOGD("threshold(H:%d/S:%d)", *pHw_threshold, *pSwnr_threshold);

  FUNCTION_OUT;
  return true;
}

/******************************************************************************
 *
 ******************************************************************************/
void SwnrPluginProviderImp::waitForIdle() {
  FUNCTION_IN;

  onProcessFuture();
  FUNCTION_OUT;

  return;
}

/******************************************************************************
 *
 ******************************************************************************/
SwnrPluginProviderImp::SwnrPluginProviderImp() {
  FUNCTION_IN;

  mEnableLog = ::property_get_int32("debug.plugin.swnr", 1);
  muDumpBuffer = ::property_get_int32("debug.camera.dump.nr", 0);
  mEnable = ::property_get_int32("vendor.debug.camera.nr.enable", -1);
  FUNCTION_OUT;
}

/******************************************************************************
 *
 ******************************************************************************/
SwnrPluginProviderImp::~SwnrPluginProviderImp() {
  FUNCTION_IN;

  FUNCTION_OUT;
}

/******************************************************************************
 *
 ******************************************************************************/
void SwnrPluginProviderImp::set(MINT32 iOpenId) {
  MY_LOGD("set openId:%d", iOpenId);
  mOpenId = iOpenId;
}

/******************************************************************************
 *
 ******************************************************************************/
const SwnrPluginProviderImp::Property& SwnrPluginProviderImp::property() {
  FUNCTION_IN;

  static Property prop;
  static bool inited;

  if (!inited) {
    prop.mName = "MTK NR";
    prop.mFeatures = MTK_FEATURE_NR;
    prop.mInPlace = MTRUE;
    prop.mFaceData = eFD_None;
    prop.mPosition = 0;
    prop.mSupportCrop = MFALSE;
    prop.mSupportScale = MFALSE;
    inited = true;
  }
  FUNCTION_OUT;
  return prop;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
SwnrPluginProviderImp::negotiate(Selection* sel) {
  FUNCTION_IN;
  if (0 == mEnable) {
    FUNCTION_OUT;
    return -EINVAL;
  }
  if (sel->mIMetadataDynamic.getControl() != NULL) {
    IMetadata* pIMetadata_P1 = sel->mIMetadataDynamic.getControl().get();
    IMetadata* pIMetadata_Hal = sel->mIMetadataHal.getControl().get();

    MINT32 threshold = 0;
    MINT32 iso = 0;
    MINT32 magic = 0;

    tryGetMetadata<MINT32>(pIMetadata_P1, MTK_SENSOR_SENSITIVITY, &iso);
    tryGetMetadata<MINT32>(pIMetadata_Hal, MTK_P1NODE_PROCESSOR_MAGICNUM,
                           &magic);

    MINT64 scenario = 0;
    MINT32 threshold_swnr, threshold_mnr;
    queryNrThreshold(scenario, &threshold_mnr, &threshold_swnr);

    threshold = threshold_mnr;

    MY_LOGD("threshold:%d iso:%d, magic:%d", threshold, iso, magic);
    // return -EINVAL if do not meet the trigger condition
    if (iso < threshold && mEnable == -1) {
      FUNCTION_OUT;
      return -EINVAL;
    }
  }

  sel->mIBufferFull.setRequired(true)
      .addAcceptedFormat(eImgFmt_YV12)
      .addAcceptedSize(eImgSize_Full);

  sel->mIMetadataDynamic.setRequired(true);
  sel->mIMetadataApp.setRequired(true);
  sel->mIMetadataHal.setRequired(true);
  sel->mOMetadataApp.setRequired(false);
  sel->mOMetadataHal.setRequired(false);
  FUNCTION_OUT;
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
void SwnrPluginProviderImp::init() {
  FUNCTION_IN;
  mbRequestExit = MFALSE;
  MY_LOGD("create new thread +");
  mThread = std::thread(std::bind(&SwnrPluginProviderImp::threadLoop, this));
  MY_LOGD("create new thread -");
  FUNCTION_OUT;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
SwnrPluginProviderImp::process(RequestPtr pRequest,
                               RequestCallbackPtr pCallback) {
  FUNCTION_IN;
  MERROR r = OK;
  // 1. check params
  if (!pRequest->mIBufferFull.get()) {
    if (pCallback.get() != nullptr) {
      pCallback->onCompleted(pRequest, 0);
      goto lbExit;
    }
  }
  {
    // TODO(MTK): workaround for init time has no OpenId info
    if (mpSwnr == NULL) {
      CAM_TRACE_FMT_BEGIN("NRplugin:MAKE_SwNR instance(%d)", mOpenId);
      MY_LOGD("create instance openId %d", mOpenId);
      if (mOpenId < 0) {
        MY_LOGE("need set openId before init() !!");
      } else {
        mpSwnr = reinterpret_cast<ISwNR*>(MAKE_SWNR_IPC(mOpenId));
      }

      CAM_TRACE_FMT_END();
    }
  }

  // 2. enque
  {
    int debug_sync = ::property_get_int32("debug.plugin.swnr.sync", 0);

    // sync call
    if ((!pCallback.get()) || debug_sync) {
      MY_LOGD("swnr sync call...");
      r = doSwnr(pRequest);
      if (pCallback.get()) {
        pCallback->onCompleted(pRequest, r);
      }
    } else {
      MY_LOGD("swnr async call...");
      std::unique_lock<std::mutex> lck(mFutureLock);
      mvFutures.insert(std::pair<RequestPtr, std::future<int32_t>>(
          pRequest, std::async(std::launch::deferred, [=]() -> int32_t {
            ::prctl(PR_SET_NAME, reinterpret_cast<int64_t>("Cam@doSWNR"), 0, 0,
                    0);
            int32_t err = OK;
            err = doSwnr(pRequest);
            if (pCallback.get() != nullptr)
              pCallback->onCompleted(pRequest, err);
            return err;
          })));
      mFutureCond.notify_one();
    }  // else
  }

lbExit:
  FUNCTION_OUT;
  return r;
}

/******************************************************************************
 *
 ******************************************************************************/
void SwnrPluginProviderImp::abort(const vector<RequestPtr>& pRequests) {
  FUNCTION_IN;
  MY_LOGD("temp skip");
  std::lock_guard<std::mutex> _l(mFutureLock);
  int idx = 0;
  while (!pRequests.empty()) {
    for (map<RequestPtr, std::future<int32_t>>::iterator it = mvFutures.begin();
         it != mvFutures.end();) {
      if ((*it).first == pRequests[idx]) {
        it = mvFutures.erase(it);
      }
    }
    idx++;
  }

  FUNCTION_OUT;
}

/******************************************************************************
 *
 ******************************************************************************/
void SwnrPluginProviderImp::uninit() {
  FUNCTION_IN;

  if (mpSwnr) {
    MY_LOGD("delete instance openId %d", mOpenId);
    delete mpSwnr;
    mpSwnr = nullptr;
  }
  if (!mvFutures.empty()) {
    MY_LOGE("remain %d requests...", mvFutures.size());
    waitForIdle();
  }
  {
    std::unique_lock<std::mutex> lck(mFutureLock);
    mbRequestExit = MTRUE;
    mFutureCond.notify_one();
  }

  // Make sure mThread is finish
  {
    MY_LOGD("Uninit make sure mThread finish+");
    mThread.join();
    MY_LOGD("Uninit make sure mThread finish-");
  }
  FUNCTION_OUT;
}

/******************************************************************************
 *
 ******************************************************************************/
bool SwnrPluginProviderImp::onDequeRequest() {
  FUNCTION_IN;
  std::unique_lock<std::mutex> lck(mFutureLock);
  while (mvFutures.empty() && !mbRequestExit) {
    MY_LOGD("NR onDequeRequest waiting ...");
    mFutureCond.wait(lck);
    MY_LOGD("NR onDequeRequest waiting done");
  }
  if (mbRequestExit) {
    MY_LOGD("[flush] mvFutures.size:%zu", mvFutures.size());
    return false;
  }
  FUNCTION_OUT;
  return true;
}
/******************************************************************************
 *
 ******************************************************************************/
bool SwnrPluginProviderImp::onProcessFuture() {
  FUNCTION_IN;
  std::future<int32_t> task;

  for (map<RequestPtr, std::future<int32_t>>::iterator it = mvFutures.begin();
       it != mvFutures.end();) {
    // handle queue in processing time
    {
      std::lock_guard<std::mutex> _l(mFutureLock);
      task = std::move((*it).second);
      it = mvFutures.erase(it);
    }
    if (task.valid()) {
      int32_t status = task.get();  // processing
      MY_LOGE_IF(status != OK, "status: %d:%s", status, ::strerror(-status));
    } else {
      MY_LOGW("task is not valid?");
    }
  }

  FUNCTION_OUT;
  return true;
}

/******************************************************************************
 *
 ******************************************************************************/
int32_t SwnrPluginProviderImp::doSwnr(RequestPtr const pRequest) {
  CAM_TRACE_NAME("SwnrPluginProviderImp:doSwnr");
  FUNCTION_IN;
  int32_t err = OK;

  // buffer
  IImageBuffer* pIBuffer_MainFull = nullptr;
  IImageBuffer* pOBuffer_Full = nullptr;

  if (pRequest->mIBufferFull != nullptr) {
    pIBuffer_MainFull = pRequest->mIBufferFull->acquire(
        eBUFFER_USAGE_SW_READ_MASK | eBUFFER_USAGE_SW_WRITE_MASK);
    MY_LOGD("\tMain Full img VA: 0x%x", pIBuffer_MainFull->getBufVA(0));
  }

  if (pRequest->mOBufferFull != nullptr) {
    pOBuffer_Full = pRequest->mOBufferFull->acquire();
    MY_LOGD("\tOut Full img VA: 0x%x", pOBuffer_Full->getBufVA(0));
  }

  // meta
  IMetadata* pIMetadata_P1 = nullptr;
  IMetadata* pIMetadata_App = nullptr;
  IMetadata* pIMetadata_Hal = nullptr;
  IMetadata* pOMetadata_Hal = nullptr;

  if (pRequest->mIMetadataDynamic != nullptr) {
    pIMetadata_P1 = pRequest->mIMetadataDynamic->acquire();
    if (pIMetadata_P1 != nullptr) {
      MY_LOGD("\tIn APP P1 meta count: 0x%x", pIMetadata_P1->count());
    }
  }
  if (pRequest->mIMetadataApp != nullptr) {
    pIMetadata_App = pRequest->mIMetadataApp->acquire();
    if (pIMetadata_App != nullptr) {
      MY_LOGD("\tIn APP meta count: 0x%x", pIMetadata_App->count());
    }
  }
  if (pRequest->mIMetadataHal != nullptr) {
    pIMetadata_Hal = pRequest->mIMetadataHal->acquire();
    if (pIMetadata_Hal != nullptr) {
      MY_LOGD("\tIn HAL meta count: 0x%x", pIMetadata_Hal->count());
    }
  }
  if (pRequest->mOMetadataHal != nullptr && pIMetadata_Hal != nullptr) {
    pOMetadata_Hal = pRequest->mOMetadataHal->acquire();
    if (pOMetadata_Hal != nullptr) {
      MY_LOGD("\tOut HAL meta count: 0x%x", pOMetadata_Hal->count());
      (*pOMetadata_Hal) += (*pIMetadata_Hal);
    }
  }

  // dump
  if (muDumpBuffer) {
    char filename[256] = {0};
    snprintf(filename, sizeof(filename), "%s/swnr_in_%d_%d.yv12", DUMP_PATH,
             pIBuffer_MainFull->getImgSize().w,
             pIBuffer_MainFull->getImgSize().h);
    MY_LOGD("[swnr][in] filename = %s", filename);
    pIBuffer_MainFull->saveToFile(filename);
  }

  if (mpSwnr == nullptr) {
    CAM_TRACE_BEGIN("SwnrPluginProviderImp:MAKE_SwNR instance");
    mpSwnr = MAKE_SWNR_IPC(mOpenId);
    CAM_TRACE_END();
  }

  if (mpSwnr) {
    MINT32 iso = -1;
    MINT32 magicNo = -1;
    if (pIMetadata_Hal) {
      tryGetMetadata<MINT32>(pIMetadata_Hal, MTK_P1NODE_PROCESSOR_MAGICNUM,
                             &magicNo);
    }
    if (pIMetadata_P1) {
      tryGetMetadata<MINT32>(pIMetadata_P1, MTK_SENSOR_SENSITIVITY, &iso);
    }
    MY_LOGD("magicNo:%d iso:%d", magicNo, iso);

    ISwNR::SWNRParam swnrParam;
    swnrParam.iso = iso;
    // TODO(MTK): FIXME
    swnrParam.isMfll = 0;

    CAM_TRACE_BEGIN("SwnrPluginProviderImp:SwNR processing");

    MY_LOGD("SWNR processing +");

    if (!mpSwnr->doSwNR(swnrParam, pIBuffer_MainFull)) {
      MY_LOGE("SWNR failed");
      err = BAD_VALUE;
    } else {
      if (pOMetadata_Hal != nullptr) {
        mpSwnr->getDebugInfo(pOMetadata_Hal);
      } else {
        MY_LOGD("no hal metadata for dumping debug info");
      }
    }
    MY_LOGD("SWNR processing -");
    if (muDumpBuffer) {
      char filename[256] = {0};
      snprintf(filename, sizeof(filename), "%s/swnr_out_%d_%d.yv12", DUMP_PATH,
               pIBuffer_MainFull->getImgSize().w,
               pIBuffer_MainFull->getImgSize().h);
      MY_LOGD("[swnr][out] filename = %s", filename);
      pIBuffer_MainFull->saveToFile(filename);
    }

    CAM_TRACE_END();
  }

  FUNCTION_OUT;

  return err;
}

void SwnrPluginProviderImp::threadLoop() {
  FUNCTION_IN;
  MY_LOGD("run in new thread....");

  if (mpSwnr == NULL) {
    CAM_TRACE_BEGIN("NRplugin:MAKE_SwNR instance");
    if (mOpenId < 0) {
      MY_LOGE("need set openId before init() !!");
    } else {
      mpSwnr = MAKE_SWNR_IPC(mOpenId);
    }
    CAM_TRACE_END();
  }
  while (onDequeRequest()) {
    onProcessFuture();
    std::lock_guard<std::mutex> _l(mFutureLock);
    if (mbRequestExit) {
      MY_LOGD("request to exit.");
      break;
    }
  }
  FUNCTION_OUT;
}
