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

// Standard C header file

// Android system/core header file

// mtkcam custom header file

// mtkcam global header file

// Module header file

// Local header file

// Logging
#include <common/include/DebugControl.h>
#define PIPE_CLASS_TAG "PluginHandle"
#define PIPE_TRACE TRACE_BOKEH_NODE
#include <capture/CaptureFeaturePlugin.h>
#include <common/include/PipeLog.h>
#include <memory>

/*****************************************************************************
 * Namespace start.
 *****************************************************************************/
namespace NSCam {
namespace NSCamFeature {
namespace NSFeaturePipe {
namespace NSCapture {

/**************************************************************************
 * Class Define
 *************************************************************************/

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  PluginBufferHandle Implementation.
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
PluginBufferHandle::PluginBufferHandle(
    std::shared_ptr<CaptureFeatureNodeRequest> pNodeRequest, BufferID_T bufId)
    : mpNodeRequest(pNodeRequest), mpImageBuffer(nullptr), mBufferId(bufId) {}

PluginBufferHandle::~PluginBufferHandle() {
  if (mpImageBuffer != nullptr) {
    MY_LOGI("did NOT release plugin buffer:%d", mBufferId);
    release();
  }
}

IImageBuffer* PluginBufferHandle::acquire(MINT usage) {
  (void)usage;
  if (mpImageBuffer == NULL) {
    auto pNodeRequest = mpNodeRequest.lock();
    if (!pNodeRequest) {
      return NULL;
    }

    mpImageBuffer = pNodeRequest->acquireBuffer(mBufferId);
  }
  return mpImageBuffer;
}

MVOID
PluginBufferHandle::release() {
  if (mpImageBuffer != NULL) {
    mpImageBuffer = NULL;

    auto pNodeRequest = mpNodeRequest.lock();
    if (!pNodeRequest) {
      return;
    }

    pNodeRequest->releaseBuffer(mBufferId);
  }
}

MVOID
PluginBufferHandle::dump(std::ostream& os) const {
  if (mpImageBuffer == NULL) {
    os << "{ null }" << std::endl;
  } else {
    os << "{address: " << mpImageBuffer << "}" << std::endl;
  }
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  PluginMetadataHandle Implementation.
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
PluginMetadataHandle::PluginMetadataHandle(
    std::shared_ptr<CaptureFeatureNodeRequest> pNodeRequest,
    MetadataID_T metaId)
    : mpNodeRequest(pNodeRequest), mpMetadata(NULL), mMetaId(metaId) {}

PluginMetadataHandle::~PluginMetadataHandle() {
  if (mpMetadata != NULL) {
    MY_LOGI("did NOT release plugin metadata:%d", mMetaId);
    release();
  }
}

IMetadata* PluginMetadataHandle::acquire() {
  if (mpMetadata == NULL) {
    auto pNodeRequest = mpNodeRequest.lock();
    if (!pNodeRequest) {
      return NULL;
    }
    mpMetadata = pNodeRequest->acquireMetadata(mMetaId);
  }
  return mpMetadata;
}

MVOID
PluginMetadataHandle::release() {
  if (mpMetadata != NULL) {
    mpMetadata = NULL;

    auto pNodeRequest = mpNodeRequest.lock();
    if (!pNodeRequest) {
      return;
    }
    pNodeRequest->releaseMetadata(mMetaId);
  }
}

MVOID
PluginMetadataHandle::dump(std::ostream& os) const {
  if (mpMetadata == NULL) {
    os << "{ null }" << std::endl;
  } else {
    os << "{count: " << mpMetadata->count() << "}" << std::endl;
  }
}

}  // namespace NSCapture
}  // namespace NSFeaturePipe
}  // namespace NSCamFeature
}  // namespace NSCam
