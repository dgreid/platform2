/*
 * Copyright (C) 2017-2020 Intel Corporation.
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

#ifndef PSL_IPU3_WORKERS_IDEVICEWORKER_H_
#define PSL_IPU3_WORKERS_IDEVICEWORKER_H_

#include "RequestCtrlState.h"
#include "GraphConfig.h"
#include "tasks/ExecuteTaskBase.h"
#include "Camera3Request.h"
#include <linux/intel-ipu3.h>

namespace cros {
namespace intel {

enum DeviceMessageId {
    MESSAGE_ID_EXIT = 0,
    MESSAGE_COMPLETE_REQ,
    MESSAGE_ID_POLL,
    MESSAGE_ID_FLUSH,
    MESSAGE_ID_MAX
};

class MessageCallbackMetadata {
public:
    Camera3Request* request;
    bool updateMeta;

    MessageCallbackMetadata():
        request(nullptr),
        updateMeta(false) {}
};

class MessagePollEvent {
public:
    int requestId;
    std::shared_ptr<cros::V4L2VideoNode> *activeDevices;
    int polledDevices;
    int numDevices;
    IPollEventListener::PollEventMessageId pollMsgId;

    MessagePollEvent() : requestId(-1),
            activeDevices(nullptr),
            polledDevices(0),
            numDevices(0),
            pollMsgId(IPollEventListener::POLL_EVENT_ID_ERROR) {}
};

class DeviceMessage {
public:
    DeviceMessageId id;
    ProcTaskMsg pMsg;
    MessageCallbackMetadata cbMetadataMsg;
    MessagePollEvent pollEvent;

    DeviceMessage():
        id(MESSAGE_ID_MAX) {}
};

class IDeviceWorker
{
public:
    explicit IDeviceWorker(std::shared_ptr<cros::V4L2VideoNode> node, int cameraId) :
                                           mNode(node), mCameraId(cameraId) {}
    virtual ~IDeviceWorker() {}

    virtual status_t configure(std::shared_ptr<GraphConfig> &config) = 0;
    virtual status_t startWorker() = 0;
    virtual status_t stopWorker() = 0;
    virtual status_t prepareRun(std::shared_ptr<DeviceMessage> msg) = 0;
    virtual status_t run() = 0;
    virtual status_t postRun() = 0;
    virtual bool needPolling() = 0;
    virtual std::shared_ptr<cros::V4L2VideoNode> getNode() const { return mNode; }

protected:
    std::shared_ptr<DeviceMessage> mMsg; /*!Set in prepareRun and should be valid until
                           postRun is called */
    std::shared_ptr<cros::V4L2VideoNode> mNode;
    int mCameraId;
private:
    IDeviceWorker();
};

} /* namespace intel */
} /* namespace cros */

#endif /* PSL_IPU3_WORKERS_IDEVICEWORKER_H_ */
