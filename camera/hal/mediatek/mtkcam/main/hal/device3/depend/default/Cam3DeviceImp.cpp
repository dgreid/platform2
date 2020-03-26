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

#define LOG_TAG "MtkCam/Cam3Device"
//
#include "default/Cam3DeviceImp.h"
#include "default/MyUtils.h"
#include <system/camera_metadata.h>
#include <mtkcam/utils/hw/CamManagerV3.h>
#include <mtkcam/ipc/client/Mediatek3AClient.h>

#include <atomic>
#include <memory>
#include <string>
#include <utility>
//
#include <sys/prctl.h>
//
using NSCam::v3::NSDefaultCam3Device::Cam3DeviceImp;
using NSCam::v3::pipeline::model::IPipelineModel;
using NSCam::v3::pipeline::model::IPipelineModelManager;
using NSCam::v3::pipeline::model::UserConfigurationParams;
using NSCam::v3::pipeline::model::UserRequestParams;

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<NSCam::Cam3Device> createCam3Device_Default(
    std::string const& rDevName, int32_t const i4OpenId) {
  auto ptr = std::make_shared<Cam3DeviceImp>(rDevName, i4OpenId);
  return ptr;
}

/******************************************************************************
 *
 ******************************************************************************/
Cam3DeviceImp::~Cam3DeviceImp() {
  MY_LOGD("deconstruction");
}

/******************************************************************************
 *
 ******************************************************************************/
Cam3DeviceImp::Cam3DeviceImp(std::string const& rDevName,
                             int32_t const i4OpenId)
    : Cam3DeviceBase(rDevName, i4OpenId)
      //
      ,
      mRequestingAllowed(0)
      //
      ,
      mpHalSensor(nullptr)
//
#if '1' == MTKCAM_HAVE_3A_HAL
      ,
      mpHal3a(NULL)
#endif
//
{
}

/******************************************************************************
 *
 ******************************************************************************/
camera_metadata const* Cam3DeviceImp::i_construct_default_request_settings(
    int type) {
  MY_LOGD("type:%d", type);
  std::shared_ptr<ITemplateRequest> obj =
      NSTemplateRequestManager::valueFor(getOpenId());
  if (obj == nullptr) {
    obj = ITemplateRequest::getInstance(getOpenId());
    NSTemplateRequestManager::add(getOpenId(), obj);
  }
  return obj->getData(type);
}

status_t Cam3DeviceImp::deviceError(void) {
  camera3_notify_msg msg;
  msg.type = CAMERA3_MSG_ERROR;
  msg.message.error.error_code = CAMERA3_MSG_ERROR_DEVICE;
  MY_LOGE("@%s +", __func__);
  mpCallbackOps->notify(mpCallbackOps, &msg);
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
Cam3DeviceImp::onInitializeLocked() {
  if (Mediatek3AClient::getInstance())
    Mediatek3AClient::getInstance()->registerErrorCallback(this);
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
Cam3DeviceImp::onUninitializeLocked() {
  NSCam::Utils::CamProfile profile(__FUNCTION__, getDevName());

  /*
   * 12. Alternatively, the framework may call camera3_device_t->common->close()
   *    to end the camera session. This may be called at any time when no other
   *    calls from the framework are active, although the call may block until
   * all in-flight captures have completed (all results returned, all buffers
   *    filled). After the close call returns, no more calls to the
   *    camera3_callback_ops_t functions are allowed from the HAL. Once the
   *    close() call is underway, the framework may not call any other HAL
   * device functions.
   *
   */
  //
  onFlushLocked();
  disableRequesting();
  {
    std::lock_guard<std::mutex> _l(mAppContextLock);
    //
    if (mAppContext.pPipelineModel) {
      mAppContext.pPipelineModel->close();
      mAppContext.pPipelineModel = NULL;
    }
    //
    if (mAppContext.pAppStreamManager) {
      mAppContext.pAppStreamManager->destroy();
      mAppContext.pAppStreamManager = NULL;
      profile.print("AppStreamManager -");
    }
  }
  //
#if '1' == MTKCAM_HAVE_3A_HAL
  if (mpHal3a) {
    if (!mpHal3a->notifyPwrOff()) {
      CAM_TRACE_NAME("notifyPwrOff fail");
    }
  }
#endif
#if '1' == MTKCAM_HAVE_SENSOR_HAL
  {
    CAM_TRACE_NAME("Sensor powerOff");
    if (mpHalSensor) {
      MY_LOGD("HalSensor:%p", mpHalSensor);
      MUINT const sensorIndex = getOpenId();
      mpHalSensor->powerOff(getDevName(), 1, &sensorIndex);
      mpHalSensor->destroyInstance(getDevName());
      mpHalSensor = NULL;
    }
    profile.print("Sensor powerOff -");
  }
#endif  // MTKCAM_HAVE_SENSOR_HAL
  //
  //--------------------------------------------------------------------------
  //  Close 3A
#if '1' == MTKCAM_HAVE_3A_HAL
  {
    CAM_TRACE_NAME("uninit(3A)");
    mpHal3a = NULL;
    profile.print("3A Hal -");
  }
#endif  // MTKCAM_HAVE_3A_HAL
  //--------------------------------------------------------------------------
  profile.print("");

  if (Mediatek3AClient::getInstance())
    Mediatek3AClient::getInstance()->registerErrorCallback(nullptr);

  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
Cam3DeviceImp::onFlushLocked() {
  MY_LOGD("");
  //
  if (!waitUntilOpenDoneLocked()) {
    MY_LOGE("Flush, initialize fail.");
    return -ENODEV;
  }

  //
  MERROR err = flushAndWait(getSafeAppContext());
  //
  return err;
}

/******************************************************************************
 *
 ******************************************************************************/
Cam3DeviceImp::AppContext Cam3DeviceImp::getSafeAppContext() const {
  std::lock_guard<std::mutex> _l(mAppContextLock);
  AppContext context = mAppContext;
  return context;
}

/******************************************************************************
 *
 ******************************************************************************/
/**
 * flush() should only return when there are no more outstanding buffers or
 * requests left in the HAL. The framework may call configure_streams (as
 * the HAL state is now quiesced) or may issue new requests.
 *
 * Performance requirements:
 *
 * The HAL should return from this call in 100ms, and must return from this
 * call in 1000ms. And this call must not be blocked longer than pipeline
 * latency (see S7 for definition).
 */
MERROR
Cam3DeviceImp::flushAndWait(AppContext const& appContext) {
  MY_LOGD("+");
  MERROR err = OK;
  //
  if (appContext.pPipelineModel) {
    err = appContext.pPipelineModel->beginFlush();
    MY_LOGW_IF(OK != err, "IPipelineModelMgr::flush err:%d(%s)", err,
               ::strerror(-err));
  }
  //
  if (appContext.pAppStreamManager) {
    err = appContext.pAppStreamManager->waitUntilDrained(
        NSCam::Utils::ms2ns(1000));
    MY_LOGW_IF(OK != err, "AppStreamManager::waitUntilDrained err:%d(%s)", err,
               ::strerror(-err));
  }
  //
  if (appContext.pPipelineModel) {
    appContext.pPipelineModel->endFlush();
  }
  //
  MY_LOGD("-");
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
Cam3DeviceImp::enableRequesting() {
  std::lock_guard<std::mutex> _lRequesting(mRequestingLock);
  std::atomic_store_explicit(&mRequestingAllowed, 1, std::memory_order_release);
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
Cam3DeviceImp::disableRequesting() {
  std::lock_guard<std::mutex> _lRequesting(mRequestingLock);
  std::atomic_store_explicit(&mRequestingAllowed, 0, std::memory_order_release);
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
Cam3DeviceImp::onConfigureStreamsLocked(
    camera3_stream_configuration_t* stream_list) {
  CAM_TRACE_CALL();

  MY_LOGD("+");
  //
  MERROR err;
  onFlushLocked();
  std::lock_guard<std::mutex> _l(mAppContextLock);
  //

  if (mAppContext.pPipelineModel) {
    mAppContext.pPipelineModel->close();
    mAppContext.pPipelineModel = nullptr;
  }

  if (mAppContext.pAppStreamManager) {
    mAppContext.pAppStreamManager->destroy();
    mAppContext.pAppStreamManager = nullptr;
  }

  //
  mAppContext.pAppStreamManager = IAppStreamManager::create(
      getOpenId(), mpCallbackOps,
      NSMetadataProviderManager::valueFor(getOpenId()));
  if (!mAppContext.pAppStreamManager) {
    MY_LOGE("IAppStreamManager::create");
    return -ENODEV;
  }
  //
  if (!mAppContext.pPipelineModel) {
    auto pPipelineModelMgr = IPipelineModelManager::get();
    if (CC_UNLIKELY(pPipelineModelMgr == nullptr)) {
      MY_LOGE("IPipelineModelManager::get() is null object!");
      return ENODEV;
    }
    //
    auto pPipelineModel = pPipelineModelMgr->getPipelineModel(getOpenId());
    if (CC_UNLIKELY(pPipelineModel == nullptr)) {
      MY_LOGE("IPipelineModelManager::getPipelineModel(%d) is null object!",
              getOpenId());
      return ENODEV;
    }
    //
    MERROR err = OK;
    err = pPipelineModel->open("pipelinemodel", shared_from_this());
    if (CC_UNLIKELY(OK != err)) {
      MY_LOGE("fail to IPipelinemodel->open() status:%d(%s)", -err,
              ::strerror(-err));
      return ENODEV;
    }
    mAppContext.pPipelineModel = pPipelineModel;
  }
  //
  err = mAppContext.pAppStreamManager->configureStreams(stream_list);
  if (OK != err) {
    return err;
  }
  //
  IAppStreamManager::ConfigAppStreams appStreams;
  err = mAppContext.pAppStreamManager->queryConfiguredStreams(&appStreams);
  if (OK != err) {
    return err;
  }
  //
  {
    auto pParams = std::make_shared<UserConfigurationParams>();
    if (!pParams) {
      MY_LOGE("Bad UserConfigurationParams");
      return ENODEV;
    }
    pParams->operationMode = stream_list->operation_mode;
#define _CLONE_(dst, src)                           \
  do {                                              \
    auto it = src.begin();                          \
    dst.clear();                                    \
    for (size_t j = 0; j < src.size(); ++j, ++it) { \
      dst.emplace(it->first, it->second);           \
    }                                               \
  } while (0)

    _CLONE_(pParams->ImageStreams, appStreams.vImageStreams);
    _CLONE_(pParams->MetaStreams, appStreams.vMetaStreams);
    _CLONE_(pParams->MinFrameDuration, appStreams.vMinFrameDuration);
    _CLONE_(pParams->StallFrameDuration, appStreams.vStallFrameDuration);

#undef _CLONE_
    //
    err = mAppContext.pPipelineModel->configure(pParams);
    if (OK != err) {
      MY_LOGE("configure pipeline fail");
      return err;
    }
  }
  //
  enableRequesting();
  //
  MY_LOGD("-");
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
Cam3DeviceImp::i_process_capture_request(camera3_capture_request_t* request) {
  if ((request == NULL) || (request->output_buffers->stream->priv == NULL)) {
    MY_LOGE("request is NULL or UnConfig Stream!");
    return -EINVAL;
  }
  MY_LOGI("i_process_capture_request frameNo:%d", request->frame_number);

  MERROR err = OK;
  AppContext appContext = getSafeAppContext();
  IPipelineModel* pPipeline = appContext.pPipelineModel.get();
  std::shared_ptr<IAppStreamManager> pAppStreamManager =
      appContext.pAppStreamManager;
  IAppStreamManager::Request appRequest;
  //
  {
    std::lock_guard<std::mutex> _lRequesting(mRequestingLock);
    //
    if (0 == std::atomic_load_explicit(&mRequestingAllowed,
                                       std::memory_order_acquire)) {
      MY_LOGW("frameNo:%d - submitting during flushing", request->frame_number);
      flushRequest(request);
      return OK;
    }
    //
    if (!pAppStreamManager || !pPipeline) {
      MY_LOGE("Bad IAppStreamManager:%p pPipeline:%p", pAppStreamManager.get(),
              pPipeline);
      return -ENODEV;
    }
    //
    err = pAppStreamManager->createRequest(request, &appRequest);
    if (OK != err) {
      return err;
    }
    //
    err = pAppStreamManager->registerRequest(appRequest);
    if (OK != err) {
      return err;
    }
  }
#define _CLONE_(dst, src)                             \
  do {                                                \
    dst.clear();                                      \
    for (auto const& j : src) {                       \
      dst.emplace(std::make_pair(j.first, j.second)); \
    }                                                 \
  } while (0)

  std::vector<std::shared_ptr<UserRequestParams>> vPipelineRequests(1);
  auto& pItem = vPipelineRequests[0];
  pItem = std::make_shared<UserRequestParams>();
  if (!pItem) {
    MY_LOGE("Bad UserRequestParams");
    return ENODEV;
  }
  pItem->requestNo = appRequest.frameNo;
  _CLONE_(pItem->IImageBuffers, appRequest.vInputImageBuffers);
  _CLONE_(pItem->OImageBuffers, appRequest.vOutputImageBuffers);
  _CLONE_(pItem->IMetaBuffers, appRequest.vInputMetaBuffers);
#undef _CLONE_
  //
  //  Since this call may block, it should be performed out of locking.
  uint32_t numRequestProcessed = 0;
  err = pPipeline->submitRequest(vPipelineRequests, &numRequestProcessed);
  if (OK != err || 1 != numRequestProcessed) {
    MY_LOGE("%u requests submitted unsucessfully - err:(%s)",
            numRequestProcessed - err, ::strerror(-err));
    return err;
  }
  // to debug
  { mProcessRequestEnd = static_cast<MUINT64>(NSCam::Utils::getTimeInNs()); }
  //
  MY_LOGD("[end]request->frame_number: %d", request->frame_number);
  return OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
Cam3DeviceImp::flushRequest(camera3_capture_request_t* request) const {
  MY_LOGW("flushRequest frameNo:%d", request->frame_number);
  //
  {
    camera3_notify_msg msg;
    msg.type = CAMERA3_MSG_ERROR;
    msg.message.error.frame_number = request->frame_number;
    msg.message.error.error_stream = NULL;
    msg.message.error.error_code = CAMERA3_MSG_ERROR_REQUEST;
    mpCallbackOps->notify(mpCallbackOps, &msg);
  }
  //
  {
    // camera3_capture_request_t::input_buffer
    std::vector<camera3_stream_buffer_t> input_buffers;
    if (request->input_buffer) {
      input_buffers.resize(1);
      camera3_stream_buffer_t& buffer = input_buffers[0];
      buffer = *request->input_buffer;
      buffer.release_fence = buffer.acquire_fence;
      buffer.acquire_fence = -1;
    }
    // camera3_capture_request_t::output_buffers
    std::vector<camera3_stream_buffer_t> output_buffers;
    output_buffers.resize(request->num_output_buffers);
    for (uint32_t i = 0; i < request->num_output_buffers; i++) {
      camera3_stream_buffer_t& buffer = output_buffers[i];
      buffer = request->output_buffers[i];
      buffer.status = CAMERA3_BUFFER_STATUS_ERROR;
      buffer.release_fence = buffer.acquire_fence;
      buffer.acquire_fence = -1;
    }
    //
    camera3_capture_result const result = {
        .frame_number = request->frame_number,
        .result = NULL,
        .num_output_buffers = static_cast<uint32_t>(output_buffers.size()),
        .output_buffers = output_buffers.data(),
        .input_buffer = input_buffers.size() ? input_buffers.data() : NULL,
        .partial_result = 0,
    };
    mpCallbackOps->process_capture_result(mpCallbackOps, &result);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
auto Cam3DeviceImp::onFrameUpdated(
    pipeline::model::UserOnFrameUpdated const& params) -> void {
  MY_LOGD("onFrameUpdated!!!");

  MY_LOGD_IF(getLogLevel() >= 2,
             "frameNo:%u + userId:%#" PRIxPTR " OAppMeta#(left:%zd this:%zu)",
             params.requestNo, params.userId, params.nOutMetaLeft,
             params.vOutMeta.size());
  NSCam::Utils::CamProfile profile(__FUNCTION__, "Cam3DeviceCommon");
  //
  std::shared_ptr<IAppStreamManager> pAppStreamManager = getAppStreamManager();
  if (!pAppStreamManager) {
    MY_LOGE("NULL AppStreamManager");
    return;
  }
  profile.print_overtime(1,
                         "getAppStreamManager: frameNo:%u userId:%#" PRIxPTR
                         " OAppMeta#(left:%zd this:%zu)",
                         params.requestNo, params.userId, params.nOutMetaLeft,
                         params.vOutMeta.size());

  IAppStreamManager::UpdateResultParams tempParams = {
      .frameNo = params.requestNo,
      .userId = params.userId,
      .hasLastPartial = params.nOutMetaLeft <= 0,
  };

  tempParams.resultMeta.reserve(params.vOutMeta.size());
  for (size_t i = 0; i < params.vOutMeta.size(); ++i) {
    tempParams.resultMeta.push_back(params.vOutMeta[i]);
  }

  pAppStreamManager->updateResult(tempParams.frameNo, tempParams.userId,
                                  tempParams.resultMeta,
                                  tempParams.hasLastPartial);
  profile.print_overtime(1,
                         "updateResult: frameNo:%u userId:%#" PRIxPTR
                         " OAppMeta#(left:%zd this:%zu)",
                         params.requestNo, params.userId, params.nOutMetaLeft,
                         params.vOutMeta.size());
  MY_LOGD_IF(getLogLevel() >= 2,
             "frameNo:%u - userId:%#" PRIxPTR " OAppMeta#(left:%zd this:%zu)",
             params.requestNo, params.userId, params.nOutMetaLeft,
             params.vOutMeta.size());
}

bool Cam3DeviceImp::waitUntilOpenDoneLocked() {
  auto pPipelineModel = getSafePipelineModel();
  if (pPipelineModel != nullptr) {
    return pPipelineModel->waitUntilOpenDone();
  }
  return true;
}
