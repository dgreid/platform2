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

#define LOG_TAG "mtkcam-FeatureSettingPolicy"
//
#include <algorithm>
#include <tuple>
// MTKCAM
#include <mtkcam/utils/std/Log.h>
#include <mtkcam/utils/std/Trace.h>
#include <mtkcam/utils/hw/HwInfoHelper.h>
#include <mtkcam/utils/metastore/IMetadataProvider.h>
#include <mtkcam/utils/metadata/client/mtk_metadata_tag.h>
#include <mtkcam/utils/metadata/hal/mtk_platform_metadata_tag.h>
#include <mtkcam/utils/LogicalCam/IHalLogicalDeviceList.h>

#include <mtkcam/feature/eis/eis_ext.h>
#include <mtkcam/feature/3dnr/3dnr_defs.h>
#include <mtkcam/feature/utils/FeatureProfileHelper.h>

#include <mtkcam/aaa/IHal3A.h>
#include <camera_custom_eis.h>
#include <camera_custom_3dnr.h>
// dual cam
#include <mtkcam/aaa/ISync3A.h>
#include <isp_tuning.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
//
#include "../model/utils/include/impl/ScenarioControl.h"
#include "mtkcam/pipeline/policy/FeatureSettingPolicy.h"
//
#include "MyUtils.h"
/******************************************************************************
 *
 ******************************************************************************/
/******************************************************************************
 *
 ******************************************************************************/
// TODO(MTK): rename
#define SENSOR_INDEX_MAIN (0)
#define SENSOR_INDEX_SUB1 (1)
#define SENSOR_INDEX_SUB2 (2)
// TODO(MTK): disable thsi flag before MP
#define DEBUG_FEATURE_SETTING_POLICY (0)
#define DEBUG_EISEM (0)
#define DEBUG_EIS30 (0)
#ifndef NR3D_SUPPORTED
#define FORCE_3DNR (0)
#else
#define FORCE_3DNR (1)
#endif
#define DEBUG_TSQ (0)
#define DEBUG_VHDR (0)
#define DEBUG_APP_HDR (-1)
#define DEBUG_DUMMY_HDR (1)

/******************************************************************************
 *
 ******************************************************************************/
template <typename TPlugin>
class PluginWrapper {
 public:
  using PluginPtr = typename TPlugin::Ptr;
  using ProviderPtr = typename TPlugin::IProvider::Ptr;
  using InterfacePtr = typename TPlugin::IInterface::Ptr;
  using SelectionPtr = typename TPlugin::Selection::Ptr;
  using Selection = typename TPlugin::Selection;
  using Property = typename TPlugin::Property;
  using ProviderPtrs = std::vector<ProviderPtr>;

 public:
  PluginWrapper(const std::string& name, MINT32 iOpenId, MINT32 iOpenId2 = -1);
  ~PluginWrapper();

 public:
  auto getName() const -> const std::string&;
  auto isKeyFeatureExisting(MINT64 combinedKeyFeature) const -> MBOOL;
  auto tryGetKeyFeature(MINT64 combinedKeyFeature, MINT64* keyFeature) const
      -> MBOOL;
  auto getProvider(MINT64 combinedKeyFeature) -> ProviderPtr;
  auto getProviders() -> ProviderPtrs;
  auto createSelection() const -> SelectionPtr;
  auto offer(Selection* sel) const -> MVOID;
  auto keepSelection(const uint32_t requestNo,
                     ProviderPtr const& providerPtr,
                     SelectionPtr const& sel) -> MVOID;
  auto pushSelection() -> MVOID;
  auto cancel() -> MVOID;

 private:
  using ProviderPtrMap = std::unordered_map<MUINT64, ProviderPtr>;
  using SelectionPtrMap =
      std::unordered_map<ProviderPtr, std::vector<SelectionPtr>>;

 private:
  const std::string mName;
  const MINT32 mOpenId1;
  const MINT32 mOpenId2;
  PluginPtr mInstancePtr;
  ProviderPtrMap mProviderPtrMap;
  SelectionPtrMap mTempSelectionPtrMap;
  InterfacePtr mInterfacePtr;
};

/******************************************************************************
 *
 ******************************************************************************/
template <typename TPlugin>
PluginWrapper<TPlugin>::PluginWrapper(const std::string& name,
                                      MINT32 iOpenId,
                                      MINT32 iOpenId2)
    : mName(name), mOpenId1(iOpenId), mOpenId2(iOpenId2) {
  MY_LOGD("ctor, name:%s, openId:%d, openId2:%d", mName.c_str(), mOpenId1,
          mOpenId2);
  mInstancePtr = TPlugin::getInstance(mOpenId1, mOpenId2);
  if (mInstancePtr) {
    mInterfacePtr = mInstancePtr->getInterface();
    auto& providers = mInstancePtr->getProviders();
    for (auto& provider : providers) {
      const Property& property = provider->property();
      if (mProviderPtrMap.find(property.mFeatures) == mProviderPtrMap.end()) {
        mProviderPtrMap[property.mFeatures] =
            provider;  // new plugin provider, add to map
        MY_LOGD("find provider... name:%s, algo(%#" PRIx64 ")", property.mName,
                property.mFeatures);
      } else {
        MY_LOGW("detect same provider... name:%s, algo(%#" PRIx64
                ") in the same interface",
                property.mName, property.mFeatures);
      }
    }
  } else {
    MY_LOGW("cannot get instance for key feature strategy");
  }
}

template <typename TPlugin>
PluginWrapper<TPlugin>::~PluginWrapper() {
  MY_LOGD("dtor, name:%s, openId:%d, openId2:%d", mName.c_str(), mOpenId1,
          mOpenId2);
}

template <typename TPlugin>
auto PluginWrapper<TPlugin>::getName() const -> const std::string& {
  return mName;
}

template <typename TPlugin>
auto PluginWrapper<TPlugin>::isKeyFeatureExisting(
    MINT64 combinedKeyFeature) const -> MBOOL {
  MINT32 keyFeature = 0;
  return tryGetKeyFeature(combinedKeyFeature, &keyFeature);
}

template <typename TPlugin>
auto PluginWrapper<TPlugin>::tryGetKeyFeature(MINT64 combinedKeyFeature,
                                              MINT64* keyFeature) const
    -> MBOOL {
  return std::find_if(mProviderPtrMap.begin(), mProviderPtrMap.end(),
                      [combinedKeyFeature, keyFeature](
                          const typename ProviderPtrMap::value_type& v) {
                        *keyFeature = v.first;
                        if ((*keyFeature & combinedKeyFeature) != 0) {
                          return MTRUE;
                        } else {
                          *keyFeature = 0;  // hint no feature be chose.
                          return MFALSE;
                        }
                      }) != mProviderPtrMap.end();
}

template <typename TPlugin>
auto PluginWrapper<TPlugin>::getProvider(MINT64 combinedKeyFeature)
    -> ProviderPtr {
  MINT64 keyFeature = 0;
  return tryGetKeyFeature(combinedKeyFeature, &keyFeature)
             ? mProviderPtrMap[keyFeature]
             : nullptr;
}

template <typename TPlugin>
auto PluginWrapper<TPlugin>::getProviders() -> ProviderPtrs {
  ProviderPtrs ret;
  for (auto item : mProviderPtrMap) {
    ret.push_back(item.second);
  }
  return std::move(ret);
}

template <typename TPlugin>
auto PluginWrapper<TPlugin>::createSelection() const -> SelectionPtr {
  return mInstancePtr->createSelection();
}

template <typename TPlugin>
auto PluginWrapper<TPlugin>::offer(Selection* sel) const -> MVOID {
  mInterfacePtr->offer(sel);
}

template <typename TPlugin>
auto PluginWrapper<TPlugin>::keepSelection(const uint32_t requestNo,
                                           ProviderPtr const& providerPtr,
                                           SelectionPtr const& sel) -> MVOID {
  if (mTempSelectionPtrMap.find(providerPtr) != mTempSelectionPtrMap.end()) {
    mTempSelectionPtrMap[providerPtr].push_back(sel);
    MY_LOGD("%s: selection size:%zu, requestNo:%u", getName().c_str(),
            mTempSelectionPtrMap[providerPtr].size(), requestNo);
  } else {
    std::vector<SelectionPtr> vSelection;
    vSelection.push_back(sel);
    mTempSelectionPtrMap[providerPtr] = vSelection;
    MY_LOGD("%s: new selection size:%zu, requestNo:%u", getName().c_str(),
            mTempSelectionPtrMap[providerPtr].size(), requestNo);
  }
}

template <typename TPlugin>
auto PluginWrapper<TPlugin>::pushSelection() -> MVOID {
  for (auto item : mTempSelectionPtrMap) {
    auto providerPtr = item.first;
    auto vSelection = item.second;
    MY_LOGD("%s: selection size:%zu", getName().c_str(), vSelection.size());
    for (auto sel : vSelection) {
      mInstancePtr->pushSelection(providerPtr, sel);
    }
    vSelection.clear();
  }
  mTempSelectionPtrMap.clear();
}

template <typename TPlugin>
auto PluginWrapper<TPlugin>::cancel() -> MVOID {
  for (auto item : mTempSelectionPtrMap) {
    auto providerPtr = item.first;
    auto vSelection = item.second;
    MY_LOGD("%s: selection size:%zu", getName().c_str(), vSelection.size());
    vSelection.clear();
  }
  mTempSelectionPtrMap.clear();
}

#define DEFINE_PLUGINWRAPER(CLASSNAME, PLUGINNAME)                          \
  class NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy:: \
      CLASSNAME final                                                       \
      : public PluginWrapper<NSCam::NSPipelinePlugin::PLUGINNAME> {         \
   public:                                                                  \
    explicit CLASSNAME(MINT32 iOpenId, MINT32 iOpenId2 = -1)                \
        : PluginWrapper<NSCam::NSPipelinePlugin::PLUGINNAME>(               \
              #PLUGINNAME, iOpenId, iOpenId2) {}                            \
  }
DEFINE_PLUGINWRAPER(RawPluginWrapper, RawPlugin);
DEFINE_PLUGINWRAPER(YuvPluginWrapper, YuvPlugin);
#undef DEFINE_PLUGINWRAPER

/******************************************************************************
 *
 ******************************************************************************/
NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    FeatureSettingPolicy(CreationParams const& params)
    : mPolicyParams(params) {
  MY_LOGI(
      "create feature setting policy instance for openId(%d), "
      "sensors_count(%zu)",
      mPolicyParams.pPipelineStaticInfo->openId,
      mPolicyParams.pPipelineStaticInfo->sensorIds.size());
  mbDebug = ::property_get_int32("vendor.debug.camera.featuresetting.log",
                                 DEBUG_FEATURE_SETTING_POLICY);
  // forced feature strategy for debug
  mForcedKeyFeatures =
      ::property_get_int32("vendor.debug.featuresetting.keyfeature", -1);
  mForcedFeatureCombination = ::property_get_int32(
      "vendor.debug.featuresetting.featurecombination", -1);
  // get multiple frames key features from  multi-frame plugin providers
  auto mainSensorId =
      mPolicyParams.pPipelineStaticInfo->sensorIds[SENSOR_INDEX_MAIN];
  //
  mRawPluginWrapperPtr = std::make_shared<RawPluginWrapper>(mainSensorId);
  mYuvPluginWrapperPtr = std::make_shared<YuvPluginWrapper>(mainSensorId);
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    collectParsedStrategyInfo(ParsedStrategyInfo* parsedInfo,
                              RequestInputParams const* in) -> bool {
  bool ret = true;
  // collect parsed info for common strategy (only per-frame requirement)
  // Note: It is per-frames query behavior here.
  // TODO(MTK): collect parsed info for common strategy
  // collect parsed info for capture feature strategy
  if (in->needP2CaptureNode) {
    if (CC_UNLIKELY(in->pRequest_ParsedAppMetaControl == nullptr)) {
      MY_LOGW("cannot get ParsedMetaControl, invalid nullptr");
    } else {
      parsedInfo->isZslModeOn = mConfigInputParams.isZslMode;
      parsedInfo->isZslFlowOn =
          in->pRequest_ParsedAppMetaControl->control_enableZsl;
    }
    // obtain latest real iso information for capture strategy
    {
      NS3Av3::CaptureParam_T captureParam;
      static std::mutex _locker;
      std::shared_ptr<NS3Av3::IHal3A> hal3a;
      MAKE_Hal3A(
          hal3a, [](NS3Av3::IHal3A* p) { p->destroyInstance(LOG_TAG); },
          mPolicyParams.pPipelineStaticInfo->sensorIds[SENSOR_INDEX_MAIN],
          LOG_TAG);

      if (hal3a.get()) {
        std::lock_guard<std::mutex> _l(_locker);
        NS3Av3::ExpSettingParam_T expParam;
        hal3a->send3ACtrl(NS3Av3::E3ACtrl_GetExposureInfo, (MINTPTR)&expParam,
                          0);
        hal3a->send3ACtrl(NS3Av3::E3ACtrl_GetExposureParam,
                          (MINTPTR)&captureParam, 0);
      } else {
        MY_LOGE(
            "create IHal3A instance failed! cannot get current real iso for "
            "strategy");
        ::memset(&captureParam, 0, sizeof(NS3Av3::CaptureParam_T));
        ret = false;
      }
      parsedInfo->realIso = captureParam.u4RealISO;
      parsedInfo->exposureTime = captureParam.u4Eposuretime;  // us

      // query flash status from Hal3A
      int isFlashOn = 0;
      hal3a->send3ACtrl(NS3Av3::E3ACtrl_GetIsFlashOnCapture,
                        (MINTPTR)&isFlashOn, 0);
      if (isFlashOn) {
        parsedInfo->isFlashOn = true;
      }
    }
    // get info from AppControl Metadata
    {
      auto pAppMetaControl = in->pRequest_AppControl;
      MUINT8 aeState = MTK_CONTROL_AE_STATE_INACTIVE;
      MUINT8 aeMode = MTK_CONTROL_AE_MODE_OFF;
      if (!IMetadata::getEntry<MUINT8>(pAppMetaControl, MTK_CONTROL_AE_MODE,
                                       &aeMode)) {
        MY_LOGW(
            "get metadata MTK_CONTROL_AE_MODE failed! cannot get current  "
            "state for strategy");
      } else {
        MINT32 manualIso = 0;
        MINT64 manualExposureTime = 0;
        if (aeMode == MTK_CONTROL_AE_MODE_OFF) {
          IMetadata::getEntry<MINT32>(pAppMetaControl, MTK_SENSOR_SENSITIVITY,
                                      &manualIso);
          IMetadata::getEntry<MINT64>(pAppMetaControl, MTK_SENSOR_EXPOSURE_TIME,
                                      &manualExposureTime);
          if (manualIso > 0 && manualExposureTime > 0) {
            MY_LOGI("it is manual iso(%d)/exposure(%" PRId64
                    " ns) as capture feature strategy info.",
                    manualIso, manualExposureTime);
            parsedInfo->realIso = manualIso;
            parsedInfo->exposureTime = manualExposureTime / 1000;  // ns to us
          } else {
            MY_LOGW("invaild manual iso(%d)/exposure(%" PRId64
                    ") for manual AE",
                    manualIso, manualExposureTime);
            // choose the previous default preview 3A info as capture feature
            // strategy
          }
        }
      }
      if (!IMetadata::getEntry<MUINT8>(pAppMetaControl, MTK_CONTROL_AE_STATE,
                                       &aeState)) {
        MY_LOGD(
            "get metadata MTK_CONTROL_AE_STATE failed! cannot get current "
            "flash state for strategy");
      }
      if (aeMode == MTK_CONTROL_AE_MODE_ON_ALWAYS_FLASH ||
          aeState == MTK_CONTROL_AE_STATE_FLASH_REQUIRED) {
        parsedInfo->isFlashOn = true;
      }
      MINT32 cshot = 0;
      if (IMetadata::getEntry<MINT32>(pAppMetaControl,
                                      MTK_CSHOT_FEATURE_CAPTURE, &cshot)) {
        if (cshot) {
          parsedInfo->isCShot = true;
        }
      }
    }

    // after doing capture, vhdr need to add dummy frame
    if (ret && mVhdrInfo.curAppHdrMode == HDRMode::VIDEO_ON) {
      mVhdrInfo.bIsDoCapture = true;
      MY_LOGD(
          "[vhdrDummyFrames] (vhdr_on): after doing capture, vhdr need to add "
          "dummy frame");
    }
    MY_LOGD(
        "strategy info for capture feature(isZsl(mode:%d, flow:%d), "
        "isCShot:%d, isFlashOn:%d, iso:%d, shutterTimeUs:%d)",
        parsedInfo->isZslModeOn, parsedInfo->isZslFlowOn, parsedInfo->isCShot,
        parsedInfo->isFlashOn, parsedInfo->realIso, parsedInfo->exposureTime);
  }
  // collect parsed strategy info for stream feature
  if (in->needP2StreamNode) {
    // Note: It is almost per-frames query behavior here.
    // TODO(MTK): collect parsed strategy info for stream feature

    // obtain latest real iso information for VHDR strategy
    if (mVhdrInfo.UiAppHdrMode == HDRMode::VIDEO_ON ||
        mVhdrInfo.UiAppHdrMode == HDRMode::VIDEO_AUTO) {
      NS3Av3::CaptureParam_T captureParam;
      static std::mutex _locker;
      std::shared_ptr<NS3Av3::IHal3A> hal3a;
      MAKE_Hal3A(
          hal3a, [](NS3Av3::IHal3A* p) { p->destroyInstance(LOG_TAG); },
          mPolicyParams.pPipelineStaticInfo->sensorIds[SENSOR_INDEX_MAIN],
          LOG_TAG);

      if (hal3a.get()) {
        std::lock_guard<std::mutex> _l(_locker);
        NS3Av3::ExpSettingParam_T expParam;
        hal3a->send3ACtrl(NS3Av3::E3ACtrl_GetExposureInfo, (MINTPTR)&expParam,
                          0);
        hal3a->send3ACtrl(NS3Av3::E3ACtrl_GetExposureParam,
                          (MINTPTR)&captureParam, 0);
      } else {
        MY_LOGE(
            "create IHal3A instance failed! cannot get current real iso for "
            "strategy");
        ::memset(&captureParam, 0, sizeof(NS3Av3::CaptureParam_T));
        ret = false;
      }
      parsedInfo->realIso = captureParam.u4RealISO;
      parsedInfo->exposureTime = captureParam.u4Eposuretime;

      MY_LOGD_IF(
          mVhdrInfo.bVhdrDebugMode,
          "stream strategy info for VHDR feature(iso:%d, shutterTimeUs:%d)",
          parsedInfo->realIso, parsedInfo->exposureTime);
    }
  }
  return ret;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    getCaptureP1DmaConfig(uint32_t* needP1Dma,
                          RequestInputParams const* in,
                          uint32_t sensorIndex = 0) -> bool {
  bool ret = true;
  // IMGO
  if ((*(in->pConfiguration_StreamInfo_P1))[sensorIndex].pHalImage_P1_Imgo !=
      nullptr) {
    *needP1Dma |= P1_IMGO;
  } else {
    MY_LOGI("The current pipeline config without IMGO output");
  }
  // RRZO
  if ((*(in->pConfiguration_StreamInfo_P1))[sensorIndex].pHalImage_P1_Rrzo !=
      nullptr) {
    *needP1Dma |= P1_RRZO;
  } else {
    MY_LOGI("The current pipeline config without RRZO output");
  }
  // LCSO
  if ((*(in->pConfiguration_StreamInfo_P1))[sensorIndex].pHalImage_P1_Lcso !=
      nullptr) {
    *needP1Dma |= P1_LCSO;
  } else {
    MY_LOGD("The current pipeline config without LCSO output");
  }
  if (!(*needP1Dma & (P1_IMGO | P1_RRZO))) {
    MY_LOGW("P1Dma output without source buffer, sensorIndex(%u)", sensorIndex);
    ret = false;
  }
  return ret;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    updateRequestResultParams(
        std::shared_ptr<RequestResultParams>* requestParams,
        NSCam::NSPipelinePlugin::MetadataPtr pOutMetaApp_Additional,
        NSCam::NSPipelinePlugin::MetadataPtr pOutMetaHal_Additional,
        uint32_t needP1Dma,
        uint32_t sensorIndex,
        MINT64 featureCombination,
        MINT32 requestIndex,
        MINT32 requestCount) -> bool {
  auto sensorNum = mPolicyParams.pPipelineStaticInfo->sensorIds.size();
  if (sensorIndex >= sensorNum) {
    MY_LOGE("sensorIndex:%u is out of current open sensor num:%zu", sensorIndex,
            sensorNum);
    return false;
  }
  auto sensorId = mPolicyParams.pPipelineStaticInfo->sensorIds[sensorIndex];
  MY_LOGD_IF(
      2 <= mbDebug,
      "updateRequestResultParams for sensorIndex:%u (physical sensorId:%d)",
      sensorIndex, sensorId);
  NSCam::NSPipelinePlugin::MetadataPtr pOutMetaApp =
      std::make_shared<IMetadata>();
  NSCam::NSPipelinePlugin::MetadataPtr pOutMetaHal =
      std::make_shared<IMetadata>();
  if (pOutMetaApp_Additional.get() != nullptr) {
    *pOutMetaApp += *pOutMetaApp_Additional;
  }
  if (pOutMetaHal_Additional.get() != nullptr) {
    *pOutMetaHal += *pOutMetaHal_Additional;
  }
  // check ISP profile
  MUINT8 ispProfile = 0;
  if (IMetadata::getEntry<MUINT8>(pOutMetaHal.get(), MTK_3A_ISP_PROFILE,
                                  &ispProfile)) {
    MY_LOGD_IF(2 <= mbDebug, "updated isp profile(%d)", ispProfile);
  } else {
    MY_LOGD_IF(2 <= mbDebug, "no updated isp profile in pOutMetaHal");
  }
  if (featureCombination) {
    MY_LOGD_IF(2 <= mbDebug, "update featureCombination=%#" PRIx64 "",
               featureCombination);
    IMetadata::setEntry<MINT64>(pOutMetaHal.get(), MTK_FEATURE_CAPTURE,
                                featureCombination);
  }
  if (requestIndex || requestCount) {
    MY_LOGD_IF(2 <= mbDebug,
               "update MTK_HAL_REQUEST_INDEX=%d, MTK_HAL_REQUEST_COUNT=%d",
               requestIndex, requestCount);
    IMetadata::setEntry<MINT32>(pOutMetaHal.get(), MTK_HAL_REQUEST_INDEX,
                                requestIndex);
    IMetadata::setEntry<MINT32>(pOutMetaHal.get(), MTK_HAL_REQUEST_COUNT,
                                requestCount);
  }
  if (CC_UNLIKELY(2 <= mbDebug)) {
    (*pOutMetaApp).dump();
    (*pOutMetaHal).dump();
  }
  const MBOOL isMainSensorIndex = (sensorIndex == SENSOR_INDEX_MAIN);
  if ((*requestParams).get() == nullptr) {
    MY_LOGD_IF(2 <= mbDebug, "no frame setting, create a new one");
    (*requestParams) = std::make_shared<RequestResultParams>();
    // App Metadata, just main sensor has the app metadata
    if (isMainSensorIndex) {
      (*requestParams)->additionalApp = pOutMetaApp;
    }
    // HAl Metadata
    (*requestParams)->vAdditionalHal.push_back(pOutMetaHal);
    // P1Dma requirement
    if (sensorIndex >= (*requestParams)->needP1Dma.size()) {
      MY_LOGD_IF(2 <= mbDebug,
                 "resize needP1Dma size to compatible with sensor index:%u",
                 sensorIndex);
      (*requestParams)->needP1Dma.resize(sensorIndex + 1);
    }
    (*requestParams)->needP1Dma[sensorIndex] |= needP1Dma;
  } else {
    MY_LOGD_IF(2 <= mbDebug,
               "frame setting has been created by previous policy, update it");
    // App Metadata, just main sensor has the app metadata
    if (isMainSensorIndex) {
      if ((*requestParams)->additionalApp.get() != nullptr) {
        MY_LOGD_IF(2 <= mbDebug, "[App] append the setting");
        *((*requestParams)->additionalApp) += *pOutMetaApp;
      } else {
        MY_LOGD_IF(2 <= mbDebug, "no app metadata, use the new one");
        (*requestParams)->additionalApp = pOutMetaApp;
      }
    }
    // HAl Metadata
    MY_LOGD_IF(2 <= mbDebug, "[Hal] metadata size(%zu) %d",
               (*requestParams)->vAdditionalHal.size(), sensorIndex);
    auto additionalHalSize = (*requestParams)->vAdditionalHal.size();
    if (sensorIndex >= additionalHalSize) {
      MY_LOGD_IF(2 <= mbDebug,
                 "resize hal meta size to compatible with sensor index:%u",
                 sensorIndex);
      (*requestParams)->vAdditionalHal.resize(sensorIndex + 1, nullptr);
      (*requestParams)->vAdditionalHal[sensorIndex] = pOutMetaHal;
    } else if ((*requestParams)->vAdditionalHal[sensorIndex].get() != nullptr) {
      MY_LOGD_IF(2 <= mbDebug, "[Hal] append the setting");
      *((*requestParams)->vAdditionalHal[sensorIndex]) += *pOutMetaHal;
    } else {
      (*requestParams)->vAdditionalHal[sensorIndex] = pOutMetaHal;
    }
    // P1Dma requirement
    if (sensorIndex >= (*requestParams)->needP1Dma.size()) {
      MY_LOGD_IF(2 <= mbDebug,
                 "resize needP1Dma size to compatible with sensor index:%u",
                 sensorIndex);
      (*requestParams)->needP1Dma.resize(sensorIndex + 1);
    }
    (*requestParams)->needP1Dma[sensorIndex] |= needP1Dma;
  }
  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    querySelectionStrategyInfo(
        NSCam::NSPipelinePlugin::StrategyInfo* strategyInfo,
        uint32_t sensorIndex,
        ParsedStrategyInfo const& parsedInfo,
        RequestOutputParams const* out,
        RequestInputParams const* in) -> bool {
  strategyInfo->isZslModeOn = parsedInfo.isZslModeOn;
  strategyInfo->isZslFlowOn = parsedInfo.isZslFlowOn;
  strategyInfo->isFlashOn = parsedInfo.isFlashOn;
  strategyInfo->exposureTime = parsedInfo.exposureTime;
  strategyInfo->realIso = parsedInfo.realIso;
  strategyInfo->customHint = parsedInfo.customHint;
  // get sensor info (the info is after reconfigure if need)
  strategyInfo->sensorId =
      mPolicyParams.pPipelineStaticInfo->sensorIds[sensorIndex];
  strategyInfo->sensorMode = out->sensorModes[sensorIndex];
  uint32_t needP1Dma = 0;
  if (!getCaptureP1DmaConfig(&needP1Dma, in, sensorIndex)) {
    MY_LOGE("P1Dma output is invalid: 0x%X", needP1Dma);
    return false;
  }
  NSCamHW::HwInfoHelper helper(strategyInfo->sensorId);
  if (!helper.updateInfos()) {
    MY_LOGE("HwInfoHelper cannot properly update infos");
    return false;
  }
  //
  uint32_t pixelMode = 0;
  if (!helper.getSensorSize(strategyInfo->sensorMode,
                            &strategyInfo->sensorSize) ||
      !helper.getSensorFps(strategyInfo->sensorMode,
                           &strategyInfo->sensorFps) ||
      !helper.queryPixelMode(strategyInfo->sensorMode, strategyInfo->sensorFps,
                             &pixelMode)) {
    MY_LOGE("cannot get params about sensor");
    return false;
  }
  //
  int32_t bitDepth = 10;
  helper.getRecommendRawBitDepth(&bitDepth);
  //
  strategyInfo->rawSize = strategyInfo->sensorSize;
  MINT format;
  size_t stride;
  if (needP1Dma & P1_IMGO) {
    // use IMGO as source for capture
    if (!helper.getImgoFmt(bitDepth, &format) ||
        !helper.alignPass1HwLimitation(pixelMode, format, true /*isImgo*/,
                                       &strategyInfo->rawSize, &stride)) {
      MY_LOGE("cannot query raw buffer info about imgo");
      return false;
    }
  } else {
    // use RRZO as source for capture
    auto rrzoSize = (*(in->pConfiguration_StreamInfo_P1))[sensorIndex]
                        .pHalImage_P1_Rrzo->getImgSize();
    strategyInfo->rawSize = rrzoSize;
    MY_LOGW(
        "no IMGO buffer, use RRZO as capture source image (for better quality, "
        "not suggest to use RRZO to capture)");
  }
  MY_LOGD(
      "isZslFlowOn:%d, isFlashOn:%d, exposureTime:%u, realIso:%u, "
      "customHint:%u",
      strategyInfo->isZslFlowOn, strategyInfo->isFlashOn,
      strategyInfo->exposureTime, strategyInfo->realIso,
      strategyInfo->customHint);
  MY_LOGD("sensor(Id:%d, mode:%u, fps:%d, size(%d, %d), capture raw(%d,%d))",
          strategyInfo->sensorId, strategyInfo->sensorMode,
          strategyInfo->sensorFps, strategyInfo->sensorSize.w,
          strategyInfo->sensorSize.h, strategyInfo->rawSize.w,
          strategyInfo->rawSize.h);
  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    updateRequestInfo(RequestOutputParams* out,
                      uint32_t sensorIndex,
                      NSCam::NSPipelinePlugin::RequestInfo const& requestInfo,
                      RequestInputParams const* in __unused) -> bool {
  out->needZslFlow = requestInfo.needZslFlow;
  out->zslPolicyParams.mPolicy = requestInfo.zslPolicyParams.mPolicy;
  out->zslPolicyParams.mTimestamp = requestInfo.zslPolicyParams.mTimestamp;
  out->zslPolicyParams.mTimeouts = requestInfo.zslPolicyParams.mTimeouts;
  if (out->needZslFlow) {
    MY_LOGD("update needZslFlow(%d), zsl policy(0x%X), timestamp:%" PRId64
            ", timeouts:%" PRId64 "",
            out->needZslFlow, out->zslPolicyParams.mPolicy,
            out->zslPolicyParams.mTimestamp, out->zslPolicyParams.mTimeouts);
  }
  if (requestInfo.sensorMode != SENSOR_SCENARIO_ID_UNNAMED_START) {
    out->sensorModes[sensorIndex] = requestInfo.sensorMode;
    MY_LOGD("feature request sensorMode:%d", requestInfo.sensorMode);
  }
  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    updateDualCamRequestOutputParams(
        RequestOutputParams* out,
        NSCam::NSPipelinePlugin::MetadataPtr pOutMetaApp_Additional,
        NSCam::NSPipelinePlugin::MetadataPtr pOutMetaHal_Additional,
        uint32_t mainCamP1Dma,
        uint32_t sub1CamP1Dma,
        MINT64 featureCombination) -> bool {
  // TODO(MTK): what value does dualcam need to assign
  if (out->needZslFlow == MTRUE) {
    out->zslPolicyParams.mPolicy |= eZslPolicy_DualFrameSync;
  }
  // update mainFrame
  // main1 mainFrame
  updateRequestResultParams(&out->mainFrame, pOutMetaApp_Additional,
                            pOutMetaHal_Additional, mainCamP1Dma,
                            SENSOR_INDEX_MAIN, featureCombination);
  // main2 mainFrame
  updateRequestResultParams(&out->mainFrame,
                            nullptr,  // sub sensor no need to set app metadata
                            pOutMetaHal_Additional, sub1CamP1Dma,
                            SENSOR_INDEX_SUB1, featureCombination);
  // update subFrames
  MY_LOGI("update subFrames size(%zu)", out->subFrames.size());
  for (size_t i = 0; i < out->subFrames.size(); i++) {
    auto subFrame = out->subFrames[i];
    if (subFrame.get()) {
      MY_LOGI("subFrames[%zu] has existed(addr:%p)", i, subFrame.get());
      // main1 subFrame
      updateRequestResultParams(&subFrame, pOutMetaApp_Additional,
                                pOutMetaHal_Additional, mainCamP1Dma,
                                SENSOR_INDEX_MAIN, featureCombination);
      // main2 subFrame
      updateRequestResultParams(
          &subFrame,
          nullptr,  // sub sensor no need to set app metadata
          pOutMetaHal_Additional, sub1CamP1Dma, SENSOR_INDEX_SUB1,
          featureCombination);
    } else {
      MY_LOGE("subFrames[%zu] is invalid", i);
    }
  }
  // update preDummyFrames
  MY_LOGI("update preDummyFrames size(%zu)", out->preDummyFrames.size());
  for (size_t i = 0; i < out->preDummyFrames.size(); i++) {
    auto preDummyFrame = out->preDummyFrames[i];
    if (preDummyFrame.get()) {
      MY_LOGE("preDummyFrames[%zu] has existed(addr:%p)", i,
              preDummyFrame.get());
      // main1 subFrame
      updateRequestResultParams(&preDummyFrame, pOutMetaApp_Additional,
                                pOutMetaHal_Additional, mainCamP1Dma,
                                SENSOR_INDEX_MAIN, featureCombination);
      // main2 subFrame
      updateRequestResultParams(
          &preDummyFrame,
          nullptr,  // sub sensor no need to set app metadata
          pOutMetaHal_Additional, sub1CamP1Dma, SENSOR_INDEX_SUB1,
          featureCombination);
    } else {
      MY_LOGE("preDummyFrames[%zu] is invalid", i);
    }
  }
  // update postDummyFrames
  MY_LOGI("update postDummyFrames size(%zu)", out->postDummyFrames.size());
  for (size_t i = 0; i < out->postDummyFrames.size(); i++) {
    auto postDummyFrame = out->postDummyFrames[i];
    if (postDummyFrame.get()) {
      MY_LOGI("postDummyFrames[%zu] has existed(addr:%p)", i,
              postDummyFrame.get());
      // main1 subFrame
      updateRequestResultParams(&postDummyFrame, pOutMetaApp_Additional,
                                pOutMetaHal_Additional, mainCamP1Dma,
                                SENSOR_INDEX_MAIN, featureCombination);
      // main2 subFrame
      updateRequestResultParams(
          &postDummyFrame,
          nullptr,  // sub sensor no need to set app metadata
          pOutMetaHal_Additional, sub1CamP1Dma, SENSOR_INDEX_SUB1,
          featureCombination);
    } else {
      MY_LOGE("postDummyFrames[%zu] is invalid", i);
    }
  }
  return true;
};

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    updateVhdrDummyFrames(RequestOutputParams* out,
                          RequestInputParams const* in) -> bool {
  if (mVhdrInfo.bIsDoCapture == MTRUE && (mVhdrInfo.DummyCount >= 1)) {
    for (MINT32 i = 0; i < mVhdrInfo.DummyCount; i++) {
      uint32_t camP1Dma = 0;
      uint32_t sensorIndex = SENSOR_INDEX_MAIN;
      if (!getCaptureP1DmaConfig(&camP1Dma, in, SENSOR_INDEX_MAIN)) {
        MY_LOGE("[vhdrDummyFrames] main P1Dma output is invalid: 0x%X",
                camP1Dma);
        return MFALSE;
      }

      NSCam::NSPipelinePlugin::MetadataPtr pAppDummy_Additional =
          std::make_shared<IMetadata>();
      NSCam::NSPipelinePlugin::MetadataPtr pHalDummy_Additional =
          std::make_shared<IMetadata>();

      //
      std::shared_ptr<RequestResultParams> preDummyFrame = nullptr;
      updateRequestResultParams(&preDummyFrame, pAppDummy_Additional,
                                pHalDummy_Additional, camP1Dma, sensorIndex);
      //
      out->preDummyFrames.push_back(preDummyFrame);
    }
    mVhdrInfo.bIsDoCapture = MFALSE;

    MY_LOGD(
        "[vhdrDummyFrames] stream request frames count(dummycount(%d) "
        "mainFrame:%d, subFrames:%zu, preDummyFrames:%zu, postDummyFrames:%zu)",
        mVhdrInfo.DummyCount, (out->mainFrame.get() != nullptr),
        out->subFrames.size(), out->preDummyFrames.size(),
        out->postDummyFrames.size());
  }
  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    strategySingleRawPlugin(
        MINT64
            combinedKeyFeature, /*eFeatureIndexMtk and eFeatureIndexCustomer*/
        MINT64
            featureCombination, /*eFeatureIndexMtk and eFeatureIndexCustomer*/
        MINT64* foundFeature,   /*eFeatureIndexMtk and eFeatureIndexCustomer*/
        RequestOutputParams* out,
        ParsedStrategyInfo const& parsedInfo,
        RequestInputParams const* in) -> bool {
  if (mRawPluginWrapperPtr->tryGetKeyFeature(combinedKeyFeature,
                                             foundFeature)) {
    // for RawPlugin key feature (ex: SW 4Cell) negotiate and query feature
    // requirement
    uint32_t mainCamP1Dma = 0;
    if (!getCaptureP1DmaConfig(&mainCamP1Dma, in, SENSOR_INDEX_MAIN)) {
      MY_LOGE("main P1Dma output is invalid: 0x%X", mainCamP1Dma);
      return false;
    }
    auto pAppMetaControl = in->pRequest_AppControl;
    auto provider = mRawPluginWrapperPtr->getProvider(*foundFeature);
    auto property = provider->property();
    auto pSelection = mRawPluginWrapperPtr->createSelection();
    Raw_Selection& sel = *pSelection;
    mRawPluginWrapperPtr->offer(&sel);
    // update app metadata for plugin reference
    NSCam::NSPipelinePlugin::MetadataPtr pInMetaApp =
        std::make_shared<IMetadata>(*pAppMetaControl);
    sel.mIMetadataApp.setControl(pInMetaApp);
    // update previous Hal ouput for plugin reference
    if (out->mainFrame.get()) {
      auto pHalMeta = out->mainFrame->vAdditionalHal[0];
      if (pHalMeta) {
        NSCam::NSPipelinePlugin::MetadataPtr pInMetaHal =
            std::make_shared<IMetadata>(*pHalMeta);
        sel.mIMetadataHal.setControl(pInMetaHal);
      }
    }
    // query strategyInfo  for plugin provider strategy
    if (!querySelectionStrategyInfo(&(sel.mIStrategyInfo), SENSOR_INDEX_MAIN,
                                    parsedInfo, out, in)) {
      MY_LOGE("cannot query strategyInfo for plugin provider negotiate!");
      return false;
    }
    if (provider->negotiate(&sel) == OK) {
      if (!updateRequestInfo(out, SENSOR_INDEX_MAIN, sel.mORequestInfo, in)) {
        MY_LOGW("update config info failed!");
        return false;
      }
      mRawPluginWrapperPtr->keepSelection(in->requestNo, provider, pSelection);
      NSCam::NSPipelinePlugin::MetadataPtr pOutMetaApp_Additional =
          sel.mIMetadataApp.getAddtional();
      NSCam::NSPipelinePlugin::MetadataPtr pOutMetaHal_Additional =
          sel.mIMetadataHal.getAddtional();
      updateRequestResultParams(&out->mainFrame, pOutMetaApp_Additional,
                                pOutMetaHal_Additional, mainCamP1Dma,
                                SENSOR_INDEX_MAIN, featureCombination);
      //
      MY_LOGD("%s(%s), trigger provider for foundFeature(%#" PRIx64 ")",
              mRawPluginWrapperPtr->getName().c_str(), property.mName,
              *foundFeature);
    } else {
      MY_LOGD("%s(%s), no need to trigger provider for foundFeature(%#" PRIx64
              ")",
              mRawPluginWrapperPtr->getName().c_str(), property.mName,
              *foundFeature);
      return false;
    }
  } else {
    MY_LOGD("no provider for single raw key feature(%#" PRIx64 ")",
            combinedKeyFeature);
  }

  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    strategySingleYuvPlugin(
        MINT64
            combinedKeyFeature, /*eFeatureIndexMtk and eFeatureIndexCustomer*/
        MINT64
            featureCombination, /*eFeatureIndexMtk and eFeatureIndexCustomer*/
        MINT64* foundFeature,   /*eFeatureIndexMtk and eFeatureIndexCustomer*/
        RequestOutputParams* out,
        ParsedStrategyInfo const& parsedInfo,
        RequestInputParams const* in) -> bool {
  if (mYuvPluginWrapperPtr->tryGetKeyFeature(combinedKeyFeature,
                                             foundFeature)) {
    uint32_t mainCamP1Dma = 0;
    if (!getCaptureP1DmaConfig(&mainCamP1Dma, in, SENSOR_INDEX_MAIN)) {
      MY_LOGE("main P1Dma output is invalid: 0x%X", mainCamP1Dma);
      return false;
    }
    auto pAppMetaControl = in->pRequest_AppControl;
    auto provider = mYuvPluginWrapperPtr->getProvider(*foundFeature);
    auto property = provider->property();
    auto pSelection = mYuvPluginWrapperPtr->createSelection();
    Yuv_Selection& sel = *pSelection;
    mYuvPluginWrapperPtr->offer(&sel);
    // update App Metadata ouput for plugin reference
    NSCam::NSPipelinePlugin::MetadataPtr pInMetaApp =
        std::make_shared<IMetadata>(*pAppMetaControl);
    sel.mIMetadataApp.setControl(pInMetaApp);
    // update previous Hal ouput for plugin reference
    if (out->mainFrame.get()) {
      auto pHalMeta = out->mainFrame->vAdditionalHal[0];
      if (pHalMeta) {
        NSCam::NSPipelinePlugin::MetadataPtr pInMetaHal =
            std::make_shared<IMetadata>(*pHalMeta);
        sel.mIMetadataHal.setControl(pInMetaHal);
      }
    }
    // query strategyInfo  for plugin provider strategy
    if (!querySelectionStrategyInfo(&(sel.mIStrategyInfo), SENSOR_INDEX_MAIN,
                                    parsedInfo, out, in)) {
      MY_LOGE("cannot query strategyInfo for plugin provider negotiate!");
      return false;
    }
    if (provider->negotiate(&sel) == OK) {
      if (!updateRequestInfo(out, SENSOR_INDEX_MAIN, sel.mORequestInfo, in)) {
        MY_LOGW("update config info failed!");
        return false;
      }
      mYuvPluginWrapperPtr->keepSelection(in->requestNo, provider, pSelection);
      NSCam::NSPipelinePlugin::MetadataPtr pOutMetaApp_Additional =
          sel.mIMetadataApp.getAddtional();
      NSCam::NSPipelinePlugin::MetadataPtr pOutMetaHal_Additional =
          sel.mIMetadataHal.getAddtional();
      updateRequestResultParams(&out->mainFrame, pOutMetaApp_Additional,
                                pOutMetaHal_Additional, mainCamP1Dma,
                                SENSOR_INDEX_MAIN, featureCombination);
      //
      MY_LOGD("%s(%s), trigger provider for foundFeature(%#" PRIx64 ")",
              mYuvPluginWrapperPtr->getName().c_str(), property.mName,
              *foundFeature);
    } else {
      MY_LOGD("%s(%s), no need to trigger provider for foundFeature(%#" PRIx64
              ")",
              mYuvPluginWrapperPtr->getName().c_str(), property.mName,
              *foundFeature);
      return false;
    }
  } else {
    MY_LOGD("no provider for single yuv key feature(%#" PRIx64 ")",
            combinedKeyFeature);
  }

  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    strategyNormalSingleCapture(
        MINT64
            combinedKeyFeature, /*eFeatureIndexMtk and eFeatureIndexCustomer*/
        MINT64
            featureCombination, /*eFeatureIndexMtk and eFeatureIndexCustomer*/
        RequestOutputParams* out,
        ParsedStrategyInfo const& parsedInfo,
        RequestInputParams const* in) -> bool {
  // general single frame capture's sub feature combination and requirement
  uint32_t mainCamP1Dma = 0;
  if (!getCaptureP1DmaConfig(&mainCamP1Dma, in, SENSOR_INDEX_MAIN)) {
    MY_LOGE("main P1Dma output is invalid: 0x%X", mainCamP1Dma);
    return false;
  }
  // zsl policy for general single capture
  if (!parsedInfo.isFlashOn && parsedInfo.isZslModeOn &&
      parsedInfo.isZslFlowOn) {
    out->needZslFlow = true;
    if (parsedInfo.isCShot) {
      out->zslPolicyParams.mPolicy = eZslPolicy_None;
    } else {
      out->zslPolicyParams.mPolicy =
          eZslPolicy_AfState | eZslPolicy_ZeroShutterDelay;
    }
    out->zslPolicyParams.mTimeouts = 1000;  // ms
  } else {
    MY_LOGD(
        "not support Zsl due to (isFlashOn:%d, isZslModeOn:%d, isZslFlowOn:%d)",
        parsedInfo.isFlashOn, parsedInfo.isZslModeOn, parsedInfo.isZslFlowOn);
  }

  // update request result (frames metadata)
  updateRequestResultParams(
      &out->mainFrame, nullptr, /* no additional metadata from provider*/
      nullptr,                  /* no additional metadata from provider*/
      mainCamP1Dma, SENSOR_INDEX_MAIN, featureCombination);

  MY_LOGI("trigger single frame feature:%#" PRIx64
          ", feature combination:%#" PRIx64 "",
          combinedKeyFeature, featureCombination);
  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    dumpRequestOutputParams(RequestOutputParams* out, bool forcedEnable = false)
        -> bool {
  // TODO(MTK): refactoring for following code
  if (CC_UNLIKELY(mbDebug || forcedEnable)) {
    // dump sensor mode
    for (unsigned int i = 0; i < out->sensorModes.size(); i++) {
      MY_LOGD("sensor(index:%d): sensorMode(%d)", i, out->sensorModes[i]);
    }

    MY_LOGD("needZslFlow:%d, boostScenario:%u, featureFlag:%d",
            out->needZslFlow, out->boostScenario, out->featureFlag);

    MY_LOGD("ZslPolicyParams, mPolicy:0x%X, mTimestamp:%" PRId64
            ", mTimeouts:%" PRId64 "",
            out->zslPolicyParams.mPolicy, out->zslPolicyParams.mTimestamp,
            out->zslPolicyParams.mTimeouts);

    // dump frames count
    MY_LOGD(
        "capture request frames count(mainFrame:%d, subFrames:%zu, "
        "preDummyFrames:%zu, postDummyFrames:%zu)",
        (out->mainFrame.get() != nullptr), out->subFrames.size(),
        out->preDummyFrames.size(), out->postDummyFrames.size());

    // dump MTK_FEATURE_CAPTURE info
    MINT64 featureCombination = 0;
    if (out->mainFrame &&
        IMetadata::getEntry<MINT64>(out->mainFrame->vAdditionalHal[0].get(),
                                    MTK_FEATURE_CAPTURE, &featureCombination)) {
      MY_LOGD("mainFrame featureCombination=%#" PRIx64 "", featureCombination);
    } else {
      MY_LOGW("mainFrame w/o featureCombination");
    }

    if (out->mainFrame.get()) {
      for (size_t index = 0; index < out->mainFrame->needP1Dma.size();
           index++) {
        MY_LOGD("needP1Dma, index:%zu, value:%d", index,
                out->mainFrame->needP1Dma[index]);
      }
      for (size_t index = 0; index < out->mainFrame->vAdditionalHal.size();
           index++) {
        MY_LOGD("dump addition hal metadata for index:%zu, count:%u", index,
                out->mainFrame->vAdditionalHal[index]->count());
        out->mainFrame->vAdditionalHal[index]->dump();
      }
      MY_LOGD("dump addition app metadata");
      out->mainFrame->additionalApp->dump();
    } else {
      MY_LOGE("failed to get main fram");
    }

    featureCombination = 0;
    for (size_t i = 0; i < out->subFrames.size(); i++) {
      auto subFrame = out->subFrames[i];
      if (subFrame && IMetadata::getEntry<MINT64>(
                          subFrame->vAdditionalHal[0].get(),
                          MTK_FEATURE_CAPTURE, &featureCombination)) {
        MY_LOGD("subFrame[%zu] featureCombination=%#" PRIx64 "", i,
                featureCombination);
      } else {
        MY_LOGW("subFrame[%zu] w/o featureCombination=%#" PRIx64 "", i,
                featureCombination);
      }
    }

    // reconfig & zsl info.
    MY_LOGD(
        "needReconfiguration:%d, featureFlag:%d, boostScenario:%d, "
        "zsl(need:%d, policy:0x%X, timestamp:%" PRId64 ", timeouts:%" PRId64
        ")",
        out->needReconfiguration, out->featureFlag, out->boostScenario,
        out->needZslFlow, out->zslPolicyParams.mPolicy,
        out->zslPolicyParams.mTimestamp, out->zslPolicyParams.mTimeouts);
  }
  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    updatePluginSelection(bool isFeatureTrigger) -> bool {
  if (isFeatureTrigger) {
    mRawPluginWrapperPtr->pushSelection();
    mYuvPluginWrapperPtr->pushSelection();
  } else {
    mRawPluginWrapperPtr->cancel();
    mYuvPluginWrapperPtr->cancel();
  }

  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    strategyCaptureFeature(MINT64 combinedKeyFeature,
                           /*eFeatureIndexMtk and eFeatureIndexCustomer*/
                           MINT64 featureCombination,
                           /*eFeatureIndexMtk and eFeatureIndexCustomer*/
                           RequestOutputParams* out,
                           ParsedStrategyInfo const& parsedInfo,
                           RequestInputParams const* in) -> bool {
  MY_LOGD("strategy for combined key feature(%#" PRIx64
          "), feature combination(%#" PRIx64 ")",
          combinedKeyFeature, featureCombination);

  if (CC_UNLIKELY(mForcedKeyFeatures >= 0)) {
    combinedKeyFeature = mForcedKeyFeatures;
    MY_LOGW("forced key feature(%#" PRIx64 ")", combinedKeyFeature);
  }
  if (CC_UNLIKELY(mForcedFeatureCombination >= 0)) {
    featureCombination = mForcedFeatureCombination;
    MY_LOGW("forced feature combination(%#" PRIx64 ")", featureCombination);
  }

  RequestOutputParams temp_out;
  if (out->mainFrame.get()) {
    MY_LOGI("clear previous invalid frames setting");
    out->mainFrame = nullptr;
    out->subFrames.clear();
    out->preDummyFrames.clear();
    out->postDummyFrames.clear();
  }
  temp_out = *out;
  //
  MINT64 foundFeature = 0;
  if (combinedKeyFeature) { /* not MTK_FEATURE_NORMAL */
    MINT64 checkFeatures = combinedKeyFeature;
    if (!strategySingleRawPlugin(combinedKeyFeature, featureCombination,
                                 &foundFeature, &temp_out, parsedInfo, in)) {
      MY_LOGD("no need to trigger feature(%#" PRIx64
              ") for features(key:%#" PRIx64 ", combined:%#" PRIx64 ")",
              foundFeature, combinedKeyFeature, featureCombination);
      return false;
    }
    checkFeatures &= ~foundFeature;
    //
    if (!strategySingleYuvPlugin(combinedKeyFeature, featureCombination,
                                 &foundFeature, &temp_out, parsedInfo, in)) {
      MY_LOGD("no need to trigger feature(%#" PRIx64
              ") for features(key:%#" PRIx64 ", combined:%#" PRIx64 ")",
              foundFeature, combinedKeyFeature, featureCombination);
      return false;
    }
    checkFeatures &= ~foundFeature;
    //
    if (checkFeatures) {
      MY_LOGD("some key features(%#" PRIx64
              ") still not found for features(%#" PRIx64 ")",
              checkFeatures, combinedKeyFeature);
      return false;
    }
  } else {
    MY_LOGD("no combinated key feature, use default normal single capture");
    if (!strategyNormalSingleCapture(combinedKeyFeature, featureCombination,
                                     &temp_out, parsedInfo, in)) {
      return false;
    }
  }
  //
  if (parsedInfo.isCShot) {
    MY_LOGD("no need dummy frames for better capture performance, isCShot(%d)",
            parsedInfo.isCShot);
  } else {  // check and update dummy frames requirement for perfect 3A
            // stable...
    updateCaptureDummyFrames(combinedKeyFeature, &temp_out, parsedInfo, in);
  }
  // update result
  *out = temp_out;

  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    updateCaptureDummyFrames(
        MINT64
            combinedKeyFeature, /*eFeatureIndexMtk and eFeatureIndexCustomer*/
        RequestOutputParams* out,
        const ParsedStrategyInfo& parsedInfo,
        RequestInputParams const* in) -> void {
  int8_t preDummyCount = 0;
  int8_t postDummyCount = 0;

  if (out->preDummyFrames.size() || out->postDummyFrames.size()) {
    MY_LOGI("feature(%#" PRIx64 ") has choose dummy frames(pre:%zu, post:%zu)",
            combinedKeyFeature, out->preDummyFrames.size(),
            out->postDummyFrames.size());
    return;
  }

  // lambda for choose maximum count
  auto updateDummyCount = [&preDummyCount, &postDummyCount](
                              int8_t preCount, int8_t postCount) -> void {
    preDummyCount = std::max(preDummyCount, preCount);
    postDummyCount = std::max(postDummyCount, postCount);
  };

  // lambda to check manual 3A
  auto isManual3aSetting = [](IMetadata const* pAppMeta,
                              IMetadata const* pHalMeta) -> bool {
    if (pAppMeta && pHalMeta) {
      // check manual AE (method.1)
      MUINT8 aeMode = MTK_CONTROL_AE_MODE_ON;
      if (IMetadata::getEntry<MUINT8>(pAppMeta, MTK_CONTROL_AE_MODE, &aeMode)) {
        if (aeMode == MTK_CONTROL_AE_MODE_OFF) {
          MY_LOGD("get MTK_CONTROL_AE_MODE(%d), it is manual AE", aeMode);
          return true;
        }
      }
      // check manual AE (method.2)
      IMetadata::Memory capParams;
      capParams.resize(sizeof(NS3Av3::CaptureParam_T));
      if (IMetadata::getEntry<IMetadata::Memory>(pHalMeta, MTK_3A_AE_CAP_PARAM,
                                                 &capParams)) {
        MY_LOGD("get MTK_3A_AE_CAP_PARAM, it is manual AE");
        return true;
      }
      // check manual AW
      MUINT8 awLock = MFALSE;
      IMetadata::getEntry<MUINT8>(pAppMeta, MTK_CONTROL_AWB_LOCK, &awLock);
      if (awLock) {
        MY_LOGD("get MTK_CONTROL_AWB_LOCK(%d), it is manual AE", awLock);
        return true;
      }
    } else {
      MY_LOGW("no metadata(app:%p, hal:%p) to query hint", pAppMeta, pHalMeta);
    }

    return false;
  };
  //
  bool bIsManual3A = false;
  if (CC_LIKELY(out->mainFrame.get())) {
    IMetadata const* pAppMeta = out->mainFrame->additionalApp.get();
    IMetadata const* pHalMeta =
        out->mainFrame->vAdditionalHal[SENSOR_INDEX_MAIN].get();
    bIsManual3A = isManual3aSetting(pAppMeta, pHalMeta);
  } else {
    MY_LOGD("no metadata info due to no mainFrame");
  }
  //
  if (bIsManual3A) {
    // get manual 3a delay frames count from 3a hal
    MUINT32 delayedFrames = 0;
    std::shared_ptr<NS3Av3::IHal3A> hal3a;
    MAKE_Hal3A(
        hal3a, [](NS3Av3::IHal3A* p) { p->destroyInstance(LOG_TAG); },
        mPolicyParams.pPipelineStaticInfo->sensorIds[SENSOR_INDEX_MAIN],
        LOG_TAG);

    hal3a->send3ACtrl(NS3Av3::E3ACtrl_GetCaptureDelayFrame,
                      reinterpret_cast<MINTPTR>(&delayedFrames), 0);
    MY_LOGD("delayedFrames count:%d due to manual 3A", delayedFrames);
    //
    updateDummyCount(0, delayedFrames);
  }

  MY_LOGD("dummy frames result(pre:%d, post:%d)", preDummyCount,
          postDummyCount);

  uint32_t camP1Dma = 0;
  uint32_t sensorIndex = SENSOR_INDEX_MAIN;
  if (!getCaptureP1DmaConfig(&camP1Dma, in, SENSOR_INDEX_MAIN)) {
    MY_LOGE("main P1Dma output is invalid: 0x%X", camP1Dma);
    return;
  }

  // update preDummyFrames
  for (MINT32 i = 0; i < preDummyCount; i++) {
    NSCam::NSPipelinePlugin::MetadataPtr pAppDummy_Additional =
        std::make_shared<IMetadata>();
    NSCam::NSPipelinePlugin::MetadataPtr pHalDummy_Additional =
        std::make_shared<IMetadata>();
    // update info for pre-dummy frames for flash stable
    IMetadata::setEntry<MUINT8>(pAppDummy_Additional.get(), MTK_CONTROL_AE_MODE,
                                MTK_CONTROL_AE_MODE_OFF);
    IMetadata::setEntry<MINT64>(pAppDummy_Additional.get(),
                                MTK_SENSOR_EXPOSURE_TIME, 33333333);
    IMetadata::setEntry<MINT32>(pAppDummy_Additional.get(),
                                MTK_SENSOR_SENSITIVITY, 1000);
    //
    std::shared_ptr<RequestResultParams> preDummyFrame = nullptr;
    updateRequestResultParams(&preDummyFrame, pAppDummy_Additional,
                              pHalDummy_Additional, camP1Dma, sensorIndex);
    //
    out->preDummyFrames.push_back(preDummyFrame);
  }

  // update postDummyFrames
  for (MINT32 i = 0; i < postDummyCount; i++) {
    NSCam::NSPipelinePlugin::MetadataPtr pAppDummy_Additional =
        std::make_shared<IMetadata>();
    NSCam::NSPipelinePlugin::MetadataPtr pHalDummy_Additional =
        std::make_shared<IMetadata>();
    // update info for post-dummy(delay) frames to restore 3A for preview stable
    IMetadata::setEntry<MBOOL>(pHalDummy_Additional.get(),
                               MTK_3A_AE_RESTORE_CAPPARA, 1);
    //
    std::shared_ptr<RequestResultParams> postDummyFrame = nullptr;
    updateRequestResultParams(&postDummyFrame, pAppDummy_Additional,
                              pHalDummy_Additional, camP1Dma, sensorIndex);
    //
    out->postDummyFrames.push_back(postDummyFrame);
  }

  // check result
  if (out->preDummyFrames.size() || out->postDummyFrames.size()) {
    MY_LOGI("feature(%#" PRIx64
            ") append dummy frames(pre:%zu, post:%zu) due to isFlashOn(%d), "
            "isManual3A(%d)",
            combinedKeyFeature, out->preDummyFrames.size(),
            out->postDummyFrames.size(), parsedInfo.isFlashOn, bIsManual3A);

    if (out->needZslFlow) {
      MY_LOGW("not support Zsl buffer due to isFlashOn(%d) or isManual3A(%d)",
              parsedInfo.isFlashOn, bIsManual3A);
      out->needZslFlow = false;
    }
  }

  return;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    evaluateCaptureSetting(RequestOutputParams* out,
                           ParsedStrategyInfo const& parsedInfo,
                           RequestInputParams const* in) -> bool {
  MY_LOGD("capture req#:%u", in->requestNo);

  NSCam::v3::pipeline::policy::scenariomgr::ScenarioFeatures scenarioFeatures;
  NSCam::v3::pipeline::policy::scenariomgr::ScenarioHint scenarioHint;
  if (parsedInfo.isCShot) {
    out->boostScenario =
        NSCam::v3::pipeline::model::IScenarioControlV3::Scenario_ContinuousShot;
  }
  // TODO(MTK):
  // scenarioHint.captureScenarioIndex = ? /* hint from vendor tag */
  int32_t openId = mPolicyParams.pPipelineStaticInfo->openId;
  auto pAppMetadata = in->pRequest_AppControl;

  int32_t scenario = -1;
  if (!get_capture_scenario(&scenario, scenarioHint, pAppMetadata)) {
    MY_LOGE("cannot get capture scenario");
    return false;
  }
  if (!get_features_table_by_scenario(openId, scenario, &scenarioFeatures)) {
    MY_LOGE("cannot query scenarioFeatures for (openId:%d, scenario:%d)",
            openId, scenario);
    return false;
  } else {
    MY_LOGD("find scenario:%s for (openId:%d, scenario:%d)",
            scenarioFeatures.scenarioName.c_str(), openId, scenario);
  }

  bool isFeatureTrigger = false;
  for (auto featureSet : scenarioFeatures.vFeatureSet) {
    // evaluate key feature plugin and feature combination for feature strategy
    // policy.
    if (strategyCaptureFeature(featureSet.feature,
                               featureSet.featureCombination, out, parsedInfo,
                               in)) {
      isFeatureTrigger = true;
      MY_LOGI("trigger feature:%s(%#" PRIx64
              "), feature combination:%s(%#" PRIx64 ")",
              featureSet.featureName.c_str(),
              static_cast<MINT64>(featureSet.feature),
              featureSet.featureCombinationName.c_str(),
              static_cast<MINT64>(featureSet.featureCombination));
      updatePluginSelection(isFeatureTrigger);
      break;
    } else {
      isFeatureTrigger = false;
      MY_LOGD("no need to trigger feature:%s(%#" PRIx64
              "), feature combination:%s(%#" PRIx64 ")",
              featureSet.featureName.c_str(),
              static_cast<MINT64>(featureSet.feature),
              featureSet.featureCombinationName.c_str(),
              static_cast<MINT64>(featureSet.featureCombination));
      updatePluginSelection(isFeatureTrigger);
    }
  }
  dumpRequestOutputParams(out, true);

  if (!isFeatureTrigger) {
    MY_LOGE("no feature can be triggered!");
    return false;
  }

  MY_LOGD(
      "capture request frames count(mainFrame:%d, subFrames:%zu, "
      "preDummyFrames:%zu, postDummyFrames:%zu)",
      (out->mainFrame.get() != nullptr), out->subFrames.size(),
      out->preDummyFrames.size(), out->postDummyFrames.size());
  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    isNeedIsoReconfig(HDRMode* apphdrMode, uint32_t recodingMode) -> bool {
  {
    static std::mutex _locker;

    if (recodingMode == MTK_FEATUREPIPE_VIDEO_RECORD) {
      if (mVhdrInfo.IsoSwitchModeStatus == eSwitchMode_LowLightLvMode) {
        *apphdrMode = HDRMode::OFF;
      }

      MY_LOGD(
          "Has Recording and  no need iso reconfig "
          "recodingMode(%d)IsoSwitchModeStatus(%d) apphdrMode(%x)",
          recodingMode, mVhdrInfo.IsoSwitchModeStatus,
          (unsigned int)*apphdrMode);

      return true;
    }

    std::shared_ptr<NS3Av3::IHal3A> hal3a;
    MAKE_Hal3A(
        hal3a, [](NS3Av3::IHal3A* p) { p->destroyInstance(LOG_TAG); },
        mPolicyParams.pPipelineStaticInfo->sensorIds[SENSOR_INDEX_MAIN],
        LOG_TAG);
    if (hal3a.get()) {
      std::lock_guard<std::mutex> _l(_locker);
      int iIsoThresholdStable1 = -1;  // for low iso (ex: 2800)
      int iIsoThresholdStable2 = -1;  // for high iso (ex: 5600)
      hal3a->send3ACtrl(NS3Av3::E3ACtrl_GetISOThresStatus,
                        (MINTPTR)&iIsoThresholdStable1,
                        (MINTPTR)&iIsoThresholdStable2);

      MY_LOGD_IF(mVhdrInfo.bVhdrDebugMode,
                 "Iso-reconfig status: apphdrMode(%hhu) isothreshold(%d,%d) "
                 "recording(%d)",
                 *apphdrMode, iIsoThresholdStable1, iIsoThresholdStable2,
                 recodingMode);

      if (mVhdrInfo.IsoSwitchModeStatus == eSwitchMode_HighLightMode) {
        // when 3HDR feature on (from AP setting)
        // original enviroment : high light (low iso) (binning mode)
        // new enviroment : low light (high iso) and stable
        // need to change to binning mode (from 3hdr mode)
        // if iIsoThresholdStable2 == 0: now iso bigger than ISO 5600 & stable
        if (iIsoThresholdStable2 == 0) {  // orginal:3hdr mode, so need to check
                                          // iIsoThresholdStable2 for ISO5600
          mVhdrInfo.IsoSwitchModeStatus = eSwitchMode_LowLightLvMode;
          *apphdrMode = HDRMode::OFF;
          MY_LOGD(
              "need Iso-reconfig: IsoSwitchModeStatus_h->l (%d) "
              "isothreshold(%d,%d) apphdrMode(%hhu)",
              mVhdrInfo.IsoSwitchModeStatus, iIsoThresholdStable1,
              iIsoThresholdStable2, *apphdrMode);
        }
      } else if (mVhdrInfo.IsoSwitchModeStatus == eSwitchMode_LowLightLvMode) {
        // when 3HDR feature on (from AP setting)
        // original enviroment : low light (high iso) (binning mode)
        // new enviroment : high light (low iso) and stable
        // need to change to 3hdr mode (from binning mode)
        // if iIsoThresholdStable1 == 1: now iso smaller than ISO 2800 & stable
        *apphdrMode = HDRMode::OFF;
        if (iIsoThresholdStable1 ==
            1) {  // orginal:binning mode, so need to check iIsoThresholdStable1
                  // for ISO2800 (1:smaller than 2800)
          mVhdrInfo.IsoSwitchModeStatus = eSwitchMode_HighLightMode;
          *apphdrMode = HDRMode::ON;
          MY_LOGD(
              "need Iso-reconfig: IsoSwitchModeStatus_l->h (%d) "
              "isothreshold(%d,%d) apphdrMode(%hhu)",
              mVhdrInfo.IsoSwitchModeStatus, iIsoThresholdStable1,
              iIsoThresholdStable1, *apphdrMode);
        }
      } else {
        mVhdrInfo.IsoSwitchModeStatus = eSwitchMode_HighLightMode;
        MY_LOGD(
            "not need Iso-reconfig: IsoSwitchModeStatus_Undefined (%d) "
            "isothreshold(%d,%d) apphdrMode(%hhu)",
            mVhdrInfo.IsoSwitchModeStatus, iIsoThresholdStable1,
            iIsoThresholdStable1, *apphdrMode);
      }
      return true;
    } else {
      MY_LOGE(
          "create IHal3A instance failed! cannot get current real iso for "
          "strategy");
      mVhdrInfo.IsoSwitchModeStatus = eSwitchMode_HighLightMode;
      return false;
    }
  }
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    updateStreamData(RequestOutputParams* out,
                     ParsedStrategyInfo const& parsedInfo,
                     RequestInputParams const* in) -> bool {
  // 1. Update stream state : decide App Mode
  int32_t recordState = -1;
  uint32_t AppMode = 0;
  bool isRepeating = in->pRequest_ParsedAppMetaControl->repeating;
  IMetadata const* pAppMetaControl = in->pRequest_AppControl;
  if (IMetadata::getEntry<MINT32>(
          pAppMetaControl, MTK_STREAMING_FEATURE_RECORD_STATE, &recordState)) {
    // App has set recordState Tag
    if (recordState == MTK_STREAMING_FEATURE_RECORD_STATE_PREVIEW) {
      if (in->pRequest_AppImageStreamInfo->hasVideoConsumer) {
        AppMode = MTK_FEATUREPIPE_VIDEO_STOP;
      } else {
        AppMode = MTK_FEATUREPIPE_VIDEO_PREVIEW;
      }
    } else {
      AppMode = mConfigOutputParams.StreamingParams.mLastAppInfo.appMode;
      MY_LOGW(
          "Unknown or Not Supported app recordState(%d), use last appMode=%d",
          recordState,
          mConfigOutputParams.StreamingParams.mLastAppInfo.appMode);
    }
  } else {
    // App has NOT set recordState Tag
    // (slow motion has no repeating request)
    // no need process slow motion because use different policy?
    if (isRepeating /*|| isHighspped*/) {
      if (in->pRequest_AppImageStreamInfo->hasVideoConsumer) {
        AppMode = MTK_FEATUREPIPE_VIDEO_RECORD;
      } else if (in->Configuration_HasRecording) {
        AppMode = MTK_FEATUREPIPE_VIDEO_PREVIEW;
      } else {
        AppMode = MTK_FEATUREPIPE_PHOTO_PREVIEW;
      }
    } else {
      AppMode = mConfigOutputParams.StreamingParams.mLastAppInfo.appMode;
    }
  }

  NSCam::NSPipelinePlugin::MetadataPtr pOutMetaHal =
      std::make_shared<IMetadata>();
  IMetadata::setEntry<MINT32>(pOutMetaHal.get(), MTK_FEATUREPIPE_APP_MODE,
                              AppMode);
  uint32_t needP1Dma = 0;
  if ((*(in->pConfiguration_StreamInfo_P1))[0].pHalImage_P1_Rrzo != nullptr) {
    needP1Dma |= P1_RRZO;
  }
#if 0  // TO DO
    auto needImgo = [this](MSize imgSize, MSize rrzoSize) -> int {
        // if isZslMode=true, must output IMGO for ZSL buffer pool
        return (imgSize.w > rrzoSize.w) || (imgSize.h > rrzoSize.h) ||
          mConfigInputParams.isZslMode;
    }
#endif
  if ((*(in->pConfiguration_StreamInfo_P1))[0].pHalImage_P1_Imgo != nullptr) {
    needP1Dma |= P1_IMGO;
  }
  if ((*(in->pConfiguration_StreamInfo_P1))[0].pHalImage_P1_Lcso != nullptr) {
    needP1Dma |= P1_LCSO;
  }
  if ((*(in->pConfiguration_StreamInfo_P1))[0].pHalImage_P1_Rsso != nullptr) {
    needP1Dma |= P1_RSSO;
  }
  // SMVR
  if (mPolicyParams.pPipelineUserConfiguration->pParsedAppConfiguration
          ->isConstrainedHighSpeedMode) {
    IMetadata::IEntry const& entry =
        pAppMetaControl->entryFor(MTK_CONTROL_AE_TARGET_FPS_RANGE);
    if (entry.isEmpty()) {
      MY_LOGW("no MTK_CONTROL_AE_TARGET_FPS_RANGE");
    } else {
      MINT32 i4MinFps = entry.itemAt(0, Type2Type<MINT32>());
      MINT32 i4MaxFps = entry.itemAt(1, Type2Type<MINT32>());
      MINT32 postDummyReqs = i4MinFps == 30 ? (i4MaxFps / i4MinFps - 1) : 0;
      MUINT8 fps = i4MinFps == 30
                       ? MTK_SMVR_FPS_30
                       : i4MinFps == 120
                             ? MTK_SMVR_FPS_120
                             : i4MinFps == 240
                                   ? MTK_SMVR_FPS_240
                                   : i4MinFps == 480
                                         ? MTK_SMVR_FPS_480
                                         : i4MinFps == 960 ? MTK_SMVR_FPS_960
                                                           : MTK_SMVR_FPS_30;

      MY_LOGD("SMVR: i4MinFps=%d, i4MaxFps=%d, postDummyReqs=%d", i4MinFps,
              i4MaxFps, postDummyReqs);

      IMetadata::setEntry<MUINT8>(pOutMetaHal.get(), MTK_HAL_REQUEST_SMVR_FPS,
                                  fps);
      if (postDummyReqs) {
        std::shared_ptr<RequestResultParams> postDummyFrame = nullptr;
        updateRequestResultParams(&postDummyFrame, nullptr, nullptr, needP1Dma,
                                  SENSOR_INDEX_MAIN);
        for (MINT32 i = 0; i < postDummyReqs; i++) {
          out->postDummyFrames.push_back(postDummyFrame);
        }
      }
    }
  }

  // vhdr set proflie
  MUINT32 vhdrMode = SENSOR_VHDR_MODE_NONE;
  HDRMode apphdrMode = HDRMode::OFF;
  MINT32 apphdrModeInt = 0;
  MINT32 forceAppHdrMode =
      mVhdrInfo.bVhdrDebugMode
          ? ::property_get_int32("vendor.debug.camera.hal3.appHdrMode",
                                 DEBUG_APP_HDR)
          : DEBUG_APP_HDR;

  if (IMetadata::getEntry<MINT32>(pAppMetaControl, MTK_HDR_FEATURE_HDR_MODE,
                                  &apphdrModeInt)) {
    mVhdrInfo.UiAppHdrMode = static_cast<HDRMode>((MUINT8)apphdrModeInt);
  } else {
    MY_LOGD("Get UiAppMeta:hdrMode Fail ");
  }

  auto isUiVhdrOn = [&](void) -> bool {
    if (mVhdrInfo.bVhdrDebugMode && forceAppHdrMode >= 0) {
      apphdrMode = static_cast<HDRMode>((MUINT8)forceAppHdrMode);
    } else {
      apphdrMode = mVhdrInfo.UiAppHdrMode;
    }

    if (apphdrMode == HDRMode::VIDEO_ON || apphdrMode == HDRMode::VIDEO_AUTO) {
      return true;
    } else {
      return false;
    }
  };

  if (isUiVhdrOn()) {
    // Iso reconfig
    isNeedIsoReconfig(&apphdrMode, AppMode);

    vhdrMode = mVhdrInfo.cfgVhdrMode;

    // after doing capture, vhdr need to add dummy frame
    updateVhdrDummyFrames(out, in);
  }

  mVhdrInfo.lastAppHdrMode = mVhdrInfo.curAppHdrMode;
  mVhdrInfo.curAppHdrMode = apphdrMode;
  MY_LOGD_IF(
      mVhdrInfo.bVhdrDebugMode,
      "updateStreamData vhdrMode:%d, lastAppHdrMode:%hhu, curAppHdrMode:%hhu, "
      "UiAppHdrMode:%hhu IsoSwitchModeStatus:%d iso:%d, exposureTime:%d ",
      vhdrMode, mVhdrInfo.lastAppHdrMode, mVhdrInfo.curAppHdrMode,
      mVhdrInfo.UiAppHdrMode, mVhdrInfo.IsoSwitchModeStatus, parsedInfo.realIso,
      parsedInfo.exposureTime);

  // update HDR mode to 3A
  IMetadata::setEntry<MUINT8>(pOutMetaHal.get(), MTK_3A_HDR_MODE,
                              static_cast<MUINT8>(apphdrMode));

  // prepare Stream Size
#if 0  // need check
    auto rrzoSize =
      (*(in->pConfiguration_StreamInfo_P1))[0].pHalImage_P1_Rrzo->getImgSize();
    ProfileParam profileParam(
        rrzoSize,  // stream size
        vhdrMode,  // vhdrmode
        0,       // sensormode
        ProfileParam::FLAG_NONE,  // TODO(MTK): set flag by
                                  // is ZSDPureRawStreaming or not fMask
        0);
#endif
  return updateRequestResultParams(&out->mainFrame, nullptr, pOutMetaHal,
                                   needP1Dma, SENSOR_INDEX_MAIN, 0, 0, 0);
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    evaluateStreamSetting(RequestOutputParams* out,
                          ParsedStrategyInfo const& parsedInfo __unused,
                          RequestInputParams const* in __unused,
                          bool enabledP2Capture) -> bool {
  if (enabledP2Capture) {
    /**************************************************************
     * NOTE:
     * In this stage,
     * MTK_3A_ISP_PROFILE and sensor setting has been configured
     * for capture behavior.
     *************************************************************/
    // stream policy with capture policy and behavior.
    // It may occurs some quality limitation duting captue behavior.
    // For example, change sensor mode, 3A sensor setting, P1 ISP profile.
    // Capture+streaming feature combination policy
    // TODO(MTK): implement for customized streaming feature setting evaluate
    // with capture behavior
    MY_LOGI(
        "not yet implement for stream feature setting evaluate with capture "
        "behavior");
  } else {
    // TODO(MTK): only porting some streaming feature, temporary.
    // not yet implement all stream feature setting evaluate
    updateStreamData(out, parsedInfo, in);
  }
  MY_LOGD_IF(2 <= mbDebug,
             "stream request frames count(mainFrame:%d, subFrames:%zu, "
             "preDummyFrames:%zu, postDummyFrames:%zu)",
             (out->mainFrame.get() != nullptr), out->subFrames.size(),
             out->preDummyFrames.size(), out->postDummyFrames.size());
  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    evaluateReconfiguration(RequestOutputParams* out,
                            RequestInputParams const* in) -> bool {
  out->needReconfiguration = false;
  out->reconfigCategory = ReCfgCtg::NO;
  for (unsigned int i = 0; i < in->sensorModes.size(); i++) {
    if (in->sensorModes[i] != out->sensorModes[i]) {
      MY_LOGD("sensor(index:%d): sensorMode(%d --> %d) is changed", i,
              in->sensorModes[i], out->sensorModes[i]);
      out->needReconfiguration = true;
    }

    if (mVhdrInfo.curAppHdrMode == mVhdrInfo.lastAppHdrMode) {
      MY_LOGD_IF(mVhdrInfo.bVhdrDebugMode,
                 "App hdrMode no change: Last(%hhu) - Cur(%hhu)",
                 mVhdrInfo.lastAppHdrMode, mVhdrInfo.curAppHdrMode);
    } else {
      // app hdrMode change
      if (mVhdrInfo.curAppHdrMode == HDRMode::VIDEO_ON ||
          mVhdrInfo.curAppHdrMode == HDRMode::VIDEO_AUTO ||
          mVhdrInfo.lastAppHdrMode == HDRMode::VIDEO_ON ||
          mVhdrInfo.lastAppHdrMode == HDRMode::VIDEO_AUTO) {
        MY_LOGD(
            "App hdrMode change: Last(%hhu) - Cur(%hhu), need reconfig for "
            "vhdr",
            mVhdrInfo.lastAppHdrMode, mVhdrInfo.curAppHdrMode);
        out->needReconfiguration = true;
        out->reconfigCategory = ReCfgCtg::STREAMING;
      } else {
        MY_LOGD("App hdrMode change: Last(%hhu) - Cur(%hhu), no need reconfig",
                mVhdrInfo.lastAppHdrMode, mVhdrInfo.curAppHdrMode);
      }
    }
#if 1  // check_later
    MINT32 forceReconfig =
        ::property_get_int32("vendor.debug.camera.hal3.pure.reconfig.test", -1);
#else
    MINT32 forceReconfig =
        ::property_get_bool("vendor.debug.camera.hal3.pure.reconfig.test", -1);
#endif
    if (forceReconfig == 1) {
      out->needReconfiguration = true;
      out->reconfigCategory = ReCfgCtg::STREAMING;
    } else if (forceReconfig == 0) {
      out->needReconfiguration = false;
      out->reconfigCategory = ReCfgCtg::NO;
    }

    // sensor mode is not the same as preview default (cannot execute zsl)
    if (out->needReconfiguration == true ||
        mDefaultConfig.sensorMode[i] != out->sensorModes[i]) {
      out->needZslFlow = false;
      out->zslPolicyParams.mPolicy = eZslPolicy_None;
      MY_LOGD("must reconfiguration, capture new frames w/o zsl flow");
    }
  }
  // zsl policy debug
  if (out->needZslFlow) {
    MY_LOGD("needZslFlow(%d), zsl policy(0x%X), timestamp:%" PRId64
            ", timeouts:%" PRId64 "",
            out->needZslFlow, out->zslPolicyParams.mPolicy,
            out->zslPolicyParams.mTimestamp, out->zslPolicyParams.mTimeouts);
  }
  return true;
}

/******************************************************************************
 *
 ******************************************************************************/
auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    evaluateCaptureConfiguration(ConfigurationOutputParams* out __unused,
                                 ConfigurationInputParams const* in __unused)
        -> bool {
  // query features by scenario during config
  NSCam::v3::pipeline::policy::scenariomgr::ScenarioFeatures scenarioFeatures;
  NSCam::v3::pipeline::policy::scenariomgr::ScenarioHint scenarioHint;
  // TODO(MTK):
  // scenarioHint.captureScenarioIndex = ?   /* hint from vendor tag */
  // scenarioHint.streamingScenarioIndex = ? /* hint from vendor tag */
  int32_t openId = mPolicyParams.pPipelineStaticInfo->openId;
  auto pAppMetadata = in->pSessionParams;

  int32_t scenario = -1;
  if (!get_capture_scenario(&scenario, scenarioHint, pAppMetadata)) {
    MY_LOGE("cannot get capture scenario");
    return false;
  }
  if (!get_features_table_by_scenario(openId, scenario, &scenarioFeatures)) {
    MY_LOGE("cannot query scenarioFeatures for (openId:%d, scenario:%d)",
            openId, scenario);
    return false;
  } else {
    MY_LOGD("find scenario:%s for (openId:%d, scenario:%d)",
            scenarioFeatures.scenarioName.c_str(), openId, scenario);
  }

  for (auto featureSet : scenarioFeatures.vFeatureSet) {
    MY_LOGI("scenario(%s) support feature:%s(%#" PRIx64
            "), feature combination:%s(%#" PRIx64 ")",
            scenarioFeatures.scenarioName.c_str(),
            featureSet.featureName.c_str(),
            static_cast<MINT64>(featureSet.feature),
            featureSet.featureCombinationName.c_str(),
            static_cast<MINT64>(featureSet.featureCombination));
    out->CaptureParams.supportedScenarioFeatures |=
        featureSet.featureCombination;
  }
  MY_LOGD("support features:%#" PRIx64 "",
          out->CaptureParams.supportedScenarioFeatures);

  // TODO(MTK): defined the maximum Jpeg mumber
  out->CaptureParams.maxAppJpegStreamNum = 5;
  MY_LOGI("maxAppJpegStreamNum:%u, maxZslBufferNum:%u",
          out->CaptureParams.maxAppJpegStreamNum,
          out->CaptureParams.maxZslBufferNum);
  return true;
}

auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    evaluateStreamConfiguration(ConfigurationOutputParams* out,
                                ConfigurationInputParams const* in __unused)
        -> bool {
  auto const& pParsedAppConfiguration =
      mPolicyParams.pPipelineUserConfiguration->pParsedAppConfiguration;
  MINT32 force3DNR = ::property_get_int32("vendor.debug.camera.hal3.3dnr",
                                          FORCE_3DNR);  // default on
  mVhdrInfo.bVhdrDebugMode = DEBUG_VHDR;
  mVhdrInfo.DummyCount = ::property_get_int32(
      "vendor.debug.camera.hal3.dummycount", DEBUG_DUMMY_HDR);
  MINT32 forceAppHdrMode = ::property_get_int32(
      "vendor.debug.camera.hal3.appHdrMode", DEBUG_APP_HDR);
  // query features by scenario during config
  NSCam::v3::pipeline::policy::scenariomgr::ScenarioFeatures scenarioFeatures;
  NSCam::v3::pipeline::policy::scenariomgr::ScenarioHint scenarioHint;
  // TODO(MTK):
  // scenarioHint.captureScenarioIndex = ?   /* hint from vendor tag */
  // scenarioHint.streamingScenarioIndex = ? /* hint from vendor tag */
  int32_t openId = mPolicyParams.pPipelineStaticInfo->openId;
  auto pAppMetadata = in->pSessionParams;

  int32_t scenario = -1;
  if (!get_streaming_scenario(&scenario, scenarioHint, pAppMetadata)) {
    MY_LOGE("cannot get streaming scenario");
    return false;
  }
  if (!get_features_table_by_scenario(openId, scenario, &scenarioFeatures)) {
    MY_LOGE("cannot query scenarioFeatures for (openId:%d, scenario:%d)",
            openId, scenario);
    return false;
  } else {
    MY_LOGD("find scenario:%s for (openId:%d, scenario:%d)",
            scenarioFeatures.scenarioName.c_str(), openId, scenario);
  }

  for (auto featureSet : scenarioFeatures.vFeatureSet) {
    MY_LOGI("scenario(%s) support feature:%s(%#" PRIx64
            "), feature combination:%s(%#" PRIx64 ")",
            scenarioFeatures.scenarioName.c_str(),
            featureSet.featureName.c_str(),
            static_cast<MINT64>(featureSet.feature),
            featureSet.featureCombinationName.c_str(),
            static_cast<MINT64>(featureSet.featureCombination));
    out->StreamingParams.supportedScenarioFeatures |=
        featureSet.featureCombination;
  }
  MY_LOGD("support features:%#" PRIx64 "",
          out->StreamingParams.supportedScenarioFeatures);

  // VHDR
  out->StreamingParams.vhdrMode = SENSOR_VHDR_MODE_NONE;
  // getHDRMode
  HDRMode appHdrMode = HDRMode::OFF;
  MINT32 hdrModeInt = 0;
  if (mVhdrInfo.bVhdrDebugMode && forceAppHdrMode >= 0) {
    // Force set app hdrMode
    appHdrMode = static_cast<HDRMode>((MUINT8)forceAppHdrMode);
  } else if (!mVhdrInfo.bFirstConfig) {
    // After first configure, using CurAppHdrMode
    appHdrMode = mVhdrInfo.curAppHdrMode;
  } else if (IMetadata::getEntry<MINT32>(
                 &pParsedAppConfiguration->sessionParams,
                 MTK_HDR_FEATURE_SESSION_PARAM_HDR_MODE, &hdrModeInt)) {
    appHdrMode = static_cast<HDRMode>((MUINT8)hdrModeInt);
    mVhdrInfo.curAppHdrMode = appHdrMode;
    MY_LOGI("first config vhdr(%hhu)", mVhdrInfo.curAppHdrMode);
  } else {
    MY_LOGI("Get appConfig sessionParams appHdrMode fail ");
  }
  mVhdrInfo.bFirstConfig = MFALSE;

  IMetadata::IEntry test_entry =
      pParsedAppConfiguration->sessionParams.entryFor(
          MTK_HDR_FEATURE_SESSION_PARAM_HDR_MODE);

  MY_LOGD(
      "StreamConfig: bFirstConfig(%d) forceAppHdrMode(%d), "
      "curAppHdrMode(%hhu), test_entry.count(%d), appHdrMode(%hhu)",
      mVhdrInfo.bFirstConfig, forceAppHdrMode, mVhdrInfo.curAppHdrMode,
      test_entry.count(), appHdrMode);

  // getVHDRMode
  if (appHdrMode == HDRMode::VIDEO_ON || appHdrMode == HDRMode::VIDEO_AUTO) {
    std::shared_ptr<IMetadataProvider> metaProvider =
        NSMetadataProviderManager::valueFor(
            mPolicyParams.pPipelineStaticInfo->sensorIds[0]);
    if (metaProvider == nullptr) {
      MY_LOGE(
          "Can not get metadata provider for search vhdr mode!! set vhdrMode "
          "to none");
    } else {
      IMetadata staMeta = metaProvider->getMtkStaticCharacteristics();
      IMetadata::IEntry availVhdrEntry =
          staMeta.entryFor(MTK_HDR_FEATURE_AVAILABLE_VHDR_MODES);
      for (size_t i = 0; i < availVhdrEntry.count(); i++) {
        if (availVhdrEntry.itemAt(i, Type2Type<MINT32>()) !=
            SENSOR_VHDR_MODE_NONE) {
          out->StreamingParams.vhdrMode =
              (MUINT32)availVhdrEntry.itemAt(i, Type2Type<MINT32>());
          mVhdrInfo.cfgVhdrMode = out->StreamingParams.vhdrMode;
          break;
        }
      }
      if (out->StreamingParams.vhdrMode == SENSOR_VHDR_MODE_NONE) {
        MY_LOGE(
            "Can not get supported vhdr mode from "
            "MTK_HDR_FEATURE_AVAILABLE_VHDR_MODES! (maybe FO not set?), set "
            "vhdrMode to none");
      }
    }
  } else {
    out->StreamingParams.vhdrMode = SENSOR_VHDR_MODE_NONE;
    mVhdrInfo.cfgVhdrMode = SENSOR_VHDR_MODE_NONE;
    MY_LOGI(
        "Can not get supported vhdr mode from MetaProvider!! (maybe FO not "
        "set?), set vhdrMode to none");
  }
  // 3DNR
  out->StreamingParams.nr3dMode = 0;
  MINT32 e3DnrMode = MTK_NR_FEATURE_3DNR_MODE_OFF;
  MBOOL isAPSupport3DNR = MFALSE;
  if (IMetadata::getEntry<MINT32>(&pParsedAppConfiguration->sessionParams,
                                  MTK_NR_FEATURE_3DNR_MODE, &e3DnrMode) &&
      e3DnrMode == MTK_NR_FEATURE_3DNR_MODE_ON) {
    isAPSupport3DNR = MTRUE;
  }
  if (force3DNR) {
    out->StreamingParams.nr3dMode |= NSCam::NR3D::E3DNR_MODE_MASK_UI_SUPPORT;
  }
  if (::property_get_int32("vendor.debug.camera.3dnr.level", 0)) {
    out->StreamingParams.nr3dMode |=
        NSCam::NR3D::E3DNR_MODE_MASK_HAL_FORCE_SUPPORT;
  }
  if (E3DNR_MODE_MASK_ENABLED(
          out->StreamingParams.nr3dMode,
          (NSCam::NR3D::E3DNR_MODE_MASK_UI_SUPPORT |
           NSCam::NR3D::E3DNR_MODE_MASK_HAL_FORCE_SUPPORT))) {
    if (::property_get_int32("vendor.debug.3dnr.sl2e.enable",
                             1)) {  // sl2e: default on, need use metadata?
      out->StreamingParams.nr3dMode |= NSCam::NR3D::E3DNR_MODE_MASK_SL2E_EN;
    }

    if ((::property_get_int32("vendor.debug.3dnr.rsc.limit", 0) == 0) ||
        isAPSupport3DNR) {
      MUINT32 nr3d_mask = NR3DCustom::USAGE_MASK_NONE;
      if (pParsedAppConfiguration->operationMode ==
          1 /* CONSTRAINED_HIGH_SPEED_MODE */) {
        nr3d_mask |= NR3DCustom::USAGE_MASK_HIGHSPEED;
      }
    }
  }
  MY_LOGD("3DNR mode : %d, meta c(%d), force(%d) ap(%d)",
          out->StreamingParams.nr3dMode,
          pParsedAppConfiguration->sessionParams.count(), force3DNR,
          isAPSupport3DNR);

  // EIS
  MUINT8 appEisMode = 0;
  MINT32 advEisMode = 0;
  IMetadata::getEntry<MUINT8>(&pParsedAppConfiguration->sessionParams,
                              MTK_CONTROL_VIDEO_STABILIZATION_MODE,
                              &appEisMode);
  IMetadata::getEntry<MINT32>(&pParsedAppConfiguration->sessionParams,
                              MTK_EIS_FEATURE_EIS_MODE, &advEisMode);

  out->StreamingParams.bNeedLMV = MTRUE;  // LMV HW default on
  out->StreamingParams.eisExtraBufNum = 0;
  if (pParsedAppConfiguration->operationMode !=
          1 /* CONSTRAINED_HIGH_SPEED_MODE */
      && (E3DNR_MODE_MASK_ENABLED(
             out->StreamingParams.nr3dMode,
             (NSCam::NR3D::E3DNR_MODE_MASK_UI_SUPPORT |
              NSCam::NR3D::E3DNR_MODE_MASK_HAL_FORCE_SUPPORT)))) {
    out->StreamingParams.bNeedLMV = true;
  }
  return true;
}
/******************************************************************************
 *
 ******************************************************************************/
auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    evaluateConfiguration(ConfigurationOutputParams* out,
                          ConfigurationInputParams const* in) -> int {
  // check input setting is valid
  if (CC_UNLIKELY(in->pSessionParams == nullptr)) {
    CAM_LOGE("pSessionParams is invalid nullptr");
    return -ENODEV;
  }
  //
  if (CC_UNLIKELY(!evaluateCaptureConfiguration(out, in))) {
    CAM_LOGE("evaluate capture configuration failed!");
    return -ENODEV;
  }
  if (CC_UNLIKELY(!evaluateStreamConfiguration(out, in))) {
    CAM_LOGE("evaluate stream configuration failed!");
    return -ENODEV;
  }
  // Default connfig params for feature strategy.
  mConfigInputParams = *in;
  mConfigOutputParams = *out;
  //
  // TODO(MTK): implement ommon feature configuration here.
  // 1. P1 IMGO, RRZO, LCSO, RSSO configuration
  //    and those cache buffer account for zsd flow
  return OK;
}
auto NSCam::v3::pipeline::policy::featuresetting::FeatureSettingPolicy::
    evaluateRequest(RequestOutputParams* out, RequestInputParams const* in)
        -> int {
  // check setting valid.
  if
    CC_UNLIKELY(out == nullptr || in == nullptr) {
      CAM_LOGE("invalid in(%p), out(%p) for evaluate", in, out);
      return -ENODEV;
    }
  auto sensorModeSize = in->sensorModes.size();
  auto sensorIdSize = mPolicyParams.pPipelineStaticInfo->sensorIds.size();
  if (sensorModeSize != sensorIdSize) {
    CAM_LOGE(
        "input sesnorMode size(%zu) != sensorId(%zu), cannot strategy the "
        "feature policy correctly",
        sensorModeSize, sensorIdSize);
    return -ENODEV;
  }
  // keep first request config as default setting (ex: defualt sensor mode).
  if (mDefaultConfig.bInit == false) {
    MY_LOGI("keep the first request config as default config");
    mDefaultConfig.sensorMode = in->sensorModes;
    mDefaultConfig.bInit = true;
  }
  // use the default setting, features will update it later.
  out->sensorModes = mDefaultConfig.sensorMode;
  ParsedStrategyInfo parsedInfo;
  if (!collectParsedStrategyInfo(&parsedInfo, in)) {
    MY_LOGE("collectParsedStrategyInfo failed!");
    return -ENODEV;
  }
  // P2 capture feature policy
  if (in->needP2CaptureNode) {
    if (!evaluateCaptureSetting(out, parsedInfo, in)) {
      MY_LOGE("evaluateCaptureSetting failed!");
      return -ENODEV;
    }
  }
  // P2 streaming feature policy
  if (in->needP2StreamNode) {
    if (!evaluateStreamSetting(out, parsedInfo, in, in->needP2CaptureNode)) {
      MY_LOGE("evaluateStreamSetting failed!");
      return -ENODEV;
    }
  }
  // update needReconfiguration info.
  if (!evaluateReconfiguration(out, in)) {
    MY_LOGE("evaluateReconfiguration failed!");
    return -ENODEV;
  }
  return OK;
}
/******************************************************************************
 *
 ******************************************************************************/
namespace NSCam {
namespace v3 {
namespace pipeline {
namespace policy {
namespace featuresetting {
auto createFeatureSettingPolicyInstance(CreationParams const& params)
    -> std::shared_ptr<IFeatureSettingPolicy> {
  // check the policy params is valid.
  if (CC_UNLIKELY(params.pPipelineStaticInfo.get() == nullptr)) {
    CAM_LOGE("pPipelineStaticInfo is invalid nullptr");
    return nullptr;
  }
  if (CC_UNLIKELY(params.pPipelineUserConfiguration.get() == nullptr)) {
    CAM_LOGE("pPipelineUserConfiguration is invalid nullptr");
    return nullptr;
  }
  int32_t openId = params.pPipelineStaticInfo->openId;
  if (CC_UNLIKELY(openId < 0)) {
    CAM_LOGE("openId is invalid(%d)", openId);
    return nullptr;
  }
  if (CC_UNLIKELY(params.pPipelineStaticInfo->sensorIds.empty())) {
    CAM_LOGE("sensorId is empty(size:%zu)",
             params.pPipelineStaticInfo->sensorIds.size());
    return nullptr;
  }
  for (unsigned int i = 0; i < params.pPipelineStaticInfo->sensorIds.size();
       i++) {
    int32_t sensorId = params.pPipelineStaticInfo->sensorIds[i];
    CAM_LOGD("sensorId[%d]=%d", i, sensorId);
    if (CC_UNLIKELY(sensorId < 0)) {
      CAM_LOGE("sensorId is invalid(%d)", sensorId);
      return nullptr;
    }
  }
  // you have got an instance for feature setting policy.
  return std::make_shared<FeatureSettingPolicy>(params);
}
};  // namespace featuresetting
};  // namespace policy
};  // namespace pipeline
};  // namespace v3
};  // namespace NSCam
