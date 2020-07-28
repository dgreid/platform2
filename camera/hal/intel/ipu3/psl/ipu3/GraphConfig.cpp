/*
 * Copyright (C) 2017-2018 Intel Corporation
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
#include <cros-camera/v4l2_device.h>
#include <linux/v4l2-subdev.h>
#include <linux/intel-ipu3.h>
#include <algorithm>

#include "MediaEntity.h"

using GCSS::GraphConfigNode;
using std::string;
using std::vector;
using std::map;
using std::set;


namespace cros {
namespace intel {

extern int32_t gDumpType;

namespace gcu = graphconfig::utils;

// TODO: Change the format attribute natively as integer attribute
#ifndef VIDEO_RECORDING_FORMAT
#define VIDEO_RECORDING_FORMAT TILE
#endif

#define MEDIACTL_PAD_OUTPUT_NUM 2
#define MEDIACTL_PAD_VF_NUM 3
#define SCALING_FACTOR 1

const string csi2WithoutPort = "ipu3-csi2 ";
const string imguWithoutPort = "ipu3-imgu ";

const string MEDIACTL_INPUTNAME = " input";
const string MEDIACTL_PARAMETERNAME = " parameters";
const string MEDIACTL_MAIN_NAME = " output";
const string MEDIACTL_VF_NAME = " viewfinder";
const string MEDIACTL_STATNAME = " 3a stat";

GraphConfig::GraphConfig() :
        mSettings(nullptr),
        mReqId(0),
        mPipeType(PIPE_VIDEO),
        mSourceType(SRC_NONE)
{
    mSinkPeerPort.clear();
    mStreamToSinkIdMap.clear();
    mStream2TuningMap.clear();
    mCSIBE = CSI_BE + "0";
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
    mSinkPeerPort.clear();
    mStreamToSinkIdMap.clear();
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
status_t GraphConfig::prepare(Node *settings,
                              StreamToSinkMap &streamToSinkIdMap)
{
    if (CC_UNLIKELY(settings == nullptr)) {
        LOGW("Settings is nullptr!! - BUG?");
        return UNKNOWN_ERROR;
    }

    mSettings = settings;
    status_t ret = OK;

    analyzeSourceType();

    ret = getActiveOutputPorts(streamToSinkIdMap);
    if (ret != OK) {
        LOGE("Failed to get output ports");
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
                // This should not fail
                ret = result->getValue(GCSS_KEY_NAME, pgName);
                LOGW("Failed to find stream id for PG %s", pgName.c_str());
                continue;
            }
            tuningMode = 0; // default value in case it is not found
            ret = result->getValue(GCSS_KEY_TUNING_MODE, tuningMode);
            if (ret != css_err_none) {
                string pgName;
                // This should not fail
                ret = result->getValue(GCSS_KEY_NAME, pgName);
                LOGW("Failed t find tuning mode for PG %s, defaulting to %d",
                        pgName.c_str(), tuningMode);
            }
            mStream2TuningMap[streamId] =  tuningMode;
        }
    }
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
    } else {
        LOG1("No sensor node from the graph");
    }
    return OK;
}

/**
 * Finds the sink nodes and the output port peer. Use streamToSinkIdMap
 * since we are interested only in sinks that serve a stream. Takes an
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
        ret = sink->getValue(GCSS_KEY_NAME, sinkName);
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
    }
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
    // Imgu format. The tool and getMBusFormat is not in sync.
    if (tmp == "RA10" || tmp == "RG10") {
        info.output.mbusFormat = MEDIA_BUS_FMT_SRGGB10_1X10;
    } else if (tmp == "BA10" || tmp == "BG10") {
        info.output.mbusFormat = MEDIA_BUS_FMT_SGRBG10_1X10;
    } else {
        LOGE("Error: No valid format set in the settings.");
    }

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
 * \param[in/out] cio2Format   the format to use
 * \param[out] mediaCtlConfig  the struct to populate
 * \return OK              at success
 * \return UNKNOWN ERROR   at failure
 */

status_t GraphConfig::getCio2MediaCtlData(int *cio2Format, MediaCtlConfig *mediaCtlConfig)
{
    CheckError((!mediaCtlConfig), BAD_VALUE, "@%s null ptr\n", __FUNCTION__);

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
        *cio2Format = sourceInfo.pa.out.mbusFormat;

        // get the next entity port of the "ov5670 binner 11-0010" which is dynamical.
        // it could "ipu3-csi2 0" or "ipu3-csi2 1", then it will get 0 or 1.
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
        csi2 = csi2WithoutPort + std::to_string(port);
        mCSIBE = CSI_BE + std::to_string(port);
        LOG1(" csi2 is:%s, cio2 is:%s\n", csi2.c_str(), mCSIBE.c_str());
    } else {
        LOGE("Error: No source");
        return UNKNOWN_ERROR;
    }

    // Add control params
    retErr = addControls(sourceNode, sourceInfo, mediaCtlConfig);
    if (retErr != OK) {
        return UNKNOWN_ERROR;
    }

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

    // Get csi_be node. If not found, try csi_be_soc. If not found return error.
    int csiBEOutW = 0, csiBEOutH = 0;
    Node *csiBEOutput = nullptr;
    ret = mSettings->getDescendantByString("csi_be:output", &csiBEOutput);
    CheckError(ret != css_err_none, UNKNOWN_ERROR, "Failed to get csi_be nodes from the graph");

    ret = csiBEOutput->getValue(GCSS_KEY_FORMAT, formatStr);
    CheckError(ret != css_err_none, UNKNOWN_ERROR, "Failed to get format from the graph");

    retErr = getDimensions(csiBEOutput, csiBEOutW, csiBEOutH);
    CheckError(retErr != OK, UNKNOWN_ERROR, "Failed to get values from csi be output");

    /* consistency check, we have at least one CSI-BE */
    CheckError(csiBEOutput == nullptr, UNKNOWN_ERROR, "CSI BE Output nullptr");

    /* Set sensor pixel array parameter to the attributes in 'sensor_mode' node,
     * ignore the attributes in pixel_array and binner node due to upstream driver
     * removed binner and scaler subdev.
     */
    addFormatParams(sourceInfo.pa.name, sourceInfo.output.w, sourceInfo.output.h, 0, sourceInfo.output.mbusFormat, 0, mediaCtlConfig);

    // Add cio2 format into mediaCtlConfig
    addFormatParams(csi2, csiBEOutW, csiBEOutH, 0, sourceInfo.output.mbusFormat, 0, mediaCtlConfig);
    addFormatParams(csi2, csiBEOutW, csiBEOutH, 1, sourceInfo.output.mbusFormat, 0, mediaCtlConfig);
    addFormatParams(mCSIBE, csiBEOutW, csiBEOutH, 0, sourceInfo.pa.out.mbusFormat, 0, mediaCtlConfig);
    addFormatParams(mCSIBE, csiBEOutW, csiBEOutH, 1, sourceInfo.pa.out.mbusFormat, 0, mediaCtlConfig);

    // Add video nodes into mediaCtlConfig
    addVideoNodes(mediaCtlConfig);

    /* Start populating selections into mediaCtlConfig
     * entity name, width, height, left crop, top crop, target, pad, config */
    addSelectionParams(sourceInfo.pa.name,
                       sourceInfo.pa.out.w,
                       sourceInfo.pa.out.h,
                       sourceInfo.pa.out.l,
                       sourceInfo.pa.out.t,
                       V4L2_SEL_TGT_CROP, 0 /* sink pad */, mediaCtlConfig);


    LOG1("Adding Isys link %s:[0] -> %s:[0]", sourceInfo.pa.name.c_str(), csi2.c_str());
    addLinkParams(sourceInfo.pa.name, 0, csi2, 0, 1, MEDIA_LNK_FL_ENABLED, mediaCtlConfig);

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
                                          int cio2Format,
                                          int32_t testPatternMode,
                                          bool enableStill,
                                          MediaCtlConfig *mediaCtlConfig)
{
    CheckError(!mediaCtlConfig, BAD_VALUE, "@%s null ptr\n", __func__);

    int ret;

    Node *imgu = nullptr;
    int width = 0, height = 0, format = 0;
    int enabled = 1;

    string imguIndex = enableStill ? std::to_string(STILL_PIPE_INDEX) : std::to_string(VIDEO_PIPE_INDEX);
    string kImguName = imguWithoutPort + imguIndex;
    string inputName = imguWithoutPort + imguIndex + MEDIACTL_INPUTNAME;
    string statName = imguWithoutPort + imguIndex + MEDIACTL_STATNAME;
    string parameterName = imguWithoutPort + imguIndex + MEDIACTL_PARAMETERNAME;
    string outputName = imguWithoutPort + imguIndex + MEDIACTL_MAIN_NAME;

    ret = mSettings->getDescendant(GCSS_KEY_IMGU, &imgu);
    if (ret != css_err_none) {
        LOGE("Error: Couldn't get imgu node");
        return UNKNOWN_ERROR;
    }

    for (size_t i = 0; i < mLut.size(); i++) {
        Node *pipe = nullptr;
        struct v4l2_subdev_selection cropSelect, composeSelect;
        CLEAR(cropSelect);
        CLEAR(composeSelect);

        ret = imgu->getDescendant(mLut[i].uid, &pipe);
        if (ret != css_err_none) {
            LOGD("<%u> node is not present in graph (descriptor or settings) - continuing.", mLut[i].uid);
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

        if (fourccFormat == "NV12")
            format = V4L2_PIX_FMT_NV12;
        else if (fourccFormat == "CIO2")
            format = cio2Format;
        else if (fourccFormat == "YUYV")
            format = V4L2_PIX_FMT_YUYV;
        else
            return UNKNOWN_ERROR;

        if (mLut[i].uidStr == GC_MAIN ||
            mLut[i].uidStr == GC_VF) {

            int nodeWidth = 0, nodeHeight = 0;

            // Get GDC info
            ret = getNodeInfo(GCSS_KEY_IMGU_GDC, *imgu, &nodeWidth, &nodeHeight);
            if (ret != OK) {
                LOGE("pipe log name: %s can't get info!", mLut[i].nodeName.c_str());
                return UNKNOWN_ERROR;
            }
            LOG2("pipe log name: %s  gdc size %dx%d", mLut[i].nodeName.c_str(), nodeWidth, nodeHeight);
            addFormatParams(kImguName, nodeWidth, nodeHeight, 0,
                                     V4L2_MBUS_FMT_UYVY8_2X8, 0, mediaCtlConfig);

            // Get IF info
            ret = getNodeInfo(GCSS_KEY_IMGU_IF, *imgu, &nodeWidth, &nodeHeight);
            if (ret != OK) {
                LOGE("pipe log name: %s can't get info!", mLut[i].nodeName.c_str());
                return UNKNOWN_ERROR;
            }

            cropSelect.which = V4L2_SUBDEV_FORMAT_ACTIVE;
            cropSelect.target = V4L2_SEL_TGT_CROP;
            cropSelect.pad = 0;
            cropSelect.flags = 0;
            cropSelect.r.left = 0;
            cropSelect.r.top = 0;
            cropSelect.r.width = nodeWidth;
            cropSelect.r.height = nodeHeight;
            addSelectionVideoParams(inputName, cropSelect, mediaCtlConfig);
            LOG2("pipe log name: %s  if size %dx%d", mLut[i].nodeName.c_str(), nodeWidth, nodeHeight);

            // Get BDS info
            ret = getNodeInfo(GCSS_KEY_IMGU_BDS, *imgu, &nodeWidth, &nodeHeight);
            if (ret != OK) {
                LOGE("pipe log name: %s can't get info!", mLut[i].nodeName.c_str());
                return UNKNOWN_ERROR;
            }

            composeSelect.which = V4L2_SUBDEV_FORMAT_ACTIVE;
            composeSelect.target = V4L2_SEL_TGT_COMPOSE;
            composeSelect.pad = 0;
            composeSelect.flags = 0;
            composeSelect.r.left = 0;
            composeSelect.r.top = 0;
            composeSelect.r.width = nodeWidth;
            composeSelect.r.height = nodeHeight;
            addSelectionVideoParams(inputName, composeSelect, mediaCtlConfig);
            LOG2("pipe log name: %s  bds size %dx%d", mLut[i].nodeName.c_str(), nodeWidth, nodeHeight);
        }

        /* if node active add it to mediactl config */
        if (width != 0) {
            LOG2("Adding video node: %d %s %s", mLut[i].ipuNodeName, NODE_NAME(pipe), mLut[i].nodeName.c_str());
            addImguVideoNode(mLut[i].ipuNodeName, mLut[i].nodeName, mediaCtlConfig);
        }

        /* Use BGGR as bayer format when specific sensor receives test pattern request */
        if (testPatternMode != ANDROID_SENSOR_TEST_PATTERN_MODE_OFF) {
            const IPU3CameraCapInfo* capInfo = getIPU3CameraCapInfo(cameraId);
            CheckError(capInfo == nullptr, UNKNOWN_ERROR, "@%s: failed to get cameraCapInfo", __FUNCTION__);
            if (mLut[i].nodeName.compare(inputName) == 0 &&
                !capInfo->getTestPatternBayerFormat().empty()) {
                format = gcu::getV4L2Format(capInfo->getTestPatternBayerFormat());
            }
        }
        addFormatParams(mLut[i].nodeName, width, height, 1, format, 0, mediaCtlConfig);

        if (mLut[i].uidStr != GC_INPUT) {
            addLinkParams(kImguName, mLut[i].pad, mLut[i].nodeName, 0, 1, MEDIA_LNK_FL_ENABLED, mediaCtlConfig);
            /*
             * Set main node of imgu to be same with vf node if the
             * selected graph setting just has only one output
             */
            int32_t outputNum = 0;
            ret = mSettings->getValue(GCSS_KEY_ACTIVE_OUTPUTS, outputNum);
            CheckError(ret != css_err_none, UNKNOWN_ERROR, "Couldn't get active output from graph config");

            if (outputNum < MAX_OUTPUT_NUM_IN_PIPE) {
                LOG2("Adding video node: %d %s %s", IMGU_NODE_MAIN, "video", outputName.c_str());
                addSelectionVideoParams(inputName, cropSelect, mediaCtlConfig);
                addSelectionVideoParams(inputName, composeSelect, mediaCtlConfig);
                addImguVideoNode(IMGU_NODE_MAIN, outputName, mediaCtlConfig);
                addFormatParams(outputName, width, height, 1, format, 0, mediaCtlConfig);
                addLinkParams(kImguName, MEDIACTL_PAD_OUTPUT_NUM, outputName,
                    0, 1, MEDIA_LNK_FL_ENABLED, mediaCtlConfig);
            }
        } else {
            addLinkParams(mLut[i].nodeName, 0, kImguName, mLut[i].pad, 1, MEDIA_LNK_FL_ENABLED, mediaCtlConfig);
        }
    }

    LOG2("Adding stat node");
    addImguVideoNode(IMGU_NODE_STAT, statName, mediaCtlConfig);
    addLinkParams(kImguName, 4, statName, 0, 1, MEDIA_LNK_FL_ENABLED, mediaCtlConfig);

    LOG2("Adding parameter node");
    addImguVideoNode(IMGU_NODE_PARAM, parameterName, mediaCtlConfig);
    addLinkParams(parameterName, 0, kImguName, 1, 1, MEDIA_LNK_FL_ENABLED, mediaCtlConfig);

    std::string runningMode = std::to_string(enableStill ? STILL_PIPE_INDEX : VIDEO_PIPE_INDEX);
    addCtlParams(kImguName, 0, V4L2_CID_INTEL_IPU3_MODE, runningMode, mediaCtlConfig);

    return ret;
}

void GraphConfig::setMediaCtlConfig(std::shared_ptr<MediaController> mediaCtl,
                                    bool enableStill)
{
    mMediaCtl = mediaCtl;

    // "ipu3-imgu 0" for video pipe, "ipu3-imgu 1" for still pipe
    string imguIndex = enableStill ? std::to_string(STILL_PIPE_INDEX) : std::to_string(VIDEO_PIPE_INDEX);
    const string mainName = imguWithoutPort + imguIndex + MEDIACTL_MAIN_NAME;
    const string vfName = imguWithoutPort + imguIndex + MEDIACTL_VF_NAME;
    const string inputName = imguWithoutPort + imguIndex + MEDIACTL_INPUTNAME;
    int32_t outputNum = 0;

    mLut.clear();
    MediaCtlLut lut;
    lut.ipuNodeName = IMGU_NODE_NULL;

    int ret = mSettings->getValue(GCSS_KEY_ACTIVE_OUTPUTS, outputNum);
    CheckError(ret != css_err_none, VOID_VALUE, "Couldn't not get the active output from graph config");

    lut.uid = GCSS_KEY_IMGU_VF;
    lut.uidStr = GCSS::ItemUID::key2str(lut.uid);
    lut.pad = MEDIACTL_PAD_VF_NUM;
    lut.nodeName = vfName;
    LOG2("save graph setting nodes: %s, pad: %d, nodename: %s", lut.uidStr.c_str(), lut.pad, lut.nodeName.c_str());
    mLut.push_back(lut);

    if (outputNum >= MAX_OUTPUT_NUM_IN_PIPE) {
        lut.uid = GCSS_KEY_IMGU_MAIN;
        lut.uidStr = GCSS::ItemUID::key2str(lut.uid);
        lut.pad = MEDIACTL_PAD_OUTPUT_NUM;
        lut.nodeName = mainName;
        LOG2("save graph setting nodes: %s, pad: %d, nodename: %s", lut.uidStr.c_str(), lut.pad, lut.nodeName.c_str());
        mLut.push_back(lut);
    }

    lut.uid = GCSS_KEY_INPUT;
    lut.uidStr = GCSS::ItemUID::key2str(lut.uid);
    lut.pad = 0;
    lut.nodeName = inputName;
    LOG2("save graph setting nodes: %s, pad: %d, nodename: %s", lut.uidStr.c_str(), lut.pad, lut.nodeName.c_str());
    mLut.push_back(lut);

    for (auto & it : mLut) {
         it.ipuNodeName = (it.uid == GCSS_KEY_IMGU_MAIN) ? IMGU_NODE_MAIN
                        : (it.uid == GCSS_KEY_IMGU_VF)   ? IMGU_NODE_VF
                        : (it.uid == GCSS_KEY_INPUT)        ? IMGU_NODE_INPUT
                                                            : IMGU_NODE_NULL;
    }
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
 * \param mediaCtlConfig[out] populate this struct with given values
 */
void GraphConfig::addVideoNodes(MediaCtlConfig *config)
{
    CheckError((!config), VOID_VALUE, "@%s null ptr\n", __FUNCTION__);

    MediaCtlElement mediaCtlElement;

    // Imgu support
    mediaCtlElement.isysNodeName = ISYS_NODE_RAW;
    mediaCtlElement.name = mCSIBE;
    config->mVideoNodes.push_back(mediaCtlElement);
}

void GraphConfig::addImguVideoNode(int ipuNodeName, const string& nodeName, MediaCtlConfig *config)
{
    CheckError((!config), VOID_VALUE, "@%s null ptr\n", __FUNCTION__);
    CheckError((ipuNodeName == IMGU_NODE_NULL), VOID_VALUE, "@%s null ipu node name\n", __FUNCTION__);

    MediaCtlElement mediaCtlElement;
    mediaCtlElement.name = nodeName;
    mediaCtlElement.isysNodeName = ipuNodeName;
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
 * \param[out] config populate this struct with given values
 */
void GraphConfig::addFormatParams(const string &entityName,
                                  int width,
                                  int height,
                                  int pad,
                                  int format,
                                  int field,
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
        string controlNameStr("");
        if (controlName)
            controlNameStr = GCSS::ItemUID::key2str(controlName);

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
                                     const struct v4l2_subdev_selection &select,
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
    LOG2("@%s, width:%d, height:%d, left:%d, top:%d, target:%d, which:%d, flags:%d entityName:%s",
        __FUNCTION__, select.r.width, select.r.height, select.r.left, select.r.top,
        select.target, select.which, select.flags, entityName.c_str());
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
GraphConfig::getSensorFrameParams(ia_aiq_frame_params &sensorFrameParams)
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
    if (ret == css_err_none) {

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

GraphConfig::Rectangle::Rectangle(): w(0),h(0),t(0),l(0) {}
GraphConfig::SubdevPad::SubdevPad(): Rectangle(), mbusFormat(0){}
GraphConfig::SourceNodeInfo::SourceNodeInfo() : metadataEnabled(false),
                                                interlaced(0) {}
} // namespace intel
} // namespace cros
