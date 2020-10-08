/*
 * Copyright (C) 2017 Intel Corporation
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

#define LOG_TAG "GraphConfig"

#include "GraphConfig.h"
#include "GraphConfigManager.h"
#include "LogHelper.h"
#include "FormatUtils.h"
#include "NodeTypes.h"
#include "Camera3GFXFormat.h"
#include "PlatformData.h"
#include <GCSSParser.h>
#include <v4l2device.h>
#include <linux/v4l2-subdev.h>
#include <algorithm>

#include "MediaEntity.h"

using GCSS::GraphConfigNode;
using std::string;
using std::vector;
using std::map;
using std::set;


namespace android {
namespace camera2 {

extern int32_t gDumpType;

namespace gcu = graphconfig::utils;

// TODO: Change the format attribute natively as integer attribute
#ifndef VIDEO_RECORDING_FORMAT
#define VIDEO_RECORDING_FORMAT TILE
#endif

#define MEDIACTL_PAD_OUTPUT_NUM 2
#define MEDIACTL_PAD_VF_NUM 3
#define MEDIACTL_PAD_PV_NUM 4
#define SCALING_FACTOR 1

const string csi2_without_port = "rockchip-sy-mipi-dphy";

/* isp port name*/
const string MEDIACTL_INPUTNAME = "input";
const string MEDIACTL_OUTPUTNAME = "output";

/*video entity name*/
const string MEDIACTL_PARAMETERNAME = "rkisp1-input-params";

const string MEDIACTL_VIDEONAME = "rkisp1_mainpath";
const string MEDIACTL_STILLNAME = "rkisp1_mainpath";

const string MEDIACTL_PREVIEWNAME = "rkisp1_selfpath";
const string MEDIACTL_POSTVIEWNAME = "postview";

const string MEDIACTL_STATNAME = "rkisp1-statistics";

GraphConfig::GraphConfig() :
        mManager(nullptr),
        mSettings(nullptr),
        mReqId(0),
        mMetaEnabled(false),
        mFallback(false),
        mPipeType(PIPE_PREVIEW),
        mSourceType(SRC_NONE)
{
    //CLEAR(mProgramGroup);
    mSourcePortName.clear();
    mSinkPeerPort.clear();
    mStreamToSinkIdMap.clear();
    mStream2TuningMap.clear();
    createKernelListStructures();
    mCSIBE = CSI_BE + "0";
    mMainNodeName.clear();
    mSecondNodeName.clear();
}

GraphConfig::~GraphConfig()
{
    fullReset();
}
/*
 * Full reset
 * This is called whenever we want to reset the whole object. Currently that
 * is only, when GraphConfig object is destroyed.
 */
void GraphConfig::fullReset()
{
    mSourcePortName.clear();
    mSinkPeerPort.clear();
    mStreamToSinkIdMap.clear();
    mStreamIds.clear();
    deleteKernelInfo();
    delete mSettings;
    mSettings = nullptr;
    mManager = nullptr;
    mReqId = 0;
    mStream2TuningMap.clear();
}
/*
 * Reset
 * This is called per frame
 */
void GraphConfig::reset(GraphConfig *me)
{
    if (CC_LIKELY(me != nullptr)) {
        me->mReqId = 0;
    } else {
        LOGE("Trying to reset a null GraphConfig - BUG!");
    }
}

void GraphConfig::deleteKernelInfo() {
}

void GraphConfig::createKernelListStructures()
{
}

const GCSS::IGraphConfig* GraphConfig::getInterface(Node *node) const
{
    if (!node)
        return nullptr;

    return node;
}

const GCSS::IGraphConfig* GraphConfig::getInterface() const
{
    return mSettings;
}

/**
 * Per frame initialization of graph config.
 * Updates request id
 * \param[in] reqId
 */
void GraphConfig::init(int32_t reqId)
{
    mReqId = reqId;
}

/**
 * Prepare graph config once per stream config.
 * \param[in] manager
 * \param[in] settings
 * \param[in] streamToSinkIdMap
 * \param[in] active
 */
status_t GraphConfig::prepare(GraphConfigManager *manager,
                              Node *settings,
                              StreamToSinkMap &streamToSinkIdMap,
                              bool fallback)
{
    mStreamIds.clear();
    mManager = manager;
    mSettings = settings;
    mFallback = fallback;
    status_t ret = OK;

    if (CC_UNLIKELY(settings == nullptr)) {
        LOGW("Settings is nullptr!! - BUG?");
        return UNKNOWN_ERROR;
    }

    ret = analyzeSourceType();
    if (ret != OK) {
        LOGE("Failed to analyze source type");
        return ret;
    }

    ret = getActiveOutputPorts(streamToSinkIdMap);
    if (ret != OK) {
        LOGE("Failed to get output ports");
        return ret;
    }

    ret = generateKernelListsForStreams();
    if (ret != OK) {
        LOGE("Failed to generate kernel list");
        return ret;
    }

    calculateSinkDependencies();
    storeTuningModes();
    return ret;
}

/**
 * Store the tuning modes for each stream id into a map that can be used on a
 * per frame basis.
 * This method is executed once per stream configuration.
 * The tuning mode is used by AIC to find the correct tuning tables in CPF.
 *
 */
void GraphConfig::storeTuningModes()
{
    GraphConfigNode::const_iterator it = mSettings->begin();
    css_err_t ret = css_err_none;
    GraphConfigNode *result = nullptr;
    int32_t tuningMode = 0;
    int32_t streamId = 0;
    mStream2TuningMap.clear();

    while (it != mSettings->end()) {
        ret = mSettings->getDescendant(GCSS_KEY_TYPE, "program_group", it, &result);
        if (ret == css_err_none) {
            ret = result->getValue(GCSS_KEY_STREAM_ID, streamId);
            if (ret != css_err_none) {
                string pgName;
                // This should  not fail
                ret = result->getValue(GCSS_KEY_NAME, pgName);
                LOGW("Failed to find stream id for PG %s", pgName.c_str());
                continue;
            }
            tuningMode = 0; // default value in case it is not found
            ret = result->getValue(GCSS_KEY_TUNING_MODE, tuningMode);
            if (ret != css_err_none) {
                string pgName;
                // This should  not fail
                ret = result->getValue(GCSS_KEY_NAME, pgName);
                LOGW("Failed t find tuning mode for PG %s, defaulting to %d",
                        pgName.c_str(), tuningMode);
            }
            mStream2TuningMap[streamId] =  tuningMode;
        }
    }
}
/**
 * Retrieve the tuning mode associated with a given stream id.
 *
 * The tuning mode is defined by IQ-studio and represent and index to different
 * set of tuning parameters in the AIQB (a.k.a CPF)
 *
 * The tuning mode is an input parameter for AIC.
 * \param [in] streamId Identifier for the branch (video/still/isa)
 * \return tuning mode, if stream id is not found defaults to 0
 */
int32_t GraphConfig::getTuningMode(int32_t streamId)
{
    auto item = mStream2TuningMap.find(streamId);
    if (item != mStream2TuningMap.end()) {
        return item->second;
    }
    LOGW("Could not find tuning mode for requested stream id %d", streamId);
    return 0;
}

/*
 * According to the node, analyze the source type:
 * TPG or sensor
 */
status_t GraphConfig::analyzeSourceType()
{
    css_err_t ret = css_err_none;
    Node *inputDevNode = nullptr;
    ret = mSettings->getDescendant(GCSS_KEY_SENSOR, &inputDevNode);
    if (ret == css_err_none) {
        mSourceType = SRC_SENSOR;
        mSourcePortName = SENSOR_PORT_NAME;
    } else {
        LOG1("No sensor node from the graph");
    }
    return OK;
}

/**
 * Finds the sink nodes and the output port peer. Use streamToSinkIdMap
 * since we are intrested only in sinks that serve a stream. Takes an
 * internal copy of streamToSinkIdMap to be used later.
 *
 * \param[in] streamToSinkIdMap to get the virtual sink id from a client stream pointer
 * \return OK in case of success.
 * \return UNKNOWN_ERROR or BAD_VALUE in case of fail.
 */
status_t GraphConfig::getActiveOutputPorts(const StreamToSinkMap &streamToSinkIdMap)
{
    status_t status = OK;
    css_err_t ret = css_err_none;
    NodesPtrVector sinks;

    mStreamToSinkIdMap.clear();
    mStreamToSinkIdMap = streamToSinkIdMap;
    mSinkPeerPort.clear();

    StreamToSinkMap::const_iterator it;
    it = streamToSinkIdMap.begin();

    for (; it != streamToSinkIdMap.end(); ++it) {
        sinks.clear();
        status = graphGetSinksByName(GCSS::ItemUID::key2str(it->second), sinks);
        if (CC_UNLIKELY(status != OK) || sinks.empty()) {
            string sinkName = GCSS::ItemUID::key2str(it->second);
            LOGE("Found %zu sinks, expecting 1 for sink %s", sinks.size(),
                 sinkName.c_str());
            return BAD_VALUE;
        }

        Node *sink = sinks[0];
        Node *outputPort = nullptr;

        // Get the sinkname for getting the output port
        string sinkName;
        ret =  sink->getValue(GCSS_KEY_NAME, sinkName);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Failed to get sink name");
            return BAD_VALUE;
        }
        LOG2("sink name %s", sinkName.c_str());


        int32_t streamId = -1;
        ret = sink->getValue(GCSS_KEY_STREAM_ID, streamId);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Failed to get stream id");
            return BAD_VALUE;
        }
        LOG2("stream id %d", streamId);

        outputPort = getOutputPortForSink(sinkName);
        if (outputPort == nullptr) {
            LOGE("No output port found for sink");
            return UNKNOWN_ERROR;
        }

        LOG2("output port name %s", NODE_NAME(outputPort));
        mSinkPeerPort[sink] = outputPort;
    }

    return OK;
}

string GraphConfig::getNodeName(Node * node)
{
    string nodeName("");
    if (node == nullptr) {
        LOGE("Node is nullptr");
        return nodeName;
    }

    node->getValue(GCSS_KEY_NAME, nodeName);
    return nodeName;
}

/**
 * Finds the output port which is the peer to the sink node.
 *
 * Gets root node, and finds the sink with the given name. Use portGetPeer()
 * to find the output port.
 * \return GraphConfigNode in case of success.
 * \return nullptr in case of fail.
 */
GraphConfig::Node *GraphConfig::getOutputPortForSink(const string &sinkName)
{
    css_err_t ret = css_err_none;
    status_t retErr = OK;
    Node *rootNode = nullptr;
    Node *portNode = nullptr;
    Node *peerNode = nullptr;

    rootNode = mSettings->getRootNode();
    if (rootNode == nullptr) {
        LOGE("Couldn't get root node, BUG!");
        return nullptr;
    }
    ret = rootNode->getDescendantByString(sinkName, &portNode);
    if (ret != css_err_none) {
        LOGE("Error getting sink");
        return nullptr;
    }
    retErr = portGetPeer(portNode, &peerNode);
    if (retErr != OK) {
        LOGE("Error getting peer");
        return nullptr;
    }
    return portNode;
}

/**
 * Returns true if the given node is used to output a video record
 * stream. The sink name is found and used to find client stream from the
 * mStreamToSinkIdMap.
 * Then the video encoder gralloc flag is checked from the stream flags of the
 * client stream.
 * \param[in] peer output port to find the sink node of.
 * \return true if sink port serves a video record stream.
 * \return false if sink port does not serve a video record stream.
 */
bool GraphConfig::isVideoRecordPort(Node *sink)
{
    css_err_t ret = css_err_none;
    string sinkName;
    camera3_stream_t* clientStream = nullptr;

    if (sink == nullptr) {
        LOGE("No sink node provided");
        return false;
    }

    ret = sink->getValue(GCSS_KEY_NAME, sinkName);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get sink name");
        return false;
    }

    // Find the client stream for the sink port
    StreamToSinkMap::iterator it1;
    it1 = mStreamToSinkIdMap.begin();

    for (; it1 != mStreamToSinkIdMap.end(); ++it1) {
        if (GCSS::ItemUID::key2str(it1->second) == sinkName) {
            clientStream = it1->first;
            break;
        }
    }

    if (clientStream == nullptr) {
        LOGE("Failed to find client stream");
        return false;
    }

    if (CHECK_FLAG(clientStream->usage, GRALLOC_USAGE_HW_VIDEO_ENCODER)) {
        LOG2("%s is video record port", NODE_NAME(sink));
        return true;
    }

    return false;
}

/**
 * Takes a stream id, and checks if it exists in the graph.
 *
 * \param[in] streamId
 * \return true if found, false otherwise
 */
bool GraphConfig::hasStreamInGraph(int streamId)
{
    status_t status;
    StreamsVector streamsFound;

    status = graphGetStreamIds(streamsFound);
    if (status != OK)
        return false;

    for (size_t i = 0; i < streamsFound.size(); i++) {
        if (streamsFound[i] == streamId)
            return true;
    }
    return false;
}

/**
 * check whether the kernel is in this stream
 *
 * \param[in] streamId stream id.
 * \param[in] kernelId kernel id.
 * \param[out] whether the kernel in this stream
 * \return true the kernel is in this stream
 * \return false the kernel isn't in this stream.
 *
 */
bool GraphConfig::isKernelInStream(uint32_t streamId, uint32_t kernelId)
{
    return false;
}

/**
 * get program group id for some kernel
 *
 * \param[in] streamId stream id.
 * \param[in] kernelId kernel pal uuid.
 * \param[out] program group id that contain this kernel with the same stream id
 * \return error if can't find the kernel id in any ot the PGs in this stream
 */
status_t GraphConfig::getPgIdForKernel(const uint32_t streamId, const int32_t kernelId, int32_t &pgId)
{
    css_err_t ret = css_err_none;
    status_t retErr;
    NodesPtrVector programGroups;

    // Get all program groups with the stream id
    retErr = streamGetProgramGroups(streamId, programGroups);
    if (retErr != OK) {
        LOGE("ERROR: couldn't get program groups");
        return retErr;
    }

    // Go through all the program groups with the selected streamID
    for (uint32_t i = 0; i < programGroups.size(); i++) {
        /* Iterate through program group nodes, find kernel and get the PG id */
        GCSS::GraphConfigItem::const_iterator it = programGroups[i]->begin();
        while (it != programGroups[i]->end()) {
            Node *kernelNode = nullptr;
            // Look for kernel with the requested uuid
            ret = programGroups[i]->getDescendant(GCSS_KEY_PAL_UUID,
                                                  kernelId,
                                                  it,
                                                  &kernelNode);
            if (ret != css_err_none)
                continue;

            ret = programGroups[i]->getValue(GCSS_KEY_PG_ID, pgId);
            if (CC_LIKELY(ret == css_err_none)) {
                LOG2("got the pgid:%d for kernel id:%d in stream:%d",
                        pgId, kernelId, streamId);
                return NO_ERROR;
            }
            LOGE("ERROR: Couldn't get pg id for kernel %d", kernelId);
            return BAD_VALUE;
        }
    }
    LOGE("ERROR: Couldn't get pal_uuid");
    return BAD_VALUE;
}

/**
 * Retrieve all the sinks in the current graph configuration that match the
 * input parameter string in their name attribute.
 *
 * If the name to match is empty it returns all the nodes of type sink
 *
 * \param[in] name String containing the name to match.
 * \param[out] sink List of sinks that match that name
 * \return OK in case of success
 * \return UNKNOWN_ERROR if no sinks were found in the graph config.
 *
 */
status_t GraphConfig::graphGetSinksByName(const std::string &name,
                                          NodesPtrVector &sinks)
{
    css_err_t ret = css_err_none;
    GraphConfigNode *result;
    NodesPtrVector allSinks;
    string foundName;
    GraphConfigNode::const_iterator it = mSettings->begin();

    while (it != mSettings->end()) {
        ret = mSettings->getDescendant(GCSS_KEY_TYPE, "sink", it, &result);
        if (ret == css_err_none) {
            allSinks.push_back(result);
        }
    }

    if (allSinks.empty()) {
        LOGE("Failed to find any sinks -check graph config file");
        return UNKNOWN_ERROR;
    }
    /*
     * if the name is empty it means client wants all sinks.
     */
    if (name.empty()) {
        sinks = allSinks;
        return OK;
    }

    for (size_t i = 0; i < allSinks.size(); i++) {
        allSinks[i]->getValue(GCSS_KEY_NAME, foundName);
        if (foundName.find(name) != string::npos) {
            sinks.push_back(allSinks[i]);
        }
    }

    return OK;
}

/*
 * Imgu helper functions
 */
status_t GraphConfig::graphGetDimensionsByName(const std::string &name,
                                          int &widht, int &height)
{
    widht = 0;
    height = 0;
    Node *csiBEOutput = nullptr;

    // Get csi_be node. If not found, try csi_be_soc. If not found return error.
    int ret = mSettings->getDescendantByString(name, &csiBEOutput);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't find node: %s", name.c_str());
        return UNKNOWN_ERROR;
    }

    ret = getDimensions(csiBEOutput, widht, height);
    if (ret != OK) {
        LOGE("Error: Couldn't find dimensions from <%s>", name.c_str());
        return UNKNOWN_ERROR;
    }

    return OK;
}

/*
 * Imgu helper functions
 */
status_t GraphConfig::graphGetDimensionsByName(const std::string &name,
                                          unsigned short &widht, unsigned short &height)
{
    int w, h;
    int ret = graphGetDimensionsByName(name, w, h);
    widht = w;
    height = h;

    return ret;
}

/**
 * This method creates SinkDependency structure for every active sink found in
 * the graph. These structs allow quick access to information that is required
 * by other methods.
 * Active sinks are the ones that have a connection to an active port.
 * This list of active sinks(mSinkPeerPort) has to be filled before this method
 * is executed.
 * For every virtual sink we store the name (as a key) and the terminal id of
 * the input port of the stream associated with that stream. This input port
 * will be the destination of the buffers from the capture unit.
 *
 * This method is used during init()
 * If we would have different settings per frame then this would be enough
 * to detect the active ISA nodes, but we are not there yet. we are still using
 * the base graph settings every frame.
 */
void GraphConfig::calculateSinkDependencies()
{
    status_t status = OK;
    Node *streamInputPort = nullptr;
    NodesPtrVector sinks;
    std::string sinkName;
    SinkDependency aSinkDependency;
    uint32_t stageId; //not needed
    mSinkDependencies.clear();
    mIsaOutputPort2StreamId.clear();
    map<Node*, Node*>::iterator sinkIter = mSinkPeerPort.begin();

    for (; sinkIter != mSinkPeerPort.end(); sinkIter++) {
        sinkIter->first->getValue(GCSS_KEY_NAME, sinkName);
        aSinkDependency.sinkGCKey = GCSS::ItemUID::str2key(sinkName);
        aSinkDependency.streamId = sinkGetStreamId(sinkIter->first);
        status = streamGetInputPort(aSinkDependency.streamId,
                                    &streamInputPort);
        if (CC_UNLIKELY(status != OK)) {
            LOGE("Failed to get input port for stream %d associated to sink %s",
                    aSinkDependency.streamId, sinkName.c_str());
            continue;
        }
        status = portGetFourCCInfo(*streamInputPort,
                                   stageId,
                                   aSinkDependency.streamInputPortId);
        if (CC_UNLIKELY(status != OK)) {
            LOGE("Failed to get stream %d input port 4CC code",
                    aSinkDependency.streamId);
            continue;
        }
        LOG2("Adding dependency %s stream id %d", sinkName.c_str(), aSinkDependency.streamId);
        mSinkDependencies.push_back(aSinkDependency);

        // get the output port of capture unit
        Node* isaOutPutPort = nullptr;
        status = portGetPeer(streamInputPort, &isaOutPutPort);
        if (CC_UNLIKELY(status != OK)) {
            LOGE("Fail to get isa output port for sink %s", sinkName.c_str());
            continue;
        }
        std::string fullName;
        status = portGetFullName(isaOutPutPort, fullName);
        if (CC_UNLIKELY(status != OK)) {
            LOGE("Fail to get isa output port name");
            continue;
        }
        int32_t streamId = portGetStreamId(isaOutPutPort);
        if (streamId != -1 &&
            mIsaOutputPort2StreamId.find(fullName) == mIsaOutputPort2StreamId.end())
            mIsaOutputPort2StreamId[fullName] = streamId;
    }
}

/**
 * This method is used by the GC Manager that has access to the request
 * to inform us of what are the active sinks.
 * Using the sink dependency information we can then know which ISA ports
 * are active for this GC.
 *
 * Once we have different settings per request then we can incorporate this
 * method into calculateSinkDependencies.
 *
 * \param[in] activeSinks Vector with GCSS_KEY's of the active sinks in a
 *                        request
 */
void GraphConfig::setActiveSinks(std::vector<uid_t> &activeSinks)
{
    mIsaActiveDestinations.clear();
    uid_t activeDest = 0;

    for (size_t i = 0; i < activeSinks.size(); i++) {
        for (size_t j = 0; j < mSinkDependencies.size(); j++) {
            if (mSinkDependencies[j].sinkGCKey == activeSinks[i]) {
                activeDest = mSinkDependencies[j].streamInputPortId;
                mIsaActiveDestinations[activeDest] = activeDest;
            }
        }
    }
}

/**
 * This method is used by the GC Manager that has access to the request
 * to inform us of what will the stream id be used.
 * Using the sink dependency information we can then know which stream ids
 * are active for this GC.
 *
 * Once we have different settings per request then we can incorporate this
 * method into calculateSinkDependencies.
 *
 * \param[in] activeSinks Vector with GCSS_KEY's of the active sinks in a
 *                        request
 */
void GraphConfig::setActiveStreamId(const std::vector<uid_t> &activeSinks)
{
    mActiveStreamId.clear();
    int32_t activeStreamId = 0;
    status_t status = NO_ERROR;

    for (size_t i = 0; i < activeSinks.size(); i++) {
        for (size_t j = 0; j < mSinkDependencies.size(); j++) {
            if (mSinkDependencies[j].sinkGCKey == activeSinks[i]) {
                activeStreamId = mSinkDependencies[j].streamId;
                mActiveStreamId.insert(activeStreamId);
                // get the input port for this stream
                GraphConfig::Node *port;
                GraphConfig::Node *peer;
                status = streamGetInputPort(activeStreamId, &port);
                if (status != NO_ERROR) {
                    LOGD("Fail to get input port for this stream %d", activeStreamId);
                    continue;
                }

                status = portGetPeer(port, &peer);
                if (status != NO_ERROR) {
                    LOGE("fail to get peer for the port");
                    continue;
                }

                // get peer's stream Id
                activeStreamId = portGetStreamId(peer);
                if (activeStreamId == -1) {
                    LOGE("fail to get the stream id for %s peer port %s", NODE_NAME(port), NODE_NAME(peer));
                    continue;
                }
                if (mActiveStreamId.find(activeStreamId) == mActiveStreamId.end())
                    mActiveStreamId.insert(activeStreamId);
            }
        }
    }
}

/**
 * returns the number of buffers the ISA will produce for a given request.
 */
int32_t GraphConfig::getIsaOutputCount() const
{
    return mIsaActiveDestinations.size();
}

bool GraphConfig::isIsaOutputDestinationActive(uid_t destinationPortId) const
{
    std::map<uid_t, uid_t>::const_iterator it = mIsaActiveDestinations.begin();
    for (; it != mIsaActiveDestinations.end(); ++it) {
        if (destinationPortId == it->first) {
            return true;
        }
    }
    return false;
}

bool GraphConfig::isIsaStreamActive(int32_t streamId) const
{
    if (mActiveStreamId.find(streamId) != mActiveStreamId.end())
        return true;
    return false;
}

status_t GraphConfig::getActiveDestinations(std::vector<uid_t> &terminalIds) const
{
    std::map<uid_t, uid_t>::const_iterator it = mIsaActiveDestinations.begin();
    for (; it != mIsaActiveDestinations.end(); ++it) {
        terminalIds.push_back(it->first);
    }

    return OK;
}

/**
 * Query the connection info structs for a given pipeline defined by
 * stream id.
 *
 * \param[in] sinkName to be used as key to get pipeline connections
 * \param[out] stream id connect with sink
 * \param[out] connections for pipeline configuation
 * \return OK in case of success.
 * \return UNKNOWN_ERROR or BAD_VALUE in case of fail.
 * \if sinkName is not supported, NAME_NOT_FOUND is returned.
 * \sink name support list as below defined in graph_descriptor.xml
 * \<sink name="video0"/>
 * \<sink name="video1"/>
 * \<sink name="video2"/>
 * \<sink name="still0"/>
 * \<sink name="still1"/>
 * \<sink name="still2"/>
 * \<sink name="raw"/>
 */
status_t GraphConfig::pipelineGetInternalConnections(
                          const std::string &sinkName,
                          int &streamId,
                          std::vector<PSysPipelineConnection> &confVector)
{
    HAL_TRACE_CALL(CAMERA_DEBUG_LOG_LEVEL2);

    status_t status = OK;
    css_err_t ret = css_err_none;
    NodesPtrVector sinks;
    NodesPtrVector programGroups;
    NodesPtrVector alreadyConnectedPorts;
    Node *peerPort = nullptr;
    Node *port = nullptr;
    PSysPipelineConnection aConnection;

    CLEAR(aConnection);

    alreadyConnectedPorts.clear();
    status = graphGetSinksByName(sinkName, sinks);
    if (CC_UNLIKELY(status != OK) || sinks.empty()) {
        LOGD("No %s sinks in graph", sinkName.c_str());
        return NAME_NOT_FOUND;
    }

    streamId = sinkGetStreamId(sinks[0]);
    if (CC_UNLIKELY(streamId <= 0)) {
        LOGE("Sink node lacks stream id attribute - fix your config");
        return BAD_VALUE;
    }

    status = streamGetProgramGroups(streamId, programGroups);
    if (CC_UNLIKELY(status != OK || programGroups.empty())) {
        LOGE("No Program groups associated with stream id %d", streamId);
        return BAD_VALUE;
    }

    for (size_t i = 0; i < programGroups.size(); i++) {

        Node::const_iterator it = programGroups[i]->begin();

        while (it != programGroups[i]->end()) {
            ret = programGroups[i]->getDescendant(GCSS_KEY_TYPE, "port",
                                                it, &port);
            if (ret != css_err_none)
                continue;

            /*
             * Since we are iterating through the ports
             * check if this port is already connected to avoid setting
             * the connection twice
             */
            if (std::find(alreadyConnectedPorts.begin(),
                          alreadyConnectedPorts.end(),
                          port) != alreadyConnectedPorts.end()) {
                continue;
            }
            LOG1("Configuring Port from PG[%zu]", i);

            status = portGetFormat(port, aConnection.portFormatSettings);
            if (CC_UNLIKELY(status != OK)) {
                LOGE("Failed to get port format info in port from PG[%zu] "
                     "from stream id %d", i, streamId);
                return BAD_VALUE;
            }
            if (aConnection.portFormatSettings.enabled == 0) {
                LOG1("Port from PG[%zu] from stream id %d disabled",
                        i, streamId);
                confVector.push_back(aConnection);
                continue;
            } else {
                LOG1("Port: 0x%x format(%dx%d)fourcc: %s bpl: %d bpp: %d",
                        aConnection.portFormatSettings.terminalId,
                        aConnection.portFormatSettings.width,
                        aConnection.portFormatSettings.height,
                        v4l2Fmt2Str(aConnection.portFormatSettings.fourcc),
                        aConnection.portFormatSettings.bpl,
                        aConnection.portFormatSettings.bpp);
            }

            /*
             * for each port get the connection info and pass it
             * to the pipeline object
             */
            status = portGetConnection(port,
                                       aConnection.connectionConfig,
                                       &peerPort);
            if (CC_UNLIKELY(status != OK )) {
                LOGE("Failed to create connection info in port from PG[%zu]"
                     "from stream id %d", i, streamId);
                return BAD_VALUE;
            }

            aConnection.hasEdgePort = false;
            if (isPipeEdgePort(port)) {
                camera3_stream_t *clientStream = nullptr;
                status = portGetClientStream(peerPort, &clientStream);
                if (CC_UNLIKELY(status != OK)) {
                    LOGE("Failed to find client stream for v-sink");
                    return UNKNOWN_ERROR;
                }
                aConnection.stream = clientStream;
                aConnection.hasEdgePort = true;
            }
            confVector.push_back(aConnection);
            alreadyConnectedPorts.push_back(port);
            alreadyConnectedPorts.push_back(peerPort);
        }
    }

    return OK;
}

/**
 * Find distinct stream ids from the graph and return them in a vector.
 * \param streamIds Vector to be populated with stream ids.
 */
status_t GraphConfig::graphGetStreamIds(StreamsVector &streamIds)
{
    GraphConfigNode *result;
    int32_t streamId = -1;
    css_err_t ret;

    GraphConfigNode::const_iterator it = mSettings->begin();
    while (it != mSettings->end()) {
        bool found = false;
        // Find all program groups
        ret = mSettings->getDescendant(GCSS_KEY_TYPE, "hw",
                                       it, &result);
        if (ret != css_err_none)
            continue;

        ret = result->getValue(GCSS_KEY_STREAM_ID, streamId);
        if (ret != css_err_none)
            continue;

        // If stream id is not yet in vector, add it
        StreamsVector::iterator ite = streamIds.begin();
        for(; ite !=streamIds.end(); ++ite) {
            if (streamId == *ite) {
                found = true;
                break;
            }
        }
        if (found)
            continue;

        streamIds.push_back(streamId);
    }

    if (streamIds.empty()) {
        LOGE("Failed to find any streamIds %d)", streamId);
        return UNKNOWN_ERROR;
    }
    return OK;
}

/**
 * Retrieve the stream id associated with a given sink.
 * The stream id represents the branch of the PSYS processing nodes that
 * precedes this sink.
 * This id is used for IQ tunning purposes.
 *
 * \param[in] sink sink node to query
 * \return stream id or -1 in case of error
 */
int32_t GraphConfig::sinkGetStreamId(Node *sink)
{
    css_err_t ret = css_err_none;
    int32_t streamId = -1;
    string type;

    if (CC_UNLIKELY(sink == nullptr)) {
        LOGE("Invalid Node, cannot get the sink stream id");
        return -1;
    }

    ret = sink->getValue(GCSS_KEY_TYPE, type);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get Node Type");
        return -1;
    }
    if (CC_UNLIKELY(type != "sink")) {
        LOGE("Node is not a sink");
        return -1;
    }
    ret = sink->getValue(GCSS_KEY_STREAM_ID, streamId);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get stream ID");
        return -1;
    }
    return streamId;
}

int32_t GraphConfig::portGetStreamId(Node *port)
{
    css_err_t ret = css_err_none;
    GraphConfig::Node *ancestor = nullptr;
    int32_t streamId = -1;

    if (CC_UNLIKELY(port == nullptr)) {
        LOGE("Invalid Node, cannot get the port stream id");
        return -1;
    }
    ret = port->getAncestor(&ancestor);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get port's ancestor");
        return -1;
    }

    ret = ancestor->getValue(GCSS_KEY_STREAM_ID, streamId);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get stream ID %s", NODE_NAME(ancestor));
        return -1;
    }
    return streamId;
}


/**
 * Retrieve a list of program groups that belong to a  given stream id.
 * Iterates through the graph configuration storing the program groups
 * that match this stream id into the provided vector.
 *
 * \param[in] streamId Id of the stream to match.
 * \param[out] programGroups Vector with the nodes that match the criteria.
 */
status_t GraphConfig::streamGetProgramGroups(int32_t streamId,
                                             NodesPtrVector &programGroups)
{
    css_err_t ret = css_err_none;
    GraphConfigNode *result;
    NodesPtrVector allProgramGroups;
    int32_t streamIdFound = -1;

    GraphConfigNode::const_iterator it = mSettings->begin();

    while (it != mSettings->end()) {

        ret = mSettings->getDescendant(GCSS_KEY_TYPE, "hw",
                                       it, &result);
        if (ret == css_err_none)
            allProgramGroups.push_back(result);
    }

    if (allProgramGroups.empty()) {
        LOGE("Failed to find any HW's for stream id %d"
             " BUG(check graph config file)", streamId);
        return UNKNOWN_ERROR;
    }

    for (size_t i = 0; i < allProgramGroups.size(); i++) {
        ret = allProgramGroups[i]->getValue(GCSS_KEY_STREAM_ID, streamIdFound);
        if ((ret == css_err_none) && (streamIdFound == streamId)) {
            programGroups.push_back(allProgramGroups[i]);
        }
    }

    return OK;
}

status_t GraphConfig::streamGetInputPort(int32_t streamId, Node **port)
{
    css_err_t ret = css_err_none;
    Node *result = nullptr;
    Node *pgNode = nullptr;
    int32_t streamIdFound = -1;
    *port = nullptr;
    Node::const_iterator it = mSettings->begin();

    while (it != mSettings->end()) {

        ret = mSettings->getDescendant(GCSS_KEY_TYPE, "hw",
                                       it, &pgNode);
        if (ret != css_err_none)
            continue;

        ret = pgNode->getValue(GCSS_KEY_STREAM_ID, streamIdFound);
        if ((ret == css_err_none) && (streamIdFound == streamId)) {

            Node::const_iterator it = pgNode->begin();
            while (it != pgNode->end()) {
                ret = pgNode->getDescendant(GCSS_KEY_TYPE, "port",
                                                it, &result);
                if (ret != css_err_none)
                    continue;
                int32_t direction = portGetDirection(result);
                if (direction == PORT_DIRECTION_INPUT) {
                    /*
                     * TODO: add check that the port is at the edge of the stream
                     * this could be done checking that the peer's port is in a PG
                     * that belongs to a different stream id or to a virtual sink
                     *
                     */
                     *port = result;
                     return OK;
                }
            }
        }
    }
    return (*port == nullptr) ? BAD_VALUE : OK;
}

/**
 *
 * Traverse the graph settings to find program groups that belong to
 * the given stream id.
 * collect the output ports of those program groups whose peer has a different
 * stream ID.
 * It also stores the UID of the peer port of each output port.
 * This is useful to detect wither the peer is active or not.
 *
 * \param [in] streamId  The stream id we want to find the output ports from.
 * \param [out] outputPorts Vector of pointers to Nodes with the ports.
 * \param [out] peerPorts  Vector of pointers to Nodes with the peer ports.
 */
status_t
GraphConfig::streamGetConnectedOutputPorts(int32_t streamId,
                                           NodesPtrVector &outputPorts,
                                           NodesPtrVector &peerPorts)
{
    css_err_t ret = css_err_none;
    status_t status = OK;
    Node *pgNode = nullptr;
    Node *port;
    Node *peer = nullptr;
    int32_t streamIdFound = -1;
    int32_t peerStreamId = 0;
    outputPorts.clear();
    peerPorts.clear();

    Node::const_iterator it = mSettings->begin();

    while (it != mSettings->end()) {
        ret = mSettings->getDescendant(GCSS_KEY_TYPE, "program_group",
                                       it, &pgNode);
        if (ret != css_err_none)
            continue;
        ret = pgNode->getValue(GCSS_KEY_STREAM_ID, streamIdFound);
        if ((ret == css_err_none) && (streamIdFound == streamId)) {

            Node::const_iterator it = pgNode->begin();

            while (it != pgNode->end()) {
                ret = pgNode->getDescendant(GCSS_KEY_TYPE, "port", it, &port);
                if (ret != css_err_none)
                    continue;

                int32_t direction = portGetDirection(port);

                if (direction == PORT_DIRECTION_OUTPUT) {
                    status = portGetPeer(port, &peer);
                    if (status == INVALID_OPERATION)
                        continue; // disabled terminal
                    if (status == OK) {
                        peerStreamId = portGetStreamId(peer);
                        if (peerStreamId != streamId) {
                            outputPorts.push_back(port);
                            peerPorts.push_back(peer);
                        }
                    }
                }
            }
        }
    }
    if (outputPorts.empty())
        LOGW("No outputports for stream %d", streamId);
    return OK;
}

/**
 * Retrieve the graph config node of the port that is connected to a given port.
 *
 * \param[in] port Node with the info of the port that we want to find its peer.
 * \param[out] peer Pointer to a node where the peer node reference will be
 *                  stored
 * \return OK
 * \return INVALID_OPERATION if the port is disabled.
 * \return BAD_VALUE if any of the graph settings is incorrect.
 */
status_t GraphConfig::portGetPeer(Node *port, Node **peer)
{

    css_err_t ret = css_err_none;
    int32_t enabled = 1;
    string peerName;

    if (CC_UNLIKELY(port == nullptr || peer == nullptr)) {
       LOGE("Invalid Node, cannot get the peer port");
       return BAD_VALUE;
    }
    ret = port->getValue(GCSS_KEY_ENABLED, enabled);
    if (ret == css_err_none && !enabled) {
        LOG1("This port is disabled, keep on getting the connection");
        return INVALID_OPERATION;
    }
    ret = port->getValue(GCSS_KEY_PEER, peerName);
    if (ret != css_err_none) {
        LOGE("Error getting peer attribute");
        return BAD_VALUE;
    }
    ret = mSettings->getDescendantByString(peerName, peer);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to find peer by name %s", peerName.c_str());
        return BAD_VALUE;
    }
    return OK;
}

/**
 * Generate the connection configuration information for a given port.
 *
 * This connection configuration  information is required by CIPF to build
 * the pipeline
 *
 * \param[in] port Pointer to the port node
 * \param[out] connectionInfo Reference to the connection info object
 * \param[out] peerPort Reference to the peer port
 * \return OK in case of success
 * \return BAD_VALUE in case of error while retrieving the information.
 * \return INVALID_OPERATION in case of the port being disabled.
 */
status_t GraphConfig::portGetConnection(Node *port,
                                        ConnectionConfig &connectionInfo,
                                        Node **peerPort)
{
    status_t status = OK;
    css_err_t ret = css_err_none;
    int32_t direction;

    status = portGetPeer(port, peerPort);
    if (CC_UNLIKELY(status != OK)) {
        if (status == INVALID_OPERATION) {
            LOGE("Port %s disabled, cannot get the connection",
                    getNodeName(port).c_str());
        } else {
            LOGE("Failed to get the peer port for port %s",getNodeName(port).c_str());
        }
        return status;
    }

    ret = port->getValue(GCSS_KEY_DIRECTION, direction);
    if (CC_UNLIKELY(ret != css_err_none)) {
       LOGE("Failed to get port direction");
       return BAD_VALUE;
    }

    /*
     * Iterations are not used
     */
    connectionInfo.mSinkIteration = 0;
    connectionInfo.mSourceIteration = 0;

    if (direction == PORT_DIRECTION_INPUT) {
        // input port is the sink in a connection
        status = portGetFourCCInfo(*port,
                                    connectionInfo.mSinkStage,
                                    connectionInfo.mSinkTerminal);
        if (CC_UNLIKELY(status != OK)) {
            LOGE("Failed to create fourcc info for sink port");
            return BAD_VALUE;
        }
        if (*peerPort != nullptr && !portIsVirtual(*peerPort)) {
            status = portGetFourCCInfo(**peerPort,
                                       connectionInfo.mSourceStage,
                                       connectionInfo.mSourceTerminal);
            if (CC_UNLIKELY(status != OK)) {
                LOGE("Failed to create fourcc info for source port");
                return BAD_VALUE;
            }
        } else {
            connectionInfo.mSourceStage = 0;
            connectionInfo.mSourceTerminal = 0;
        }
    } else {
        //output port is the source in a connection
        status = portGetFourCCInfo(*port,
                                    connectionInfo.mSourceStage,
                                    connectionInfo.mSourceTerminal);
        if (CC_UNLIKELY(status != OK)) {
            LOGE("Failed to create fourcc info for source port");
            return BAD_VALUE;
        }

        if (*peerPort != nullptr && !portIsVirtual(*peerPort)) {
            status = portGetFourCCInfo(**peerPort,
                                    connectionInfo.mSinkStage,
                                    connectionInfo.mSinkTerminal);
            if (CC_UNLIKELY(status != OK)) {
                LOGE("Failed to create fourcc info for sink port");
                return BAD_VALUE;
            }
        } else {
            connectionInfo.mSinkStage = 0;
            connectionInfo.mSinkTerminal = 0;
        }
    }

    return status;
}

/**
 * Retrieve the format information of a port
 * if the port doesn't have any format set, it gets the format from the peer
 * port (i.e. the port connected to this one)
 *
 * \param[in] port Port to query the format.
 * \param[out] format Format settings for this port.
 */
status_t GraphConfig::portGetFormat(Node *port,
                                    PortFormatSettings &format)
{
    GraphConfigNode *peerNode = nullptr; // The peer port node
    GraphConfigNode *tmpNode = port; // The port node node we are interrogating
    css_err_t ret = css_err_none;
    uint32_t stageId; // ignored

    if (CC_UNLIKELY(port == nullptr)) {
        LOGE("Invalid parameter, could not get port format");
        return BAD_VALUE;
    }

    ret = port->getValue(GCSS_KEY_ENABLED, format.enabled);
    if (ret != css_err_none) {
        // if not present by default is enabled
        format.enabled = 1;
    }

    status_t status = portGetFourCCInfo(*tmpNode, stageId, format.terminalId);
    if (CC_UNLIKELY(status != OK)) {
        LOGE("Could not get port uid");
        return INVALID_OPERATION;
    }

    // if disabled there is no need to query the format
    if (format.enabled == 0) {
        return OK;
    }

    format.width = 0;
    format.height = 0;

    ret = port->getValue(GCSS_KEY_WIDTH, format.width);
    if (ret != css_err_none) {
        /*
         * It could be the port configuration is not in settings, that is normal
         * it means that we need to ask the format from the peer.
         */
        ret = portGetPeer(port, &peerNode);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Could not find peer port - Fix your graph");
            return BAD_VALUE;
        }

        tmpNode = peerNode;

        ret = tmpNode->getValue(GCSS_KEY_WIDTH, format.width);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Could not find port format info: width (from peer)");
            return BAD_VALUE;
        }
    }

    ret = tmpNode->getValue(GCSS_KEY_HEIGHT, format.height);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Could not find port format info: height");
        return BAD_VALUE;
    }

    string fourccFormat;
    ret = tmpNode->getValue(GCSS_KEY_FORMAT, fourccFormat);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Could not find port format info: fourcc");
        return BAD_VALUE;
    }

    format.fourcc = get_fourcc(fourccFormat[0],fourccFormat[1],
                              fourccFormat[2],fourccFormat[3]);

    format.bpl = gcu::getBpl(format.fourcc, format.width);
    LOG1("bpl set to %d for %s", format.bpl, fourccFormat.c_str());

    // if settings are specifying bpl, owerwrite the calculated one
    int bplFromSettings = 0;
    ret = tmpNode->getValue(GCSS_KEY_BYTES_PER_LINE, bplFromSettings);
    if (CC_UNLIKELY(ret == css_err_none)) {
        LOG1("Overwriting bpl(%d) from settings %d", format.bpl, bplFromSettings);
        format.bpl = bplFromSettings;
    }

    format.bpp = gcu::getBppFromCommon(format.fourcc);

    return OK;
}

/**
 * Return the port direction
 *
 * \param[in] port Reference to port Graph node
 * \return 0 if it is an input port
 * \return 1 if it is an output port
 */
int32_t GraphConfig::portGetDirection(Node *port)
{
    int32_t direction = 0;
    css_err_t ret = css_err_none;
    ret = port->getValue(GCSS_KEY_DIRECTION, direction);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to retrieve port direction, default to input");
    }

    return direction;
}

/**
 * Return the port full name
 * The port full name is made out from:
 * - the name program group it belongs to
 * - the name of the port
 * separated by ":"
 *
 * \param[in] port Reference to port Graph node
 * \param[out] fullName reference to a string to store the full name
 *
 * \return OK if everything went fine.
 * \return BAD_VALUE if any of the graph queries failed.
 */
status_t GraphConfig::portGetFullName(Node *port, string &fullName)
{
    string portName, ancestorName;
    Node *ancestor;
    css_err_t ret = css_err_none;

    if (CC_UNLIKELY(port == nullptr)) {
        LOGE("Invalid parameter, could not get port full name");
        return BAD_VALUE;
    }

    ret = port->getAncestor(&ancestor);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to retrieve port ancestor");
        return BAD_VALUE;
    }
    ret = ancestor->getValue(GCSS_KEY_NAME, ancestorName);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get ancestor name for port");
        port->dumpNodeTree(port, 1);
        return BAD_VALUE;
    }

    ret = port->getValue(GCSS_KEY_NAME, portName);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to retrieve port name");
        return BAD_VALUE;
    }

    fullName = ancestorName + ":" + portName;
    return OK;
}

/**
 * Return true if the port is a virtual port, this is the end point
 * of the graph.
 * Virtual ports are the nodes of type sink.
 *
 * \param[in] port Reference to port Graph node
 * \return true if it is a virtual port
 * \return false if it is not a virtual port
 */
bool GraphConfig::portIsVirtual(Node *port)
{
    string type;
    css_err_t ret = css_err_none;
    ret = port->getValue(GCSS_KEY_TYPE, type);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to retrieve port type, default to input");
    }

    return (type == string("sink"));
}

/**
 * For a given port node it constructs the fourCC code used in the connection
 * object. This is constructed from the program group id.
 *
 * \param[in] portNode
 * \param[out] stageId Fourcc code that describes the PG where this node is
 *                     contained
 * \param[out] terminalID Fourcc code that describes the port, in FW jargon,
 *                        this is a PG terminal.
 * \return OK in case of no error
 * \return BAD_VALUE in case some error is found
 */
status_t GraphConfig::portGetFourCCInfo(Node &portNode,
                                        uint32_t &stageId, uint32_t &terminalId)
{
    Node *pgNode; // The Program group node
    css_err_t ret = css_err_none;
    int32_t portId;
    string type, subsystem;

    ret = portNode.getValue(GCSS_KEY_ID, portId);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get port's id");
        portNode.dumpNodeTree(&portNode, 1);
        return BAD_VALUE;
    }

    ret = portNode.getAncestor(&pgNode);
    if (CC_UNLIKELY(ret != css_err_none || pgNode == nullptr)) {
       LOGE("Failed to get port ancestor");
       return BAD_VALUE;
    }

    ret = pgNode->getValue(GCSS_KEY_TYPE, type);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get port's ancestor type ");
        pgNode->dumpNodeTree(pgNode, 1);
        return BAD_VALUE;
    }

    ret = pgNode->getValue(GCSS_KEY_SUBSYSTEM, subsystem);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get port's ancestor subsystem ");
        pgNode->dumpNodeTree(pgNode, 1);
        return BAD_VALUE;
    }

    if (type == string("hw")) {
        stageId = 0;
        terminalId = portId;
    }
    return OK;
}

/**
 * Return the the terminal id of the peer port.
 *
 * Given a name of a port in canonical format (i.e. isa:non_scaled_ouptut)
 * this method returns the terminal uid (the fourcc code) associated with its
 * peer port.
 *
 * \param[in] name Port name in canonical format
 * \param[out] terminalId Terminal id of the peer port
 *
 * \return BAD_VALUE in case name is empty
 * \return INVALID_OPERATION if the port or peer was not found in the graph
 * \return OK
 */
status_t GraphConfig::portGetPeerIdByName(string name,
                                          uid_t &terminalId)
{
    uid_t stageId; // not used
    css_err_t ret;
    status_t retErr;
    Node *portNode = nullptr;
    Node *peerNode = nullptr;

    if (name.empty())
        return BAD_VALUE;

    ret = mSettings->getDescendantByString(name, &portNode);
    if (CC_UNLIKELY(ret != css_err_none)) {
       LOGE("Failed to find port %s.", name.c_str());
       return INVALID_OPERATION;
    }

    retErr = portGetPeer(portNode, &peerNode);
    if (ret != OK || peerNode == nullptr) {
        LOGE("Failed to find peer for port %s.", name.c_str());
        return INVALID_OPERATION;
    }

    portGetFourCCInfo(*peerNode, stageId, terminalId);
    return OK;
}

/**
 * This method is used by pSysIsaTask to get the stream ids
 * which are used in setting, at the same time return the
 * mIsaOutputPort2StreamId
 * \param[out] isaStreamIdVector
 * \param[out] isaOutputPort2StreamIdMap
 */
status_t GraphConfig::getIsaStreamIds(vector<int32_t> &isaStreamIdVector,
                                      map<string, int32_t> &isaOutputPort2StreamIdMap)
{
    for (auto isaOutputPort : mIsaOutputPort2StreamId) {
         int32_t streamIdFound = isaOutputPort.second;
         // save the stream id into the vector
         unsigned int i = 0;
         for (; i < isaStreamIdVector.size(); i ++) {
              if (isaStreamIdVector[i] == streamIdFound) {
                  break;
              }
         }
         if (i == isaStreamIdVector.size())
             isaStreamIdVector.push_back(streamIdFound);
    }

    if (isaStreamIdVector.size() == 0) {
        LOGE("Fail to get stream id");
        return UNKNOWN_ERROR;
    }
    isaOutputPort2StreamIdMap = mIsaOutputPort2StreamId;
    return OK;
}

/**
 * retrieve the pointer to the client stream associated with a virtual sink
 *
 * I.e. access the mapping done at stream config time between the pointers
 * to camera3_stream_t and the names video0, video1, still0 etc...
 *
 * \param[in] port Node to the virtual sink (with name videoX or stillX etc..)
 * \param[out] stream Pointer to the client stream associated with that virtual
 *                    sink.
 * \return OK
 * \return BAD_VALUE in case of invalid parameters (null pointers)
 * \return INVALID_OPERATION in case the Node is not a virtual sink.
 */
status_t GraphConfig::portGetClientStream(Node *port, camera3_stream_t **stream)
{
    css_err_t ret = css_err_none;
    string portName;

    if (CC_UNLIKELY(!port || !stream)) {
        LOGE("Could not get client stream - bad parameters");
        return BAD_VALUE;
    }

    if (!portIsVirtual(port)) {
        LOGE("Trying to find the client stream from a non virtual port");
        return INVALID_OPERATION;
    }

    ret = port->getValue(GCSS_KEY_NAME, portName);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get name for port");
        port->dumpNodeTree(port, 1);
        return BAD_VALUE;
    }

    uid_t vPortId = GCSS::ItemUID::str2key(portName);
    *stream = mManager->getStreamByVirtualId(vPortId);

    return OK;
}

/**
 * A port is at the edge of the video stream (pipeline) if its peer is in a PG
 * that has a different streamID (a.k.a. pipeline id) or if its peer is a
 * virtual sink.
 *
 * Here we check for both conditions and return true if this port is at either
 * edge of a pipeline
 */
bool GraphConfig::isPipeEdgePort(Node *port)
{
    status_t status = OK;
    css_err_t ret = css_err_none;
    GraphConfig::Node *peer = nullptr;
    GraphConfig::Node *peerAncestor = nullptr;
    int32_t portDirection;
    int32_t streamId = -1;
    int32_t peerStreamId = -1;
    string peerType;

    portDirection = portGetDirection(port);

    status = portGetPeer(port, &peer);
    if (status == INVALID_OPERATION) {
        LOG1("port is disabled, so it is an edge port");
        return true;
    }
    if (CC_UNLIKELY(status != OK)) {
        LOGE("Failed to create fourcc info for source port");
        return false;
    }

    streamId = portGetStreamId(port);
    if (CC_UNLIKELY(streamId < 0))
        return false;
    /*
     * get the stream id of the peer port
     * we also check the ancestor for that. If the peer is a virtual sink then
     * it does not have ancestor.
     */
    if (!portIsVirtual(peer)) {
        ret = peer->getAncestor(&peerAncestor);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Failed to get peer's ancestor");
            return false;
        }
        ret = peerAncestor->getValue(GCSS_KEY_STREAM_ID, peerStreamId);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Failed to get stream ID of peer PG");
            return false;
        }
        /*
         * Retrieve the type of node the peer ancestor is. It could be is not a
         * program group node but a sink or hw block
         */
        peerAncestor->getValue(GCSS_KEY_TYPE, peerType);
    }

    if (portDirection == GraphConfig::PORT_DIRECTION_INPUT) {
        /*
         *  input port,
         *  if the peer is a source or hw block then it is on the edge,
         *  or if the peer is on a different stream id
         */
        if ((streamId != peerStreamId) || (peerType == string("hw"))) {
            return true;
        }
    } else {
        /*
         *  output port,
         *  if the peer is a virtual port, or has a different stream id
         *  then it is on the edge,
         */
        if (portIsVirtual(peer) || (streamId != peerStreamId)) {
            return true;
        }
    }
    return false;
}

/**
 * Parse the information of the sensor node in the graph and store it in the
 * provided SourceNodeInfo struct.
 *
 * \param[in] sensorNode  Pointer to the graph config node that represents
 *                        the sensor.
 * \param[out] info  Data structure where the information is stored.
 */
status_t GraphConfig::parseSensorNodeInfo(Node* sensorNode,
                                          SourceNodeInfo &info)
{
    status_t retErr = OK;
    css_err_t ret = css_err_none;
    string metadata;
    string tmp;

    ret = sensorNode->getValue(GCSS_KEY_CSI_PORT, info.csiPort);
    if (CC_UNLIKELY(ret  !=  css_err_none)) {
       LOGE("Error: Couldn't get csi port from the graph");
       return UNKNOWN_ERROR;
    }

    ret = sensorNode->getValue(GCSS_KEY_SENSOR_NAME, info.name);
    if (CC_UNLIKELY(ret  !=  css_err_none)) {
       LOGE("Error: Couldn't get sensor name from sensor");
       return UNKNOWN_ERROR;
    }

    ret = sensorNode->getValue(GCSS_KEY_LINK_FREQ, info.link_freq);
    if (CC_UNLIKELY(ret !=  css_err_none)) {
        info.link_freq = "0"; // default to zero
    }

    // Find i2c address for the sensor from sensor info
    const CameraHWInfo *camHWInfo = PlatformData::getCameraHWInfo();
    for (size_t i = 0; i < camHWInfo->mSensorInfo.size(); i++) {
        if (camHWInfo->mSensorInfo[i].mSensorName == info.name)
            info.i2cAddress = camHWInfo->mSensorInfo[i].mI2CAddress;
    }
    if (info.i2cAddress == "") {
        LOGE("Couldn't get i2c address from Platformdata");
        return UNKNOWN_ERROR;
    }

    ret = sensorNode->getValue(GCSS_KEY_METADATA, metadata);
    if (CC_UNLIKELY(ret  !=  css_err_none)) {
       LOGE("Error: Couldn't get metadata enabled from sensor");
       return UNKNOWN_ERROR;
    }
    info.metadataEnabled = atoi(metadata.c_str());

    ret = sensorNode->getValue(GCSS_KEY_MODE_ID, info.modeId);
    if (CC_UNLIKELY(ret  !=  css_err_none)) {
        LOGE("Error: Couldn't get sensor mode id from sensor");
        return UNKNOWN_ERROR;
    }

    ret = sensorNode->getValue(GCSS_KEY_BAYER_ORDER, info.nativeBayer);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Error: Couldn't get native bayer order from sensor");
        return UNKNOWN_ERROR;
    }

    retErr = getDimensions(sensorNode, info.output.w, info.output.h);
    if (retErr != OK) {
        LOGE("Error: Couldn't get values from sensor");
        return UNKNOWN_ERROR;
    }

    ret = sensorNode->getValue(GCSS_KEY_INTERLACED, tmp);
    if (ret != css_err_none) {
        LOGW("Couldn't get interlaced field from sensor");
    } else {
        info.interlaced = atoi(tmp.c_str());
    }

    // v-flip is not mandatory. Some sensors may not have this control
    ret = sensorNode->getValue(GCSS_KEY_VFLIP, info.verticalFlip);

    // h-flip is not mandatory. Some sensors may not have this control
    ret = sensorNode->getValue(GCSS_KEY_HFLIP, info.horizontalFlip);


    Node *port0Node = nullptr;
    ret = sensorNode->getDescendant(GCSS_KEY_PORT_0, &port0Node);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get port_0");
        return UNKNOWN_ERROR;
    }
    ret = port0Node->getValue(GCSS_KEY_FORMAT, tmp);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get format from the graph");
        return UNKNOWN_ERROR;
    }
    /* find mbus format from common format in settings */
    info.output.mbusFormat = gcu::getMBusFormat(get_fourcc(tmp[0], tmp[1],
                                                          tmp[2], tmp[3]));

    /*
     * Get size and cropping from pixel array to use in format and selection
     */
    Node *pixelArrayOutput = nullptr;
    ret = sensorNode->getDescendantByString("pixel_array:output",
                                            &pixelArrayOutput);
    if (CC_UNLIKELY(ret  !=  css_err_none)) {
        LOGE("Error: Couldn't get pixel array node from the graph");
        return UNKNOWN_ERROR;
    }

    retErr = getDimensions(pixelArrayOutput, info.pa.out.w,
                                             info.pa.out.h,
                                             info.pa.out.l,
                                             info.pa.out.t);
    if (retErr != OK) {
        LOGE("Error: Couldn't get values from pixel array output");
        return UNKNOWN_ERROR;
    }

    info.pa.name = info.name+ " " + info.i2cAddress;
    /* Populate the formats for each subdevice
     * The format for the Pixel Array is determined by the native bayer order
     * and the bpp selected by the settings.
     * We extract the bpp from the format in the sensor port.
     *
     * The format in the sensor output port may be different that the
     * pixel array format because the sensor may be changing the effective
     * bayer order by flipping or internal cropping
     * */
    int32_t bpp = gcu::getBpp(info.output.mbusFormat);
    info.pa.out.mbusFormat = gcu::getMBusFormat(info.nativeBayer, bpp);
    return OK;
}

/*
 * Create mediaCtlConfig structure to configure sensor mode. All settings are
 * retrieved from graph config settings and applied one by one.
 *
 * TODO a lot of string values because of android gcss keys, which does not
 * have support for ints. Some could be moved to gcss_keys
 *
 * \param[out] mediaCtlConfig  the struct to populate
 * \return OK              at success
 * \return UNKNOWN ERROR   at failure
 */

status_t GraphConfig::getMediaCtlData(MediaCtlConfig *mediaCtlConfig)
{
    CheckAndLogError((!mediaCtlConfig), BAD_VALUE, "@%s null ptr\n",
                     __FUNCTION__);

    ConfigProperties cameraProps;      // camera properties
    css_err_t ret = css_err_none;
    status_t retErr = OK;
    string formatStr;
    SourceNodeInfo sourceInfo;
    Node *sourceNode = nullptr;

    string csi2;
    if (mSourceType == SRC_SENSOR) {
        ret = mSettings->getDescendant(GCSS_KEY_SENSOR, &sourceNode);
        if (ret != css_err_none) {
            LOGE("Error: Couldn't get sensor node from the graph");
            return UNKNOWN_ERROR;
        }
        retErr = parseSensorNodeInfo(sourceNode, sourceInfo);
        if (retErr != OK) {
            LOGE("Error: Couldn't get sensor node info");
            return UNKNOWN_ERROR;
        }

        // get the next entity port of the "ov5670 binner 11-0010" which is dynamical.
        // it could "rkisp1-csi2 0" or "rkisp1-csi2 1", then it will get 0 or 1.
        int port = 0;
        std::shared_ptr<MediaEntity> entity = nullptr;
        string entityName = sourceInfo.name + " " + sourceInfo.i2cAddress;
        LOG1("entityName:%s\n", entityName.c_str());
        status_t ret = mMediaCtl->getMediaEntity(entity, entityName.c_str());
        if (ret != NO_ERROR) {
            LOGE("@%s, fail to call getMediaEntity, ret:%d\n", __FUNCTION__, ret);
            return UNKNOWN_ERROR;
        }
        std::vector<media_link_desc> links;
        entity->getLinkDesc(links);
        LOG1("@%s, links number:%zu\n", __FUNCTION__, links.size());
        if (links.size()) {
            struct media_pad_desc* pad = &links[0].sink;
            LOG1("@%s, sink entity:%d, flags:%d, index:%d\n",
                __FUNCTION__, pad->entity, pad->flags, pad->index);
            struct media_entity_desc entityDesc;
            mMediaCtl->findMediaEntityById(pad->entity, entityDesc);
            LOG1("@%s, name:%s\n", __FUNCTION__, entityDesc.name);
            string name = entityDesc.name;
            std::size_t p = name.find(" ");
            if (p != std::string::npos) {
                string s;
                s.append(name, p + 1, 1);
                port = std::stoi(s);
                LOG1("@%s, port:%d\n", __FUNCTION__, port);
            }
        }

        // get csi2 and cio2 names
        //csi2 = csi2_without_port + std::to_string(port);
        //mCSIBE = CSI_BE + std::to_string(port);
        csi2 = csi2_without_port;
        mCSIBE = CSI_BE;
        /* TODO: should config according to real topology, sensor to isp link is decided by the sensor interface,
        * link is different for mipi and dvp.
        *  mipi sensor --- > csi ---> isp
        * dvp sensor ----> isp
        */
        addLinkParams(entityName, 0, csi2, 0, 1, MEDIA_LNK_FL_ENABLED, mediaCtlConfig);
        LOG1(" csi2 is:%s, cio2 is:%s\n", csi2.c_str(), mCSIBE.c_str());
    } else {
        LOGE("Error: No source");
        return UNKNOWN_ERROR;
    }

#if 0//needn't get the default expsure here, get it from 3a lib instead
    // Add control params
    retErr = addControls(sourceNode, sourceInfo, mediaCtlConfig);
    if (retErr != OK) {
        return UNKNOWN_ERROR;
    }
#endif
    /*
     * Add Camera properties to mediaCtlConfig
     */
    int id = 0;
    ret = sourceNode->getValue(GCSS_KEY_ID, id);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get sensor id from sensor");
        return UNKNOWN_ERROR;
    }
    std::string cameraName = sourceInfo.name + " " + sourceInfo.modeId;
    cameraProps.outputWidth = sourceInfo.output.w;
    cameraProps.outputHeight = sourceInfo.output.h;
    cameraProps.id = id;
    cameraProps.name = cameraName.c_str();
    mediaCtlConfig->mCameraProps = cameraProps;

    mediaCtlConfig->mFTCSize.Width = sourceInfo.output.w;
    mediaCtlConfig->mFTCSize.Height = sourceInfo.output.h;

    Node *pixelFormatterIn = nullptr, *pixelFormatterOut = nullptr;
    Node *csiBEOutput = nullptr, *csiBESocOutput = nullptr;
    int pfInW = 0, pfInH = 0, pfOutW = 0, pfOutH = 0, pfLeft = 0, pfTop = 0;
    bool pfPresent = false;

    // Get csi_be node. If not found, try csi_be_soc. If not found return error.
    ret = mSettings->getDescendantByString("csi_be:output", &csiBEOutput);
    if (ret != css_err_none) {

        ret = mSettings->getDescendantByString("csi_be_soc:output", &csiBESocOutput);
        if (ret != css_err_none) {
            LOGE("Error: Couldn't get csi_be or csi_be_soc nodes from the graph");
            return UNKNOWN_ERROR;
        }
        // get format from _soc
        ret = csiBESocOutput->getValue(GCSS_KEY_FORMAT, formatStr);
        if (ret != css_err_none) {
            LOGE("Error: Couldn't get format from the graph");
            return UNKNOWN_ERROR;
        }
    } else {
        ret = csiBEOutput->getValue(GCSS_KEY_FORMAT, formatStr);
        if (ret != css_err_none) {
            LOGE("Error: Couldn't get format from the graph");
            return UNKNOWN_ERROR;
        }
    }

    /* consistency check, we have at least one CSI-BE */
    if (CC_UNLIKELY(csiBESocOutput == nullptr && csiBEOutput == nullptr)) {
        LOGE("Error: CSI BE Output nullptr");
        return UNKNOWN_ERROR;
    }

    std::string pixelFormatterInput = "bxt_pixelformatter:input";
    std::string pixelFormatterOutput = "bxt_pixelformatter:output";
    std::string inputPort;
    std::string outputPort;
    if (csiBEOutput != nullptr) {
        inputPort = "csi_be:" + pixelFormatterInput;
        outputPort = "csi_be:" + pixelFormatterOutput;
    } else {
        inputPort = "csi_be_soc:" + pixelFormatterInput;
        outputPort = "csi_be_soc:" + pixelFormatterOutput;
    }

    /* Get cropping values from the pixel formatter input. Output resolution
     * comes from the csi be output. Some graphs may not use pixel formatter.*/
    ret = mSettings->getDescendantByString(inputPort,
                                            &pixelFormatterIn);
    if (ret != css_err_none) {
        LOGW("Couldn't get pixel formatter input, skipping");
    } else {
        pfPresent = true;
        ret = mSettings->getDescendantByString(outputPort,
                                                &pixelFormatterOut);
        if (ret != css_err_none) {
            LOGE("Error: Couldn't get pixel formatter output");
            return UNKNOWN_ERROR;
        }

        retErr = getDimensions(pixelFormatterIn, pfInW, pfInH, pfLeft, pfTop);
        if (retErr != OK) {
            LOGE("Error: Couldn't get values from pixel formatter input");
            return UNKNOWN_ERROR;
        }

        retErr = getDimensions(pixelFormatterOut, pfOutW, pfOutH);
        if (retErr != OK) {
            LOGE("Error: Couldn't get values from pixel formatter output");
            return UNKNOWN_ERROR;
        }
    }

    int csiBEOutW = 0, csiBEOutH = 0;
    int csiBESocOutW = 0, csiBESocOutH = 0;
    if (csiBEOutput != nullptr) {
        retErr = getDimensions(csiBEOutput, csiBEOutW,csiBEOutH);
        if (retErr != OK) {
            LOGE("Error: Couldn't values from csi be output");
            return UNKNOWN_ERROR;
        }
    } else {
        retErr = getDimensions(csiBESocOutput, csiBESocOutW, csiBESocOutH);
        if (retErr != OK) {
            LOGE("Error: Couldn't get values from csi be soc out");
            return UNKNOWN_ERROR;
        }
        LOG1("pfInW:%d, pfLeft:%d, pfTop:%d,pfOutW:%d,pfOutH:%d,csiBESocOutW:%d,csiBESocOutH:%d",
              pfInW, pfLeft, pfTop, pfOutW, pfOutH, csiBESocOutW,csiBESocOutH);
    }

    /* Boolean to tell whether there is pixel formatter cropping.
     * This affects which selections are made */
    bool pixelFormatter = false;
    if (pfInW != pfOutW || pfInH != pfOutH || pfLeft != 0 || pfTop != 0)
        pixelFormatter = true;

    /*
     * If CSI BE SOC is not used, we must have ISA. Get video crop, scaled and
     * non scaled output from ISA and apply the formats. Otherwise add formats
     * for CSI BE SOC.
     */
    Node *isaNode = nullptr;
    Node *cropVideoIn = nullptr, *cropVideoOut = nullptr;
    int videoCropW = 0, videoCropH = 0, videoCropT = 0, videoCropL = 0;
    int videoCropOutW = 0, videoCropOutH = 0;

    /*
     * First get and set values when CSI BE SOC is not used
     */
    if (csiBESocOutput == nullptr) {
        ret = mSettings->getDescendant(GCSS_KEY_CSI_BE, &isaNode);
        if (ret != css_err_none) {
            LOGE("Error: Couldn't get isa node");
            return UNKNOWN_ERROR;
        }

        // Check if there is video cropping available. It is zero as default.
        ret = isaNode->getDescendantByString("csi_be:output",
                                             &cropVideoOut);
        if (ret == css_err_none) {
            ret = isaNode->getDescendantByString("csi_be:input",
                                                 &cropVideoIn);
        }
        if (ret == css_err_none) {
            retErr = getDimensions(cropVideoIn, videoCropW, videoCropH,
                    videoCropL, videoCropT);
            if (retErr != OK) {
                LOGE("Error: Couldn't get values from crop video input");
                return UNKNOWN_ERROR;
            }
            retErr = getDimensions(cropVideoOut, videoCropOutW, videoCropOutH);
            if (retErr != OK) {
                LOGE("Error: Couldn't get values from crop video output");
                return UNKNOWN_ERROR;
            }
        }
    }

    /* Set sensor pixel array parameter to the attributes in 'sensor_mode' node,
     * ignore the attributes in pixel_array and binner node due to upstream driver
     * removed binner and scaler subdev.
     */
    addFormatParams(sourceInfo.pa.name, sourceInfo.output.w, sourceInfo.output.h,
                    0, sourceInfo.output.mbusFormat, 0, 0, mediaCtlConfig);

    // rkisp1-csi2 0 or 1
    addFormatParams(csi2, csiBESocOutW, csiBESocOutH,
                    0, sourceInfo.output.mbusFormat, 0, 0, mediaCtlConfig);
    addFormatParams(csi2, csiBESocOutW, csiBESocOutH,
                    1, sourceInfo.output.mbusFormat, 0, 0, mediaCtlConfig);

    /* Start populating selections into mediaCtlConfig
     * entity name, width, height, left crop, top crop, target, pad, config */
    addSelectionParams(sourceInfo.pa.name,
                       sourceInfo.pa.out.w,
                       sourceInfo.pa.out.h,
                       sourceInfo.pa.out.l,
                       sourceInfo.pa.out.t,
                       V4L2_SEL_TGT_CROP, 0 /* sink pad */, mediaCtlConfig);

    if (gDumpType & CAMERA_DUMP_MEDIA_CTL)
        dumpMediaCtlConfig(*mediaCtlConfig);

    return OK;
}

status_t GraphConfig::getNodeInfo(const ia_uid uid, const Node &parent, int* width, int* height)
{
    status_t status = OK;

    Node *node = nullptr;
    status = parent.getDescendant(uid, &node);
    if (status != css_err_none) {
        LOGE("pipe log <%s> node is not present in graph (descriptor or settings) - continuing.",GCSS::ItemUID::key2str(uid));
        return UNKNOWN_ERROR;
    }
    status = node->getValue(GCSS_KEY_WIDTH, *width);
    if (status != css_err_none) {
        LOGE("pipe log Could not get width for <%s>", NODE_NAME(node));
        return UNKNOWN_ERROR;
    }

    if (width == 0) {
        LOGE("pipe log Could not get width for <%s>", NODE_NAME(node));
        return UNKNOWN_ERROR;
    }

    status = node->getValue(GCSS_KEY_HEIGHT, *height);
    if (status != css_err_none) {
        LOGE("pipe log Could not get height for <%s>", NODE_NAME(node));
        return UNKNOWN_ERROR;
    }

    if (height == 0) {
        LOGE("pipe log Could not get height for <%s>", NODE_NAME(node));
        return UNKNOWN_ERROR;
    }

    return status;
}


/*
 * Imgu specific function
 */
status_t GraphConfig::getImguMediaCtlData(int32_t cameraId,
                                          int32_t testPatternMode,
                                          MediaCtlConfig *mediaCtlConfig,
                                          MediaCtlConfig *mediaCtlConfigVideo,
                                          MediaCtlConfig *mediaCtlConfigStill)
{
    CheckAndLogError(
        (!mediaCtlConfig || !mediaCtlConfigVideo || !mediaCtlConfigStill),
        BAD_VALUE, "@%s null ptr\n", __FUNCTION__);

    int ret;

    Node *imgu = nullptr;
    Node *preview = nullptr, *video = nullptr, *still = nullptr, *output = nullptr, *input = nullptr;
    int width = 0, height = 0, format = 0;
    int enabled = 1;
    std::string kImguName = "rkisp1-isp-subdev";

    ret = mSettings->getDescendant(GCSS_KEY_IMGU, &imgu);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get imgu node");
        return UNKNOWN_ERROR;
    }

    // TODO: not done
    mSettings->getDescendant(GCSS_KEY_IMGU_PREVIEW, &preview);
    mSettings->getDescendant(GCSS_KEY_IMGU_VIDEO, &video);
    mSettings->getDescendant(GCSS_KEY_IMGU_STILL, &still);
    imgu->getDescendant(GCSS_KEY_OUTPUT, &output);
    imgu->getDescendant(GCSS_KEY_INPUT, &input);

    struct lut {
        uint32_t uid;
        string name;
        int ipuNodeName;
        Node *pipe;
        int pad;
    };

    vector<lut> uids;
    uids.clear();

    uids = {
        { GCSS_KEY_IMGU_STILL, MEDIACTL_STILLNAME, IMGU_NODE_STILL, still, -1 },
        { GCSS_KEY_INPUT, kImguName, IMGU_NODE_INPUT,input, 0 },
        { GCSS_KEY_OUTPUT, kImguName, -1, output, MEDIACTL_PAD_OUTPUT_NUM },
        { GCSS_KEY_IMGU_VIDEO, MEDIACTL_VIDEONAME, IMGU_NODE_VIDEO, video, 0 },
        { GCSS_KEY_IMGU_PREVIEW, MEDIACTL_PREVIEWNAME, IMGU_NODE_VF_PREVIEW, preview, 0 },
    };

    int ispOutWidth = 0, ispOutHeight = 0;
    for (int i = 0; i < uids.size(); i++) {
        string name = uids[i].name;
        Node *pipe = uids[i].pipe;

        if (pipe == nullptr) {
            LOGD("<%u> node is not present in graph (descriptor or settings) - continuing.", uids[i].uid);
            continue;
        }

        // Assume enabled="1" by default. Explicitly set to 0 in settings, if necessary.
        enabled = 1;
        ret = pipe->getValue(GCSS_KEY_ENABLED, enabled);
        if (ret != css_err_none) {
            LOG1("Attribute 'enabled' not present in <%s>. Assuming enabled=\"1\"", NODE_NAME(pipe));
        }

        if (!enabled) {
            LOG1("Node <%s> not enabled - continuing", NODE_NAME(pipe));
            continue;
        }

        ret = pipe->getValue(GCSS_KEY_WIDTH, width);
        if (ret != css_err_none) {
            LOGE("Could not get width for <%s>", NODE_NAME(pipe));
            return UNKNOWN_ERROR;
        }

        if (width == 0)
            continue;

        ret = pipe->getValue(GCSS_KEY_HEIGHT, height);
        if (ret != css_err_none) {
            LOGE("Could not get height for <%s>", NODE_NAME(pipe));
            return UNKNOWN_ERROR;
        }
        string fourccFormat = "";
        ret = pipe->getValue(GCSS_KEY_FORMAT, fourccFormat);
        if (ret != css_err_none) {
            LOGE("Could not get format for <%s>", NODE_NAME(pipe));
            return UNKNOWN_ERROR;
        }

        format = gcu::getV4L2Format(get_fourcc(fourccFormat[0],
                                               fourccFormat[1],
                                               fourccFormat[2],
                                               fourccFormat[3]));

        if (GCSS::ItemUID::key2str(uids[i].uid) == GC_PREVIEW ||
            GCSS::ItemUID::key2str(uids[i].uid) == GC_STILL ||
            GCSS::ItemUID::key2str(uids[i].uid) == GC_VIDEO) {
            int nodeWidth = 0, nodeHeight = 0;

            /* Use BGGR as bayer format when specific sensor receives test pattern request */
            if (testPatternMode != ANDROID_SENSOR_TEST_PATTERN_MODE_OFF) {
                const RKISP1CameraCapInfo* capInfo = getRKISP1CameraCapInfo(cameraId);
                CheckAndLogError(capInfo == nullptr, UNKNOWN_ERROR,
                                 "@%s: failed to get cameraCapInfo",
                                 __FUNCTION__);
                if (name.compare(MEDIACTL_INPUTNAME) == 0 &&
                        !capInfo->getTestPatternBayerFormat().empty()) {
                    format = gcu::getV4L2Format(capInfo->getTestPatternBayerFormat());
                }
            }
            addFormatParams(name, width, height, uids[i].pad,
                            format, 0, 0, mediaCtlConfig);

            // Get path crop  info
            ret = getNodeInfo(GCSS_KEY_IMGU_PCRP, *pipe, &nodeWidth, &nodeHeight);
            if (ret != OK) {
                LOGE("pipe log name: %s can't get info!", name.c_str());
                return UNKNOWN_ERROR;
            } else {
                struct v4l2_selection select;
                CLEAR(select);
                /* videodev2.h says don't use *_MPLAEN */
                select.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                select.target = V4L2_SEL_TGT_CROP;
                select.flags = 0;
                select.r.left = (ispOutWidth - nodeWidth) / 2;
                select.r.top = (ispOutHeight - nodeHeight) / 2;
                select.r.width = nodeWidth;
                select.r.height = nodeHeight;
                addSelectionVideoParams(name.c_str(), select, mediaCtlConfig);
                LOG2("pipe log name: %s  crop size %dx%d", name.c_str(), nodeWidth, nodeHeight);
            }

            LOG2("Adding video node: %s", NODE_NAME(pipe));
            addImguVideoNode(uids[i].ipuNodeName, name, mediaCtlConfig);
            addLinkParams(kImguName, MEDIACTL_PAD_OUTPUT_NUM, name, 0, 1, MEDIA_LNK_FL_ENABLED, mediaCtlConfig);
        } else if (GCSS::ItemUID::key2str(uids[i].uid) == GC_INPUT) {
            int nodeWidth = 0, nodeHeight = 0;
            int32_t iMbusFormat = gcu::getMBusFormat(get_fourcc(fourccFormat[0],
                                                                fourccFormat[1],
                                                                fourccFormat[2],
                                                                fourccFormat[3]));
            addFormatParams(name, width, height, uids[i].pad,
                            iMbusFormat, 0, 0, mediaCtlConfig);

            // Get isp input crop  info
            ret = getNodeInfo(GCSS_KEY_IMGU_IAC, *pipe, &nodeWidth, &nodeHeight);
            if (ret != OK) {
                LOGW("pipe log name: %s can't get info!", name.c_str());
                return UNKNOWN_ERROR;
            } else
                addSelectionParams(name, nodeWidth, nodeHeight, 0, 0, V4L2_SEL_TGT_CROP, uids[i].pad, mediaCtlConfig);
            /* TODO: related to sensor interface, see getMediaCtlData*/
            addLinkParams(mCSIBE, 1, kImguName, 0, 1, MEDIA_LNK_FL_ENABLED, mediaCtlConfig);
        } else if (GCSS::ItemUID::key2str(uids[i].uid) == GC_OUTPUT) {
            int nodeWidth = 0, nodeHeight = 0;
            int32_t oMbusFormat = gcu::getMBusFormat(get_fourcc(fourccFormat[0],
                                                                fourccFormat[1],
                                                                fourccFormat[2],
                                                                fourccFormat[3]));
            /* use limit range if test pattern mode is selected */
            int quantization = testPatternMode != ANDROID_SENSOR_TEST_PATTERN_MODE_OFF ?
                V4L2_QUANTIZATION_LIM_RANGE : V4L2_QUANTIZATION_DEFAULT;
            addFormatParams(name, width, height, uids[i].pad,
                            oMbusFormat, 0, quantization, mediaCtlConfig);
            // Get isp output crop  info
            ret = getNodeInfo(GCSS_KEY_IMGU_ISM, *pipe, &nodeWidth, &nodeHeight);
            if (ret != OK) {
                LOGW("pipe log name: %s can't get info!", name.c_str());
                return UNKNOWN_ERROR;
            } else
                addSelectionParams(name, nodeWidth, nodeHeight, 0, 0, V4L2_SEL_TGT_CROP, uids[i].pad, mediaCtlConfig);
            ispOutWidth = nodeWidth;
            ispOutHeight = nodeHeight;
        } else {
            LOGE("pipe log name: wrong node %s !", GCSS::ItemUID::key2str(uids[i].uid));
            return UNKNOWN_ERROR;
        }
    }

    LOG2("Adding stats node");
    addImguVideoNode(IMGU_NODE_STAT, MEDIACTL_STATNAME, mediaCtlConfig);
    addLinkParams(kImguName, 3, MEDIACTL_STATNAME, 0, 1, MEDIA_LNK_FL_ENABLED, mediaCtlConfig);

    LOG2("Adding parameter node");
    addImguVideoNode(IMGU_NODE_PARAM, MEDIACTL_PARAMETERNAME, mediaCtlConfig);
    addLinkParams(MEDIACTL_PARAMETERNAME, 0, kImguName, 1, 1, MEDIA_LNK_FL_ENABLED, mediaCtlConfig);
    return ret;
}

void GraphConfig::setMediaCtlConfig(std::shared_ptr<MediaController> mediaCtl,
                                    bool swapVideoPreview,
                                    bool enableStill)
{
    mMediaCtl = mediaCtl;
}

/*
 * Imgu helper function
 */
bool GraphConfig::doesNodeExist(string nodeName)
{
    int ret;

    Node *node = nullptr;

    ret = mSettings->getDescendantByString(nodeName, &node);
    if (ret != css_err_none) {
        LOGD("Node <%s> was not found.", nodeName.c_str());
        return false;
    }

    /* There is no good way to search if node exists or not.
     * Because mSettings has both descriptor and settings combined we
     * need to ask for specific value see if node really exists in the
     * settings side. */
    int width;
    ret = node->getValue(GCSS_KEY_WIDTH, width);
    if (ret != css_err_none) {
        LOGD("Node <%s> was not found.", nodeName.c_str());
        return false;
    }

    return true;
}


/*
 * Get values for MediaCtlConfig control params.
 * \Node sensorNode    pointer to sensor node
 */
status_t GraphConfig::addControls(const Node *sensorNode,
                                  const SourceNodeInfo &sourceInfo,
                                  MediaCtlConfig* config)
{
    css_err_t ret = css_err_none;
    std::string value;
    std::string entityName;

    if (!sourceInfo.pa.name.empty()) {
        entityName = sourceInfo.pa.name;
    } else if (!sourceInfo.tpg.name.empty()) {
        entityName = sourceInfo.tpg.name;
    } else {
        LOGE("Empty entity name");
        return UNKNOWN_ERROR;
    }

    ret = sensorNode->getValue(GCSS_KEY_EXPOSURE, value);
    if (ret == css_err_none) {
        addCtlParams(entityName, GCSS_KEY_EXPOSURE, V4L2_CID_EXPOSURE,
                     value, config);
    }

    ret = sensorNode->getValue(GCSS_KEY_GAIN, value);
    if (ret == css_err_none) {
        addCtlParams(entityName, GCSS_KEY_GAIN, V4L2_CID_ANALOGUE_GAIN,
                     value, config);
    }
    return OK;
}

/**
 * Add video nodes into mediaCtlConfig
 *
 * \param csiBESocOutput[in]  use to determine whether csi be soc is enabled
 * \param mediaCtlConfig[out] populate this struct with given values
 */
void GraphConfig::addVideoNodes(const Node* csiBESocOutput,
                                MediaCtlConfig* config)
{
    CheckAndLogError((!config), VOID_VALUE, "@%s null ptr\n", __FUNCTION__);

    MediaCtlElement mediaCtlElement;

    // Imgu support
    mediaCtlElement.isysNodeName = ISYS_NODE_RAW;
    mediaCtlElement.name = mCSIBE;
    config->mVideoNodes.push_back(mediaCtlElement);
}

void GraphConfig::addImguVideoNode(int nodeType, const string& nodeName, MediaCtlConfig *config)
{
    CheckAndLogError((!config), VOID_VALUE, "@%s null ptr\n", __FUNCTION__);
    CheckAndLogError((nodeType == IMGU_NODE_NULL), VOID_VALUE,
                     "@%s null ipu node name\n", __FUNCTION__);

    MediaCtlElement mediaCtlElement;
    mediaCtlElement.name = nodeName;
    mediaCtlElement.isysNodeName = nodeType;
    config->mVideoNodes.push_back(mediaCtlElement);
}

/*
 * Imgu helper function
 */
status_t GraphConfig::getValue(string &nodeName, uint32_t id, int &value)
{
    Node *node;
    int ret = mSettings->getDescendantByString(nodeName, &node);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get %s node", nodeName.c_str());
        return UNKNOWN_ERROR;
    }

    GCSS::GraphConfigAttribute *attr;
    ret = node->getAttribute(id, &attr);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get attribute '%s' for node: %s", GCSS::ItemUID::key2str(id), NODE_NAME(node));
        return UNKNOWN_ERROR;
    }
    string valueString = "-2";
    ret = attr->getValue(valueString);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get value of '%s' for node: %s", GCSS::ItemUID::key2str(id), NODE_NAME(node));
        return UNKNOWN_ERROR;
    }
    value = atoi(valueString.c_str());

    return OK;
}

/**
 * Dump contents of mediaCtlConfig struct
 */
void GraphConfig::dumpMediaCtlConfig(const MediaCtlConfig &config) const {

    size_t i = 0;
    LOGE("MediaCtl config w=%d ,height=%d"
        ,config.mCameraProps.outputWidth,
        config.mCameraProps.outputHeight);
    for (i = 0; i < config.mLinkParams.size() ; i++) {
       LOGE("Link Params srcName=%s  srcPad=%d ,sinkName=%s, sinkPad=%d enable=%d"
            ,config.mLinkParams[i].srcName.c_str(),
            config.mLinkParams[i].srcPad,
            config.mLinkParams[i].sinkName.c_str(),
            config.mLinkParams[i].sinkPad,
            config.mLinkParams[i].enable);
    }
    for (i = 0; i < config.mFormatParams.size() ; i++) {
       LOGE("Format Params entityName=%s  pad=%d ,width=%d, height=%d formatCode=%x"
            ,config.mFormatParams[i].entityName.c_str(),
            config.mFormatParams[i].pad,
            config.mFormatParams[i].width,
            config.mFormatParams[i].height,
            config.mFormatParams[i].formatCode);
    }
    for (i = 0; i < config.mSelectionVideoParams.size() ; i++) {
       LOGE("Selection video Params entityName=%s  type=%d ,target=%d, flag=%d"
            ,config.mSelectionVideoParams[i].entityName.c_str(),
            config.mSelectionVideoParams[i].select.type,
            config.mSelectionVideoParams[i].select.target,
            config.mSelectionVideoParams[i].select.flags);
    }
    for (i = 0; i < config.mSelectionParams.size() ; i++) {
       LOGE("Selection Params entityName=%s  pad=%d ,target=%d, top=%d left=%d width=%d, height=%d"
            ,config.mSelectionParams[i].entityName.c_str(),
            config.mSelectionParams[i].pad,
            config.mSelectionParams[i].target,
            config.mSelectionParams[i].top,
            config.mSelectionParams[i].left,
            config.mSelectionParams[i].width,
            config.mSelectionParams[i].height);
    }
    for (i = 0; i < config.mControlParams.size() ; i++) {
       LOGE("Control Params entityName=%s  controlId=%x ,value=%d, controlName=%s"
            ,config.mControlParams[i].entityName.c_str(),
            config.mControlParams[i].controlId,
            config.mControlParams[i].value,
            config.mControlParams[i].controlName.c_str());
    }
}

/**
 * Get binning factor values from the given node
 * \param[in] node the node to read the values from
 * \param[out] hBin horizontal binning factor
 * \param[out] vBin vertical binning factor
 */
status_t GraphConfig::getBinningFactor(const Node *node, int &hBin, int &vBin) const
{
    css_err_t ret = css_err_none;
    string value;
    ret = node->getValue(GCSS_KEY_BINNING_H_FACTOR, hBin);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get horizontal binning factor");
        return UNKNOWN_ERROR;
    }

    ret = node->getValue(GCSS_KEY_BINNING_V_FACTOR, vBin);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get vertical binning factor");
        return UNKNOWN_ERROR;
    }

    return OK;
}

/**
 * Get scaling factor values from the given node
 * \param[in] node the node to read the values from
 * \param[out] scalingNum scaling ratio
 * \param[out] scalingDenom scaling ratio
 */
status_t GraphConfig::getScalingFactor(const Node *node,
                                       int32_t &scalingNum,
                                       int32_t &scalingDenom) const
{
    css_err_t ret = css_err_none;
    string value;
    ret = node->getValue(GCSS_KEY_SCALING_FACTOR_NUM, scalingNum);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get width scaling num ratio");
        return UNKNOWN_ERROR;
    }

    ret = node->getValue(GCSS_KEY_SCALING_FACTOR_DENOM, scalingDenom);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get width scaling num ratio");
        return UNKNOWN_ERROR;
    }

    return OK;
}

/**
 * Get width and height values from the given node
 * \param[in] node the node to read the values from
 * \param[out] w width
 * \param[out] h height
 */
status_t GraphConfig::getDimensions(const Node *node, int &w, int &h) const
{
    css_err_t ret = css_err_none;
    ret = node->getValue(GCSS_KEY_WIDTH, w);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get width");
        return UNKNOWN_ERROR;
    }
    ret = node->getValue(GCSS_KEY_HEIGHT, h);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get height");
        return UNKNOWN_ERROR;
    }

    return OK;
}

/**
 * Get width, height and cropping values from the given node
 * \param[in] node the node to read the values from
 * \param[out] w width
 * \param[out] h height
 * \param[out] l left crop
 * \param[out] t top crop
 */
status_t GraphConfig::getDimensions(const Node *node, int32_t &w, int32_t &h,
                                    int32_t &l, int32_t &t) const
{
    status_t retErr = getDimensions(node, w, h);
    if (retErr != OK)
        return UNKNOWN_ERROR;

    css_err_t ret = node->getValue(GCSS_KEY_LEFT, l);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get left crop");
        return UNKNOWN_ERROR;
    }
    ret = node->getValue(GCSS_KEY_TOP, t);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get top crop");
        return UNKNOWN_ERROR;
    }

    return OK;
}

/**
 * Add format params to config
 *
 * \param[in] entityName
 * \param[in] width
 * \param[in] height
 * \param[in] pad
 * \param[in] format
 * \param[in] filed
 * \param[in] quantization
 * \param[out] config populate this struct with given values
 */
void GraphConfig::addFormatParams(const string &entityName,
                                  int width,
                                  int height,
                                  int pad,
                                  int format,
                                  int field,
                                  int quantization,
                                  MediaCtlConfig *config)
{
    if (!entityName.empty() && config) {
        MediaCtlFormatParams mediaCtlFormatParams;
        mediaCtlFormatParams.entityName = entityName;
        mediaCtlFormatParams.width = width;
        mediaCtlFormatParams.height = height;
        mediaCtlFormatParams.pad = pad;
        mediaCtlFormatParams.formatCode = format;
        mediaCtlFormatParams.stride = 0;
        mediaCtlFormatParams.field = field;
        mediaCtlFormatParams.quantization= quantization;
        config->mFormatParams.push_back(mediaCtlFormatParams);
        LOG2("@%s, entityName:%s, width:%d, height:%d, pad:%d, format:%d, format:%s, field:%d",
            __FUNCTION__, entityName.c_str(), width, height, pad, format, v4l2Fmt2Str(format), field);
    }
}

/**
 * Add control params into config
 * \param[in] entityName
 * \param[in] controlName
 * \param[in] controlId
 * \param[in] strValue
 * \param[out] config populate this struct with given values
 */
void GraphConfig::addCtlParams(const string &entityName,
                               uint32_t controlName,
                               int controlId,
                               const string &strValue,
                               MediaCtlConfig *config)
{
    if (!entityName.empty() && config) {
        int value = atoi(strValue.c_str());
        string controlNameStr = GCSS::ItemUID::key2str(controlName);

        MediaCtlControlParams mediaCtlControlParams;
        mediaCtlControlParams.entityName = entityName;
        mediaCtlControlParams.controlName = controlNameStr;
        mediaCtlControlParams.controlId = controlId;
        mediaCtlControlParams.value = value;
        config->mControlParams.push_back(mediaCtlControlParams);
        LOG2("@%s, entityName:%s, controlNameStr:%s, controlId:%d, value:%d",
            __FUNCTION__, entityName.c_str(), controlNameStr.c_str(), controlId, value);
    }
}

/**
 * Add selection params into config
 *
 * \param[in] entityName
 * \param[in] width
 * \param[in] height
 * \param[in] left
 * \param[in] top
 * \param[in] target
 * \param[in] pad
 * \param[out] config populate this struct with given values
 */
void GraphConfig::addSelectionParams(const string &entityName,
                                     int width,
                                     int height,
                                     int left,
                                     int top,
                                     int target,
                                     int pad,
                                     MediaCtlConfig *config)
{
    if (!entityName.empty() && config) {
        MediaCtlSelectionParams mediaCtlSelectionParams;
        mediaCtlSelectionParams.width = width;
        mediaCtlSelectionParams.height = height;
        mediaCtlSelectionParams.left = left;
        mediaCtlSelectionParams.top = top;
        mediaCtlSelectionParams.target = target;
        mediaCtlSelectionParams.pad = pad;
        mediaCtlSelectionParams.entityName = entityName;
        config->mSelectionParams.push_back(mediaCtlSelectionParams);
        LOG2("@%s, width:%d, height:%d, left:%d, top:%d, target:%d, pad:%d, entityName:%s",
            __FUNCTION__, width, height, left, top, target, pad, entityName.c_str());
    }
}

void GraphConfig::addSelectionVideoParams(const string &entityName,
                                     const struct v4l2_selection &select,
                                     MediaCtlConfig* config)
{
    if (entityName.empty() || !config) {
        LOGE("The config or entity <%s> is empty!", entityName.c_str());
        return;
    }

    MediaCtlSelectionVideoParams mediaCtlSelectionVideoParams;
    mediaCtlSelectionVideoParams.entityName = entityName;
    mediaCtlSelectionVideoParams.select = select;
    config->mSelectionVideoParams.push_back(mediaCtlSelectionVideoParams);
    LOG2("@%s, width:%d, height:%d, left:%d, top:%d, target:%d, type:%d, flags:%d entityName:%s",
        __FUNCTION__, select.r.width, select.r.height, select.r.left, select.r.top,
        select.target, select.type, select.flags, entityName.c_str());
}

/**
 * Add link params into config
 *
 * \param[in] srcName
 * \param[in] srcPad
 * \param[in] sinkName
 * \param[in] sinkPad
 * \param[in] enable
 * \param[in] flags
 * \param[out] config populate this struct with given values
 */
void GraphConfig::addLinkParams(const string &srcName,
                                int srcPad,
                                const string &sinkName,
                                int sinkPad,
                                int enable,
                                int flags,
                                MediaCtlConfig *config)
{
    if (!srcName.empty() && !sinkName.empty() && config) {
        MediaCtlLinkParams mediaCtlLinkParams;
        mediaCtlLinkParams.srcName = srcName;
        mediaCtlLinkParams.srcPad = srcPad;
        mediaCtlLinkParams.sinkName = sinkName;
        mediaCtlLinkParams.sinkPad = sinkPad;
        mediaCtlLinkParams.enable = enable;
        mediaCtlLinkParams.flags = flags;
        config->mLinkParams.push_back(mediaCtlLinkParams);
        LOG2("@%s, srcName:%s, srcPad:%d, sinkName:%s, sinkPad:%d, enable:%d, flags:%d",
            __FUNCTION__, srcName.c_str(), srcPad, sinkName.c_str(), sinkPad, enable, flags);
    }
}

/**
 * Gets all stream id's and generates kernel list for each of those.
 * Generated kernel lists are stored inside a mKernels map, from where
 * they can be retrieved with streamId.
 *
 */
status_t GraphConfig::generateKernelListsForStreams()
{
    return OK;
}

/**
 * query source frame parameters according to the different input
 * device: sensor or tpg
 */
status_t
GraphConfig::getSourceFrameParams(rk_aiq_frame_params &frameParams)
{
    status_t status = UNKNOWN_ERROR;
    if (mSourceType == SRC_SENSOR) {
        status = getSensorFrameParams(frameParams);
    } else if (mSourceType == SRC_TPG) {
        status = getTPGFrameParams(frameParams);
    } else {
        LOGE("wrong source");
    }
    return status;
}

/**
 * Retrieve the resolution of the tpg in use.
 */
status_t
GraphConfig::getTPGFrameParams(rk_aiq_frame_params &tpgFrameParams)
{
    css_err_t ret = css_err_none;

    if (mSourceType == SRC_TPG) {
        int32_t w,h;
        Node *tpgPortNode = nullptr;
        ret = mSettings->getDescendantByString(TPG_PORT_NAME, &tpgPortNode);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Error: Couldn't get tpg port_0 node from the graph");
            return UNKNOWN_ERROR;
        }

        ret = getDimensions(tpgPortNode, w, h);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Failed to get dimension for tpg port_0 Node");
            return UNKNOWN_ERROR;
        }

        tpgFrameParams.cropped_image_width = w;
        tpgFrameParams.cropped_image_height = h;
        tpgFrameParams.horizontal_crop_offset = 0;
        tpgFrameParams.vertical_crop_offset = 0;
        tpgFrameParams.horizontal_scaling_numerator = 1;
        tpgFrameParams.horizontal_scaling_denominator = 1;
        tpgFrameParams.vertical_scaling_numerator = 1;
        tpgFrameParams.vertical_scaling_denominator = 1;
        return OK;
    }
    return UNKNOWN_ERROR;
}

/**
 * Retrieve the resolution of the sensor mode in use.
 * sensor frame params is used to inform 3A what is the size of the
 * image that arrives to the ISP, in this case the ISA PG.
 * We pick it up from the sensor node of the graph.
 * In the settings we have had only width and height. We do not have
 * attributes for the cropping or scaling factor.
 * For that reason the dimensions set is the settings of the node should
 * be the final size produced by the sensor, not the one of the pixel array.
 */
status_t
GraphConfig::getSensorFrameParams(rk_aiq_frame_params &sensorFrameParams)
{
    Node *sensorNode = nullptr;
    Node *pixelArrayNode = nullptr;
    Node *binnerNode = nullptr;
    Node *scalerNode = nullptr;
    css_err_t ret = css_err_none;
    int32_t w,h;
    int32_t wPixArray,hPixArray;
    int32_t lPixArray,tPixArray;
    int32_t lFinalCrop = 0;
    int32_t tFinalCrop = 0;

    if (mSourceType != SRC_SENSOR) {
        LOGE("wrong source type");
        return UNKNOWN_ERROR;
    }
    // calculate the frame params when source is sensor
    ret = mSettings->getDescendant(GCSS_KEY_SENSOR, &sensorNode);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get sensor mode node from the graph");
        return UNKNOWN_ERROR;
    }

    ret = getDimensions(sensorNode, w, h);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get dimension for sensor Node");
        return UNKNOWN_ERROR;
    }

    ret = sensorNode->getDescendantByString("pixel_array:output", &pixelArrayNode);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get pixel_array:output");
        return UNKNOWN_ERROR;
    }

    ret = getDimensions(pixelArrayNode, wPixArray, hPixArray, lPixArray, tPixArray);
    if (CC_UNLIKELY(ret != css_err_none)) {
        LOGE("Failed to get pixel array output dimension and crop");
        return UNKNOWN_ERROR;
    }

    // start to accumulate cropping.
    lFinalCrop = lPixArray;
    tFinalCrop = tPixArray;

    LOG1("%s: PixelArray output: w: %d, h: %d, crop l: %d, crop t: %d",
            __FUNCTION__, wPixArray, hPixArray, lPixArray, tPixArray);

    int32_t hBinning = 1;
    int32_t vBinning = 1;
    ret = sensorNode->getDescendant(GCSS_KEY_BINNER, &binnerNode);
    if (ret != css_err_none) {
        LOGW("Warning, no binner found, make sure sensor has no binner");
    } else {
        int32_t lBinner, tBinner; // binner left and top crop
        int32_t wBinner, hBinner; // width and height of binner output
        ret = getBinningFactor(binnerNode, hBinning, vBinning);
        if (ret != css_err_none) {
            LOGE("Error: Couldn't get binning factor");
            return UNKNOWN_ERROR;
        }

        ret = sensorNode->getDescendantByString("binner:output", &binnerNode);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Failed to get binner:output");
            return UNKNOWN_ERROR;
        }

        ret = getDimensions(binnerNode, wBinner, hBinner, lBinner, tBinner);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Failed to get binner output dimensions and crop");
            return UNKNOWN_ERROR;
        }
        LOG1("%s: binner output w: %d, %d,"
             " binning: w: %d, h: %d, crop w: %d, crop h: %d", __FUNCTION__, wBinner, hBinner,
                                                               hBinning, vBinning,
                                                               lBinner, tBinner);

        // accumulate binner cropping.
        lFinalCrop += (lBinner * hBinning);
        tFinalCrop += (tBinner * vBinning);
    }

    int32_t scalingNum = 1; // avoid possible division by 0
    int32_t scalingDenom = 1; // avoid possible division by 0
    int32_t lScaler = 0; // left scaler crop
    int32_t tScaler = 0; // top scaler crop
    hBinning = (hBinning <= 0) ? 1 : hBinning;
    vBinning = (vBinning <= 0) ? 1 : vBinning;
    int32_t wScaler = wPixArray / hBinning;
    int32_t hScaler = hPixArray / vBinning;

    ret = sensorNode->getDescendant(GCSS_KEY_SCALER, &scalerNode);
    if (ret != css_err_none) {
        LOGW("Warning, no scaler found, make sure sensor has no scaler");
    } else {

        ret = getScalingFactor(scalerNode, scalingNum, scalingDenom);
        if (ret != css_err_none) {
            LOGE("Error: Couldn't get scaling factor");
            return UNKNOWN_ERROR;
        }

        if (scalingDenom == 0) {
            LOGE("Scaling Denominator is 0! Wrong setting! Set to 16");
            scalingNum = 16;
        }
        if (scalingNum == 0) {
            LOGE("Scaling Numerator is 0! Wrong setting! Set to 16");
            scalingDenom = 16;
        }

        ret = sensorNode->getDescendantByString("scaler:output", &scalerNode);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Failed to get scaler:output");
            return UNKNOWN_ERROR;
        }

        ret = getDimensions(scalerNode, wScaler, hScaler, lScaler, tScaler);
        if (CC_UNLIKELY(ret != css_err_none)) {
            LOGE("Failed to get scaler output dimensions and crop");
            return UNKNOWN_ERROR;
        }

        LOG1("%s: scaler output  w: %d, h: %d, crop w: %d, crop h: %d",
                __FUNCTION__,
                wScaler, hScaler,
                lScaler, tScaler);
    }
    int32_t wPixFormatIn = wScaler;
    int32_t hPixFormatIn = hScaler;
    int32_t wPixFormatOut = wPixFormatIn;
    int32_t hPixFormatOut = hPixFormatIn;
    int32_t lPixFormat = 0;
    int32_t tPixFormat = 0;

    int32_t lLastStep; // all horizontal croppings after last scaling
    int32_t tLastStep; // all vertical croppings after last scaling

    /*pixel formatter crop and scaler crop are be handled at the same time
     * since they appear after the scaling
     */
    lLastStep = lPixFormat + lScaler;
    tLastStep = tPixFormat + tScaler;

    lFinalCrop += ((lLastStep * scalingDenom) / scalingNum) * hBinning;
    tFinalCrop += ((tLastStep * scalingDenom) / scalingNum) * vBinning;

    int32_t wCroppedImage = ((wPixFormatOut * scalingDenom) / scalingNum * hBinning);
    int32_t hCroppedImage = ((hPixFormatOut * scalingDenom) / scalingNum * vBinning);

    LOG1("------------------- sensorFrameParams ---------------------------");
    LOG1("%s: Final cropped Image w = %d, Final cropped Image h = %d",
                 __FUNCTION__, wCroppedImage, hCroppedImage);

    LOG1("%s: Horizontal_crop_offset = %d, Vertical_crop_offset = %d",
                 __FUNCTION__, lFinalCrop, tFinalCrop);
    LOG1("-----------------------------------------------------------------");

    sensorFrameParams.cropped_image_width = wCroppedImage;
    sensorFrameParams.cropped_image_height = hCroppedImage;
    sensorFrameParams.horizontal_crop_offset = lFinalCrop;
    sensorFrameParams.vertical_crop_offset = tFinalCrop;
    sensorFrameParams.horizontal_scaling_numerator = SCALING_FACTOR;
    sensorFrameParams.horizontal_scaling_denominator = SCALING_FACTOR;
    sensorFrameParams.vertical_scaling_numerator = SCALING_FACTOR;
    sensorFrameParams.vertical_scaling_denominator = SCALING_FACTOR;
    return OK;
}

/**
 * Retrieve the resolution of the first port for a given stream id
 *
 * Fill the resolution inside the a frame params struct for convenience
 *
 * \param[in] streamId  Id of the stream to use.
 * \param[out] frameParams
 *
 * \return OK
 * \return UNKNOWN_ERROR if it could not find the stream id or the port
 */
status_t
GraphConfig::streamGetFrameParams(rk_aiq_frame_params &frameParams,
                                  int32_t streamId)
{
    Node *portNode = nullptr;
    status_t status = OK;
    PortFormatSettings format;

    status = streamGetInputPort(streamId, &portNode);
    status |= portGetFormat(portNode, format);
    if (CC_UNLIKELY(status != OK)) {
        LOGE("Failed to get input port format from stream %d", streamId);
        return UNKNOWN_ERROR;
    }

    frameParams.cropped_image_width = format.width;
    frameParams.cropped_image_height = format.height;
    frameParams.horizontal_crop_offset = 0;
    frameParams.vertical_crop_offset = 0;
    frameParams.horizontal_scaling_numerator = 1;
    frameParams.horizontal_scaling_denominator = 1;
    frameParams.vertical_scaling_numerator = 1;
    frameParams.vertical_scaling_denominator = 1;
    return OK;
}

void GraphConfig::dumpSettings()
{
    mSettings->dumpNodeTree(mSettings, 2);
}

void GraphConfig::dumpKernels(int32_t streamId)
{
}

GraphConfig::Rectangle::Rectangle(): w(0),h(0),t(0),l(0) {}
GraphConfig::SubdevPad::SubdevPad(): Rectangle(), mbusFormat(0){}
GraphConfig::SourceNodeInfo::SourceNodeInfo() : metadataEnabled(false),
                                                interlaced(0) {}
} // namespace camera2
} // namespace android
