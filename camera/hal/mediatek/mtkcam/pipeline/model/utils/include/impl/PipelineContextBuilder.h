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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_MODEL_UTILS_INCLUDE_IMPL_PIPELINECONTEXTBUILDER_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_MODEL_UTILS_INCLUDE_IMPL_PIPELINECONTEXTBUILDER_H_

#include <mtkcam/pipeline/pipeline/PipelineContext.h>

#include <impl/types.h>

#include <memory>
#include <string>
#include <vector>

/******************************************************************************
 *
 ******************************************************************************/
namespace NSCam {
namespace v3 {
namespace pipeline {
namespace model {

/**
 * Used on the call buildPipelineContext().
 */
struct BuildPipelineContextInputParams {
  std::string pipelineName;

  /**
   * Pipeline static info. and user configuration given at the configuration
   * stage.
   */
  PipelineStaticInfo const* pPipelineStaticInfo = nullptr;
  PipelineUserConfiguration const* pPipelineUserConfiguration = nullptr;

  /**
   * Non-P1-specific stream info configuration.
   */
  ParsedStreamInfo_NonP1 const* pParsedStreamInfo_NonP1 = nullptr;

  /**
   * P1-specific stream info configuration.
   */
  std::vector<ParsedStreamInfo_P1> const* pvParsedStreamInfo_P1 = nullptr;

  /**
   *  Replaced provider of P1 for ZSL streambuffer acquisition.
   */
  std::shared_ptr<Utils::IStreamBufferProvider> pZSLProvider = nullptr;

  /**
   * The sensor settings.
   */
  std::vector<SensorSetting> const* pvSensorSetting = nullptr;

  /**
   * P1 hardware settings.
   */
  std::vector<P1HwSetting> const* pvP1HwSetting = nullptr;

  /**
   * It indicates which pipeline nodes are needed.
   */
  PipelineNodesNeed const* pPipelineNodesNeed = nullptr;

  /**
   * The streaming feature settings.
   */
  StreamingFeatureSetting const* pStreamingFeatureSetting = nullptr;

  /**
   * The capture feature settings.
   */
  CaptureFeatureSetting const* pCaptureFeatureSetting = nullptr;

  /**
   * Batch size
   *
   * A batch size is a divisor of fps / 30. For example, if fps is 300, a batch
   * size could only be one of only be 1, 2, 5, or 10.
   *
   * See static_android.control.availableHighSpeedVideoConfigurations under
   * https://android.googlesource.com/platform/system/media/+/HEAD/camera/docs/docs.html
   */
  uint32_t batchSize = 0;

  /**
   * Old pipeline context.
   *
   * Passing 'nullptr' indicates no old pipeline context.
   * Callers must make sure "pOldPipelineContext" and "out" are different
   * instances.
   */
  std::shared_ptr<NSPipelineContext::PipelineContext> const pOldPipelineContext;

  std::shared_ptr<NSPipelineContext::IDataCallback> const pDataCallback =
      nullptr;

  /**
   * using multi-thread to init/config each node.
   */
  bool bUsingMultiThreadToBuildPipelineContext = true;
  bool bIsReconfigure = false;
};

/**
 * Generate a new pipeline context.
 *
 * @param[out] out: a new-created pipeline context.
 *
 * @param[in] in: input parameters.
 *
 * @return
 *      0 indicates success; otherwise failure.
 */
auto VISIBILITY_PUBLIC
buildPipelineContext(std::shared_ptr<NSPipelineContext::PipelineContext>* out,
                     BuildPipelineContextInputParams const& in) -> int;

/******************************************************************************
 *
 ******************************************************************************/
};      // namespace model
};      // namespace pipeline
};      // namespace v3
};      // namespace NSCam
#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_MODEL_UTILS_INCLUDE_IMPL_PIPELINECONTEXTBUILDER_H_
