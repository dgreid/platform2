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

#define LOG_TAG "MtkCam/ppl_context"
//
#include <future>
#include "MyUtils.h"
#include <map>
#include <memory>
#include <mtkcam/utils/std/Profile.h>
#include <mtkcam/pipeline/pipeline/PipelineContextImpl.h>
#include <string>
#include <vector>
#include <property_lib.h>
//
using NSCam::v3::IPipelineDAG;
using NSCam::v3::IPipelineFrame;
using NSCam::v3::NSPipelineContext::collect_from_NodeIOMaps;
using NSCam::v3::NSPipelineContext::collect_from_stream_config;
using NSCam::v3::NSPipelineContext::config_pipeline;
using NSCam::v3::NSPipelineContext::construct_FrameNodeMapControl;
using NSCam::v3::NSPipelineContext::ContextNode;
using NSCam::v3::NSPipelineContext::createHalStreamBufferPool;
using NSCam::v3::NSPipelineContext::DefaultDispatcher;
using NSCam::v3::NSPipelineContext::evaluate_buffer_users;
using NSCam::v3::NSPipelineContext::HalImageStreamBufferPoolT;
using NSCam::v3::NSPipelineContext::INodeActor;
using NSCam::v3::NSPipelineContext::NodeConfig;
using NSCam::v3::NSPipelineContext::PipelineConfig;
using NSCam::v3::NSPipelineContext::PipelineContext;
using NSCam::v3::NSPipelineContext::RequestBuilderImpl;
using NSCam::v3::NSPipelineContext::set_streaminfoset_from_config;
using NSCam::v3::NSPipelineContext::StreamConfig;
using NSCam::v3::NSPipelineContext::update_streambuffers_to_frame;
using NSCam::v3::NSPipelineContext::update_streaminfo_to_set;

/******************************************************************************
 *
 ******************************************************************************/
MVOID
RequestBuilderImpl::onRequestConstructed() {
  // clear one-shot data
  mStreamBuffers_Image.clear();
  mStreamBuffers_HalImage.clear();
  mStreamBuffers_Meta.clear();
  mStreamBuffers_HalMeta.clear();
  //
  mFlag = FLAG_NO_CHANGE;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
RequestBuilderImpl::dump(MUINT32 const reqNo, MUINT32 const frameNo) const {
  MY_LOGD("dump reqNo %d frameNo %d +", reqNo, frameNo);
  MY_LOGD("Image IOMap:");
  for (auto& i_node : mImageNodeIOMaps) {
    NodeId_T const nodeId = i_node.first;
    IOMapSet const& mapSet = i_node.second;
    for (size_t i_map = 0; i_map < mapSet.size(); i_map++) {
      std::string* dumpLog = NSPipelineContext::dump(mapSet[i_map]);
      MY_LOGD("nodeId %#" PRIxPTR " #%zu: %s", nodeId, i_map, dumpLog->c_str());
    }
  }
  //
  MY_LOGD("Meta IOMap:");
  for (auto& i_node : mMetaNodeIOMaps) {
    NodeId_T const nodeId = i_node.first;
    IOMapSet const& mapSet = i_node.second;
    for (size_t i_map = 0; i_map < mapSet.size(); i_map++) {
      std::string* dumpLog = NSPipelineContext::dump(mapSet[i_map]);
      MY_LOGD("nodeId %#" PRIxPTR " #%zu: %s", nodeId, i_map, dumpLog->c_str());
    }
  }
  //
  MY_LOGD("Node edge:");
  for (size_t i = 0; i < mNodeEdges.size(); i++) {
    MY_LOGD("nodeId %#" PRIxPTR " -> %#" PRIxPTR, mNodeEdges[i].src,
            mNodeEdges[i].dst);
  }
  //
  MY_LOGD_IF(!mpCallback.expired(), "callback is set(%p)",
             mpCallback.lock().get());
  //
  for (auto& i : mReplacingInfos) {
    MY_LOGD("replacing stream %#" PRIx64, i.second->getStreamId());
  }
  //
#define sb_dump(sbmap, str)                 \
  for (auto& it : sbmap) {                  \
    MY_LOGD("%s %#" PRIx64, str, it.first); \
  }
  sb_dump(mStreamBuffers_Image, "StreamBuffer(Image):");
  sb_dump(mStreamBuffers_HalImage, "StreamBuffer(HalImage):");
  sb_dump(mStreamBuffers_Meta, "StreamBuffer(Meta):");
  sb_dump(mStreamBuffers_HalMeta, "StreamBuffer(HalMeta):");
#undef sb_dump
  MY_LOGD("dump frameNo req %d frameNo %d -", reqNo, frameNo);
}

/******************************************************************************
 *
 ******************************************************************************/
PipelineContext::PipelineContextImpl::PipelineContextImpl(char const* name)
    : mName(name)
      //
      ,
      mpStreamConfig(std::make_shared<StreamConfig>()),
      mpNodeConfig(std::make_shared<NodeConfig>()),
      mpPipelineConfig(std::make_shared<PipelineConfig>())
      //
      ,
      mInFlush(MFALSE) {
  pthread_rwlock_init(&mRWLock, NULL);
  pthread_rwlock_init(&mFlushLock, NULL);
}

/******************************************************************************
 *
 ******************************************************************************/
PipelineContext::PipelineContextImpl::~PipelineContextImpl() {
  MY_LOGD("deconstruction");
  onLastStrongRef(nullptr);
  pthread_rwlock_destroy(&mRWLock);
  pthread_rwlock_destroy(&mFlushLock);
}

/******************************************************************************
 *
 ******************************************************************************/
void PipelineContext::PipelineContextImpl::onLastStrongRef(const void* /*id*/) {
  //
  MY_LOGD("wait drained before destroy +");
  waitUntilDrained();
  MY_LOGD("wait drained before destroy -");
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::updateConfig(NodeBuilderImpl* pBuilder) {
  pthread_rwlock_wrlock(&mRWLock);
  //
  std::shared_ptr<ContextNode>& pNode = pBuilder->mpContextNode;
  StreamSet& inStreamSet = pBuilder->mInStreamSet;
  StreamSet& outStreamSet = pBuilder->mOutStreamSet;
  StreamUsageMap& usgMap = pBuilder->mUsageMap;
  //
  if (inStreamSet.size() == 0 && outStreamSet.size() == 0) {
    MY_LOGE("should set in/out stream to node");
    pthread_rwlock_unlock(&mRWLock);
    return BAD_VALUE;
  }
  //
  std::shared_ptr<IStreamInfoSetControl> pInStreams =
      IStreamInfoSetControl::create();
  std::shared_ptr<IStreamInfoSetControl> pOutStreams =
      IStreamInfoSetControl::create();
  MERROR err;
  {
    set_streaminfoset_from_config::Params param = {
        .pStreamSet = &inStreamSet,
        .pStreamConfig = mpStreamConfig.get(),
        .pSetControl = pInStreams.get()};
    if (NSCam::OK != (err = set_streaminfoset_from_config()(param))) {
      MY_LOGE("set_streaminfoset_from_config err:%d(%s)", err,
              ::strerror(-err));
      pthread_rwlock_unlock(&mRWLock);
      return err;
    }
  }
  //
  {
    set_streaminfoset_from_config::Params param = {
        .pStreamSet = &outStreamSet,
        .pStreamConfig = mpStreamConfig.get(),
        .pSetControl = pOutStreams.get()};
    if (NSCam::OK != (err = set_streaminfoset_from_config()(param))) {
      MY_LOGE("set_streaminfoset_from_config err:%d(%s)", err,
              ::strerror(-err));
      pthread_rwlock_unlock(&mRWLock);
      return err;
    }
  }
  //
  pNode->setInStreams(pInStreams);
  pNode->setOutStreams(pOutStreams);

  // update to NodeConfig
  NodeId_T const nodeId = pNode->getNodeId();
  mpNodeConfig->addNode(nodeId, pNode);
  mpNodeConfig->setImageStreamUsage(nodeId, usgMap);
  //
  pthread_rwlock_unlock(&mRWLock);
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::reuseNode(
    NodeId_T const nodeId,
    std::shared_ptr<ContextNode> pNode,
    StreamUsageMap const& usgMap) {
  pthread_rwlock_wrlock(&mRWLock);
  //
  if (!pNode) {
    pthread_rwlock_unlock(&mRWLock);
    return BAD_VALUE;
  }
  //
  MY_LOGD("Reuse node(%" PRIdPTR "): %s", nodeId,
          pNode->getNode()->getNodeName());
  mpNodeConfig->addNode(nodeId, pNode);
  mpNodeConfig->setImageStreamUsage(nodeId, usgMap);
  //
  pthread_rwlock_unlock(&mRWLock);
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::updateConfig(
    StreamBuilderImpl* pBuilder) {
  pthread_rwlock_wrlock(&mRWLock);
  //
  MUINT32 const type = pBuilder->mType;
  //
  if (TypeOf(type) == eType_IMAGE) {
    std::shared_ptr<IImageStreamInfo> pStreamInfo = pBuilder->mpImageStreamInfo;
    if (!pStreamInfo) {
      MY_LOGE("inconsistent type 0x%x", type);
      pthread_rwlock_unlock(&mRWLock);
      return BAD_VALUE;
    }
    // 1. check if this stream is already marked as reuse
    // 2. add <stream, pool or provider> to context
    auto pItem =
        std::make_shared<StreamConfig::ItemImageStream>(pStreamInfo, type);
    //
    if (type == eStreamType_IMG_HAL_POOL) {
      // create pool
      std::shared_ptr<HalImageStreamBufferPoolT> pPool =
          createHalStreamBufferPool(getName(), pStreamInfo);
      if (!pPool) {
        MY_LOGE("create pool failed: %s", pStreamInfo->toString().c_str());
        pthread_rwlock_unlock(&mRWLock);
        return DEAD_OBJECT;
      }
      //
      pItem->pPool = pPool;
    } else if (type == eStreamType_IMG_HAL_PROVIDER) {
      // get SB Provider set by user
      std::shared_ptr<IStreamBufferProviderT> const pProvider =
          pBuilder->mpProvider;
      if (!pProvider) {
        MY_LOGE("get provider failed: %s", pStreamInfo->toString().c_str());
        pthread_rwlock_unlock(&mRWLock);
        return DEAD_OBJECT;
      }
      //
      pItem->pProvider = pProvider;
    }

    // 4. add <stream, type> to context
    auto ret = mpStreamConfig->add(pItem);
    pthread_rwlock_unlock(&mRWLock);
    return ret;
  } else if (TypeOf(type) == eType_META) {
    std::shared_ptr<IMetaStreamInfo> pStreamInfo = pBuilder->mpMetaStreamInfo;
    if (!pStreamInfo) {
      MY_LOGE("inconsistent type 0x%x", type);
      pthread_rwlock_unlock(&mRWLock);
      return BAD_VALUE;
    }
    //
    auto pItem =
        std::make_shared<StreamConfig::ItemMetaStream>(pStreamInfo, type);
    auto ret = mpStreamConfig->add(pItem);
    pthread_rwlock_unlock(&mRWLock);
    return ret;
  }
  MY_LOGE("not supported type 0x%x", type);
  pthread_rwlock_unlock(&mRWLock);
  return UNKNOWN_ERROR;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::reuseStream(
    std::shared_ptr<StreamConfig::ItemImageStream> pItem) {
  pthread_rwlock_wrlock(&mRWLock);
  //
  if (!pItem) {
    pthread_rwlock_unlock(&mRWLock);
    return BAD_VALUE;
  }
  //
  MY_LOGD("Reuse image stream: type 0x%x, %s", pItem->type,
          pItem->pInfo->toString().c_str());
  auto ret = mpStreamConfig->add(pItem);
  pthread_rwlock_unlock(&mRWLock);
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::updateConfig(
    PipelineBuilderImpl* pBuilder) {
  pthread_rwlock_wrlock(&mRWLock);
  //
  NodeSet const& rootNodes = pBuilder->mRootNodes;
  NodeEdgeSet const& edges = pBuilder->mNodeEdges;
  //
  MY_LOGD("root node size = %zu", rootNodes.size());
  //
  // check if nodes exist
  NodeConfig const* pNodeConfig = mpNodeConfig.get();
  for (size_t i = 0; i < edges.size(); i++) {
    NodeId_T const src = edges[i].src;
    NodeId_T const dst = edges[i].dst;
    if (pNodeConfig->queryNode(src) == nullptr) {
      MY_LOGE("cannot find node %#" PRIxPTR " from configuration", src);
      pthread_rwlock_unlock(&mRWLock);
      return NAME_NOT_FOUND;
    }
    if (pNodeConfig->queryNode(dst) == nullptr) {
      MY_LOGE("cannot find node %#" PRIxPTR " from configuration", dst);
      pthread_rwlock_unlock(&mRWLock);
      return NAME_NOT_FOUND;
    }
  }
  // update to context
  mpPipelineConfig->setRootNode(rootNodes);
  mpPipelineConfig->setNodeEdges(edges);
  //
  pthread_rwlock_unlock(&mRWLock);
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<IPipelineFrame>
PipelineContext::PipelineContextImpl::constructRequest(
    RequestBuilderImpl* pBuilder, MUINT32 const requestNo) {
  // to make sure onRequestConstructed() can be called when this function
  // returns
  class scopedVar {
   public:
    explicit scopedVar(RequestBuilderImpl* rpBuilder) : mpBuilder(rpBuilder) {}
    ~scopedVar() { mpBuilder->onRequestConstructed(); }

   private:
    RequestBuilderImpl* const mpBuilder;
  } _localVar(pBuilder);
  //
  pthread_rwlock_rdlock(&mRWLock);
  //
  typedef IPipelineBufferSetFrameControl PipelineFrameT;
  //
  MBOOL const& bReprocessFrame = pBuilder->mbReprocessFrame;
  NodeIOMaps const& aImageIOMaps = pBuilder->mImageNodeIOMaps;
  NodeIOMaps const& aMetaIOMaps = pBuilder->mMetaNodeIOMaps;
  NodeEdgeSet const& aEdges = pBuilder->mNodeEdges;
  NodeSet const& aRoots = pBuilder->mRootNodes;
  //
  std::weak_ptr<AppCallbackT> const& aAppCallback = pBuilder->mpCallback;
  ImageStreamInfoMapT const& aReplacingInfos = pBuilder->mReplacingInfos;
  //
  if (!mpFrameNumberGenerator) {
    MY_LOGE("cannot get frameNo generator");
    pthread_rwlock_unlock(&mRWLock);
    return nullptr;
  }
  //
  uint32_t const frameNo = mpFrameNumberGenerator->generateFrameNo();
  // DEBUG
  int Enable_Dump = property_get_int32("vendor.pipeline.request.dump", 0);
  if (Enable_Dump) {
    pBuilder->dump(requestNo, frameNo);
  }
  //
#define MY_FUNC_ASSERT(expected, _ret_) \
  do {                                  \
    MERROR ret = (_ret_);               \
    if (ret != expected) {              \
      MY_LOGE("ret %d", ret);           \
      pthread_rwlock_unlock(&mRWLock);  \
      return nullptr;                   \
    }                                   \
  } while (0)
  //
  //
  std::shared_ptr<PipelineFrameT> pFrame =
      PipelineFrameT::create(requestNo, frameNo, bReprocessFrame,
                             aAppCallback,    // IAppCallback
                             mpStreamConfig,  //  IPipelineStreamBufferProvider
                             mpDispatcher);   // IPipelineNodeCallback
  //
  if (!pFrame) {
    MY_LOGE("cannot create PipelineFrame");
    pthread_rwlock_unlock(&mRWLock);
    return nullptr;
  }
  //
  pFrame->startConfiguration();
  //
  // collect all used nodes/streams from NodeIOMaps
  StreamSet reqImgStreams;
  StreamSet reqMetaStreams;
  //
  collect_from_NodeIOMaps().getStreamSet(aImageIOMaps, &reqImgStreams);
  collect_from_NodeIOMaps().getStreamSet(aMetaIOMaps, &reqMetaStreams);

  // get StreamId <-> type & (IImageStreamInfo or IMetaStreamInfo)
  struct {
    ImageStreamInfoMapT vAppImageStreamInfo;
    ImageStreamInfoMapT vHalImageStreamInfo;
    MetaStreamInfoMapT vAppMetaStreamInfo;
    MetaStreamInfoMapT vHalMetaStreamInfo;
  } aRequestData;
  //
  {
    collect_from_stream_config::Params params = {
        .pStreamConfig = mpStreamConfig.get(),
        .pvImageStreamSet = &reqImgStreams,
        .pvMetaStreamSet = &reqMetaStreams,
        .pvAppImageStreamInfo = &aRequestData.vAppImageStreamInfo,
        .pvHalImageStreamInfo = &aRequestData.vHalImageStreamInfo,
        .pvAppMetaStreamInfo = &aRequestData.vAppMetaStreamInfo,
        .pvHalMetaStreamInfo = &aRequestData.vHalMetaStreamInfo};
    MY_FUNC_ASSERT(NSCam::OK, collect_from_stream_config()(params));
  }
  // replace IImageStreamInfo:
  //      update run-time modified IStreamInfo to this request IStreamInfoSet.
  //      Then, following operations could query IStreamInfo from this if
  //      necessary.
  for (auto& i : aReplacingInfos) {
    std::shared_ptr<IImageStreamInfo> pInfo = i.second;
    auto idx = aRequestData.vHalImageStreamInfo.find(pInfo->getStreamId());
    if (idx == aRequestData.vHalImageStreamInfo.end()) {
      MY_LOGE("cannot replace IImageStreamInfo for stream %#" PRIx64,
              pInfo->getStreamId());
      pthread_rwlock_unlock(&mRWLock);
      return nullptr;
    }
    MY_LOGD("replace stream %#" PRIx64, pInfo->getStreamId());
    idx->second = pInfo;
  }
  //
  std::shared_ptr<IPipelineDAG> pReqDAG =
      constructDAG(mpPipelineDAG.get(), aRoots, aEdges);
  if (!pReqDAG) {
    pthread_rwlock_unlock(&mRWLock);
    return nullptr;
  }
  //
  //
  std::shared_ptr<IStreamInfoSetControl> pReqStreamInfoSet;
  {
    std::shared_ptr<IStreamInfoSetControl> pStreamInfoSet =
        IStreamInfoSetControl::create();
    //
    update_streaminfo_to_set::Params params = {
        .pvAppImageStreamInfo = &aRequestData.vAppImageStreamInfo,
        .pvHalImageStreamInfo = &aRequestData.vHalImageStreamInfo,
        .pvAppMetaStreamInfo = &aRequestData.vAppMetaStreamInfo,
        .pvHalMetaStreamInfo = &aRequestData.vHalMetaStreamInfo,
        .pSetControl = pStreamInfoSet.get()};
    MY_FUNC_ASSERT(NSCam::OK, update_streaminfo_to_set()(params));
    //
    pReqStreamInfoSet = pStreamInfoSet;
  }
  //
  //
  std::shared_ptr<IPipelineFrameNodeMapControl> pReqFrameNodeMap;
  {
    std::shared_ptr<IPipelineFrameNodeMapControl> pFrameNodeMap =
        IPipelineFrameNodeMapControl::create();
    construct_FrameNodeMapControl::Params params = {
        .pImageNodeIOMaps = &aImageIOMaps,
        .pMetaNodeIOMaps = &aMetaIOMaps,
        .pReqDAG = pReqDAG.get(),
        .pReqStreamInfoSet = pReqStreamInfoSet.get(),
        .pMapControl = pFrameNodeMap.get()};
    MY_FUNC_ASSERT(NSCam::OK, construct_FrameNodeMapControl()(params));
    //
    pReqFrameNodeMap = pFrameNodeMap;
  }
  //
  // update stream buffer
  MY_FUNC_ASSERT(NSCam::OK, update_streambuffers_to_frame().updateAppMetaSB(
                                aRequestData.vAppMetaStreamInfo,
                                pBuilder->mStreamBuffers_Meta, pFrame.get()));
  MY_FUNC_ASSERT(NSCam::OK,
                 update_streambuffers_to_frame().updateHalMetaSB(
                     aRequestData.vHalMetaStreamInfo,
                     pBuilder->mStreamBuffers_HalMeta, pFrame.get()));
  MY_FUNC_ASSERT(NSCam::OK, update_streambuffers_to_frame().updateAppImageSB(
                                aRequestData.vAppImageStreamInfo,
                                pBuilder->mStreamBuffers_Image, pFrame.get()));
  MY_FUNC_ASSERT(NSCam::OK,
                 update_streambuffers_to_frame().updateHalImageSB(
                     aRequestData.vHalImageStreamInfo,
                     pBuilder->mStreamBuffers_HalImage, pFrame.get()));
  //
  // userGraph of each stream buffer
  {
    evaluate_buffer_users::Params params = {.pProvider = mpNodeConfig.get(),
                                            .pPipelineDAG = pReqDAG.get(),
                                            .pNodeMap = pReqFrameNodeMap.get(),
                                            .pBufferSet = pFrame.get()};
    MY_FUNC_ASSERT(NSCam::OK, evaluate_buffer_users()(&params));
  }
  //
  pFrame->setPipelineNodeMap(mpPipelineNodeMap);
  pFrame->setNodeMap(pReqFrameNodeMap);
  pFrame->setPipelineDAG(pReqDAG);
  pFrame->setStreamInfoSet(pReqStreamInfoSet);
  //
  pFrame->finishConfiguration();
  //
  pthread_rwlock_unlock(&mRWLock);
  return pFrame;
#undef MY_FUNC_ASSERT
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::config(PipelineContextImpl* pOldContext,
                                             MBOOL const isAsync) {
  pthread_rwlock_wrlock(&mRWLock);
  //
  MERROR err;
  // get FrameNumberGenerator only in node reuse case
  if (pOldContext != nullptr) {
    mpFrameNumberGenerator = pOldContext->mpFrameNumberGenerator;
  }
  MY_LOGD_IF(mpFrameNumberGenerator, "FrameNumberGenerator(%p)",
             mpFrameNumberGenerator.get());
  if (!mpFrameNumberGenerator) {
    mpFrameNumberGenerator = IPipelineFrameNumberGenerator::create();
  }
  //
  {
    std::shared_ptr<IPipelineDAG> pDAG;
    pDAG.reset(IPipelineDAG::create());
    std::shared_ptr<IPipelineNodeMapControl> pNodeMap;
    pNodeMap.reset(IPipelineNodeMapControl::create());
    //
    config_pipeline::Params param = {.pStreamConfig = mpStreamConfig.get(),
                                     .pNodeConfig = mpNodeConfig.get(),
                                     .pPipelineConfig = mpPipelineConfig.get(),
                                     .pDAG = pDAG.get(),
                                     .pNodeMap = pNodeMap.get()};
    if (NSCam::OK != (err = config_pipeline()(param))) {
      MY_LOGE("config_pipeline err:%d(%s)", err, ::strerror(-err));
      pthread_rwlock_unlock(&mRWLock);
      return err;
    }
    //
    mpPipelineDAG = pDAG;
    mpPipelineNodeMap = pNodeMap;
  }
  // in-flight
  if (mpDispatcher.expired()) {
    mpDispatcher_Default = DefaultDispatcher::create();
    mpDispatcher = mpDispatcher_Default;
    mpDispatcher_Default->setDataCallback(mpDataCallback);
  }
  //
  mpInFlightRequest = std::make_shared<InFlightRequest>();
  //
  // config each node
  {
    std::vector<std::future<MERROR>> vFutures;
    //
    std::vector<IPipelineDAG::NodeObj_T> const& rToposort =
        mpPipelineDAG->getToposort();
    std::vector<IPipelineDAG::NodeObj_T>::const_iterator it = rToposort.begin();
    for (; it != rToposort.end(); it++) {
      std::shared_ptr<ContextNode> pContextNode =
          mpNodeConfig->queryNode(it->id);
      std::shared_ptr<INodeActor> pIActor =
          pContextNode ? pContextNode->getNodeActor() : nullptr;
      if (!pIActor) {
        MY_LOGE("cannnot find node %#" PRIxPTR " from Node Config", it->id);
        pthread_rwlock_unlock(&mRWLock);
        return UNKNOWN_ERROR;
      }
      //
      struct job {
        static MERROR execute(std::shared_ptr<INodeActor> pIActor) {
          MERROR err;
          if (NSCam::OK != (err = pIActor->init())) {
            return err;
          }
          err = pIActor->config();
          return err;
        }
      };
      //
      vFutures.push_back(
          std::async(isAsync ? std::launch::async : std::launch::deferred,
                     &job::execute, pIActor));
    }
    //
    for (auto& fut : vFutures) {
      MERROR result = fut.get();
      if (result != NSCam::OK) {
        err = result;
      }
    }
    //
    std::shared_ptr<IPipelineNodeMap> pPipelineNodeMap = mpPipelineNodeMap;
    std::shared_ptr<INodeCallbackToPipeline> pCallback = shared_from_this();
    it = rToposort.begin();
    for (; it != rToposort.end(); it++) {
      std::shared_ptr<IPipelineNode> pNode = pPipelineNodeMap->nodeAt(it->val);
      if (pNode == nullptr) {
        MY_LOGE("nullptr node (id:%" PRIxPTR ")", it->id);
        continue;
      }
      //
      if (pNode->setNodeCallBack(pCallback) != NSCam::OK) {
        MY_LOGE("Fail to setcallback to node (id:%" PRIxPTR ")", it->id);
      }
    }
  }
  //
  pthread_rwlock_unlock(&mRWLock);
  return err;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::queue(
    std::shared_ptr<IPipelineFrame> const& pFrame) {
  pthread_rwlock_rdlock(&mRWLock);
  if (!mpInFlightRequest || mpDispatcher.expired()) {
    MY_LOGE("not configured yet!");
    pthread_rwlock_unlock(&mRWLock);
    return UNKNOWN_ERROR;
  }
  mpInFlightRequest->registerRequest(pFrame);
  //
  std::shared_ptr<IPipelineNodeMap const> pPipelineNodeMap =
      pFrame->getPipelineNodeMap();
  if (pPipelineNodeMap == nullptr || pPipelineNodeMap->isEmpty()) {
    MY_LOGE("[frameNo:%d] Bad PipelineNodeMap:%p", pFrame->getFrameNo(),
            pPipelineNodeMap.get());
    pthread_rwlock_unlock(&mRWLock);
    return DEAD_OBJECT;
  }
  //
  {
    std::lock_guard<std::mutex> _l(mLastFrameLock);
    mpLastFrame = pFrame;
  }
  //
  {
    // only wait for the regular kick-processing
    std::lock_guard<std::mutex> _l(mKickLock);
  }
  //
  MERROR err = OK;
  std::vector<IPipelineDAG::NodeObj_T> const RootNodeObjSet =
      pFrame->getPipelineDAG().getRootNode();
  std::vector<std::shared_ptr<IPipelineNode>> vspPipelineNode;
  {
    pthread_rwlock_rdlock(&mFlushLock);
    for (size_t i = 0; i < RootNodeObjSet.size(); i++) {
      std::shared_ptr<IPipelineNode> pNode =
          pPipelineNodeMap->nodeAt(RootNodeObjSet[i].val);
      if (pNode == 0) {
        MY_LOGE("[frameNo:%d] Bad root node", pFrame->getFrameNo());
        pthread_rwlock_unlock(&mFlushLock);
        pthread_rwlock_unlock(&mRWLock);
        return DEAD_OBJECT;
      }
      if (mInFlush) {
        err = pNode->flush(pFrame);
      } else {
        // check enque need pipeline blocking or not
        err = pNode->queue(pFrame);
        if (err == FAILED_TRANSACTION) {
          vspPipelineNode.push_back(pNode);
          MY_LOGD("[frameNo:%d] enque to root node: %" PRIxPTR " need blocking",
                  pFrame->getFrameNo(), pNode->getNodeId());
        } else if (err != NSCam::OK) {
          MY_LOGE("[frameNo:%d] enque to root node: %" PRIxPTR " fail(%d)",
                  pFrame->getFrameNo(), pNode->getNodeId(), err);
          pthread_rwlock_unlock(&mFlushLock);
          pthread_rwlock_unlock(&mRWLock);
          return err;
        } else {
          MY_LOGD("[frameNo:%d] enque to root node: %" PRIxPTR " success",
                  pFrame->getFrameNo(), pNode->getNodeId());
        }
      }
    }
    pthread_rwlock_unlock(&mFlushLock);
  }

  while (vspPipelineNode.size() != 0) {
    {
      // blocking and wait event to enque (wait 33ms and check again)
      std::unique_lock<std::mutex> lock(mEnqueLock);
      mCondEnque.wait_for(lock, std::chrono::nanoseconds(33000000));
    }

    {
      // wake up and enque to root node which can not be enqued last time
      pthread_rwlock_rdlock(&mFlushLock);
      std::vector<std::shared_ptr<IPipelineNode>>::iterator it =
          vspPipelineNode.begin();
      while (it != vspPipelineNode.end()) {
        if (mInFlush) {
          err = (*it)->flush(pFrame);
        } else {
          err = (*it)->queue(pFrame);
        }
        if (err == FAILED_TRANSACTION) {
          MY_LOGD("[frameNo:%d] enque to root node: %" PRIxPTR " need blocking",
                  pFrame->getFrameNo(), (*it)->getNodeId());
          it++;
        } else if (err != NSCam::OK) {
          MY_LOGE("[frameNo:%d] enque to root node: %" PRIxPTR " fail(%d)",
                  pFrame->getFrameNo(), (*it)->getNodeId(), err);
          pthread_rwlock_unlock(&mFlushLock);
          pthread_rwlock_unlock(&mRWLock);
          return err;
        } else {
          MY_LOGD(
              "[frameNo:%d] enque success, erase root node record: id = "
              "%#" PRIxPTR " ",
              pFrame->getFrameNo(), (*it)->getNodeId());
          it = vspPipelineNode.erase(it);
        }
      }
      pthread_rwlock_unlock(&mFlushLock);
    }
  }
  //
  pthread_rwlock_unlock(&mRWLock);
  return err;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::kick(
    std::shared_ptr<IPipelineFrame> const& pFrame) {
  pthread_rwlock_rdlock(&mRWLock);
  std::shared_ptr<IPipelineNodeMap const> pPipelineNodeMap =
      pFrame->getPipelineNodeMap();
  if (pPipelineNodeMap == nullptr || pPipelineNodeMap->isEmpty()) {
    MY_LOGE("[frameNo:%d] Bad PipelineNodeMap:%p", pFrame->getFrameNo(),
            pPipelineNodeMap.get());
    pthread_rwlock_unlock(&mRWLock);
    return DEAD_OBJECT;
  }
  //
  std::vector<IPipelineDAG::NodeObj_T> const RootNodeObjSet =
      pFrame->getPipelineDAG().getRootNode();
  //
  MERROR err = NSCam::OK;
  {
    std::lock_guard<std::mutex> _l(mKickLock);
    {
      pthread_rwlock_rdlock(&mFlushLock);
      if (mInFlush) {  // do-nothing
        MY_LOGD("[frameNo:%d] kick but flushing", pFrame->getFrameNo());
      } else {
        for (size_t i = 0; i < RootNodeObjSet.size(); i++) {
          std::shared_ptr<IPipelineNode> pNode =
              pPipelineNodeMap->nodeAt(RootNodeObjSet[i].val);
          if (pNode == nullptr) {
            MY_LOGE("[frameNo:%d] Bad root node", pFrame->getFrameNo());
            pthread_rwlock_unlock(&mFlushLock);
            pthread_rwlock_unlock(&mRWLock);
            return DEAD_OBJECT;
          }

          MY_LOGD("[frameNo:%d] kick begin", pFrame->getFrameNo());
          err = pNode->kick();
          MY_LOGD("[frameNo:%d] kick end", pFrame->getFrameNo());
        }
      }
      pthread_rwlock_unlock(&mFlushLock);
    }
  }
  //
  pthread_rwlock_unlock(&mRWLock);
  return err;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::waitUntilDrained() {
  pthread_rwlock_rdlock(&mRWLock);
  if (mpInFlightRequest.get()) {
    mpInFlightRequest->waitUntilDrained();
  } else {
    MY_LOGD("may not configured yet");
  }
  pthread_rwlock_unlock(&mRWLock);
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::waitUntilNodeDrained(
    NodeId_T const nodeId) {
  pthread_rwlock_rdlock(&mRWLock);
  if (mpInFlightRequest.get()) {
    mpInFlightRequest->waitUntilNodeDrained(nodeId);
  } else {
    MY_LOGD("may not configured yet");
  }
  pthread_rwlock_unlock(&mRWLock);
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::beginFlush() {
  FUNC_START;
  {
    std::lock_guard<std::mutex> _l(mLastFrameLock);
    std::shared_ptr<IPipelineFrame> pLastFrame = mpLastFrame.lock();
    if (pLastFrame != nullptr) {
      kick(pLastFrame);
    } else {
      MY_LOGI("cannot promote LastFrame");
    }
  }
  //
  {
    pthread_rwlock_wrlock(&mFlushLock);
    mInFlush = MTRUE;
    pthread_rwlock_unlock(&mFlushLock);
  }
  //
  {
    std::shared_ptr<IDispatcher> pDispatcher = mpDispatcher.lock();
    if (pDispatcher.get()) {
      pDispatcher->beginFlush();
    } else {
      MY_LOGW("cannot promote dispatcher");
    }
  }
  //
  {
    pthread_rwlock_rdlock(&mRWLock);
    //
    std::shared_ptr<IPipelineNodeMap> pPipelineNodeMap = mpPipelineNodeMap;
    std::vector<IPipelineDAG::NodeObj_T> const& rToposort =
        mpPipelineDAG->getToposort();
    std::vector<IPipelineDAG::NodeObj_T>::const_iterator it = rToposort.begin();
    for (; it != rToposort.end(); it++) {
      std::shared_ptr<IPipelineNode> pNode = pPipelineNodeMap->nodeAt(it->val);
      if (pNode == nullptr) {
        MY_LOGE("nullptr node (id:%" PRIxPTR ")", it->id);
        continue;
      }
      //
      if (pNode->flush() != NSCam::OK) {
        MY_LOGE("Fail to flush node (id:%" PRIxPTR ")", it->id);
      }
    }
    pthread_rwlock_unlock(&mRWLock);
  }
  //
  FUNC_END;
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::endFlush() {
  FUNC_START;
  //
  {
    std::shared_ptr<IDispatcher> pDispatcher = mpDispatcher.lock();
    if (pDispatcher) {
      pDispatcher->endFlush();
    } else {
      MY_LOGW("cannot promote dispatcher");
    }
  }
  //
  {
    pthread_rwlock_wrlock(&mFlushLock);
    mInFlush = MFALSE;
    pthread_rwlock_unlock(&mFlushLock);
  }
  //
  FUNC_END;
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::setScenarioControl(
    std::shared_ptr<IScenarioControl> pControl) {
  if (mpScenarioControl) {
    MY_LOGW("mpScenarioControl already existed");
  }
  mpScenarioControl = pControl;
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::setDispatcher(
    std::weak_ptr<IDispatcher> pDispatcher) {
  MY_LOGD("set dispatcher %d", pDispatcher.expired());
  mpDispatcher = pDispatcher;
  std::shared_ptr<IDispatcher> spDispatcher = mpDispatcher.lock();
  if (spDispatcher) {
    spDispatcher->setDataCallback(mpDataCallback);
  }
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
PipelineContext::PipelineContextImpl::setDataCallback(
    std::weak_ptr<IDataCallback> pCallback) {
  MY_LOGD("set DataCallback %d", pCallback.expired());
  std::shared_ptr<IDispatcher> pDispatcher = mpDispatcher.lock();
  if (pDispatcher) {
    pDispatcher->setDataCallback(pCallback);
  }
  mpDataCallback = pCallback;
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<HalImageStreamBufferPoolT>
PipelineContext::PipelineContextImpl::queryImageStreamPool(
    StreamId_T const streamId) const {
  pthread_rwlock_rdlock(&mRWLock);
  auto ret = mpStreamConfig->queryImage(streamId)->pPool;
  pthread_rwlock_unlock(&mRWLock);
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<INodeActor> PipelineContext::PipelineContextImpl::queryNode(
    NodeId_T const nodeId) const {
  pthread_rwlock_rdlock(&mRWLock);
  std::shared_ptr<ContextNode> pContextNode = mpNodeConfig->queryNode(nodeId);
  auto ret = pContextNode ? pContextNode->getNodeActor() : nullptr;
  pthread_rwlock_unlock(&mRWLock);
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
uint32_t PipelineContext::PipelineContextImpl::getFrameNo() {
  pthread_rwlock_rdlock(&mRWLock);
  //
  uint32_t frameNo = 0;
  if (!mpFrameNumberGenerator.get()) {
    MY_LOGE("cannot get frameNo generator");
  } else {
    frameNo = mpFrameNumberGenerator->getFrameNo();
    MY_LOGD("frameNo:%d", frameNo);
  }
  //
  pthread_rwlock_unlock(&mRWLock);
  return frameNo;
}

/******************************************************************************
 *
 ******************************************************************************/
auto PipelineContext::PipelineContextImpl::dumpState(
    const std::vector<std::string>& options) -> void {
  std::shared_ptr<InFlightRequest> pInFlightRequest;
  {
    pthread_rwlock_rdlock(&mRWLock);
    pInFlightRequest = mpInFlightRequest;
    pthread_rwlock_unlock(&mRWLock);
  }
  if (pInFlightRequest != nullptr) {
    pInFlightRequest->dumpState(options);
  }
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
PipelineContext::PipelineContextImpl::onCallback(CallBackParams param) {
  std::lock_guard<std::mutex> lock(mEnqueLock);
  //
  MY_LOGD("param: (id:%" PRIxPTR "), (lastFrameNum = %d), (eNoticeType = %d)",
          param.nodeId, param.lastFrameNum, param.noticeType);
  if (param.noticeType == eNotice_ReadyToEnque) {
    mCondEnque.notify_one();
  }
  return;
}

MERROR
config_pipeline::operator()(Params const& rParams) {
  MERROR err = NSCam::OK;
  //
  StreamConfig const* pStreamConfig = rParams.pStreamConfig;
  NodeConfig const* pNodeConfig = rParams.pNodeConfig;
  PipelineConfig const* pPipelineConfig = rParams.pPipelineConfig;
  IPipelineDAG* pDAG = rParams.pDAG;
  IPipelineNodeMapControl* pNodeMap = rParams.pNodeMap;
  //
  if (!pStreamConfig || !pNodeConfig || !pPipelineConfig || !pDAG ||
      !pNodeMap) {
    MY_LOGE("NULL in params");
    return UNKNOWN_ERROR;
  }
  //
  ContextNodeMapT const& configNodeMap = pNodeConfig->getContextNodeMap();
  // nodes
  for (auto iter = configNodeMap.begin(); iter != configNodeMap.end(); ++iter) {
    struct copy_IStreamInfoSetControl {
      MVOID operator()(std::shared_ptr<IStreamInfoSetControl const> const& src,
                       std::shared_ptr<IStreamInfoSetControl> const& dst) {
        dst->editAppMeta() = src->getAppMeta();
        dst->editHalMeta() = src->getHalMeta();
        dst->editAppImage() = src->getAppImage();
        dst->editHalImage() = src->getHalImage();
      }
    };
    //
    std::shared_ptr<ContextNode> pNode = iter->second;
    //
    pDAG->addNode(pNode->getNodeId(),
                  std::distance(configNodeMap.begin(), iter));
    //
    std::shared_ptr<IPipelineNodeMapControl::INode> const pINode =
        pNodeMap->getNodeAt(
            pNodeMap->add(pNode->getNodeId(), pNode->getNode()));
    // in/out
    copy_IStreamInfoSetControl()(pNode->getInStreams(),
                                 pINode->editInStreams());
    copy_IStreamInfoSetControl()(pNode->getOutStreams(),
                                 pINode->editOutStreams());
  }
  {
    NodeSet const& roots = pPipelineConfig->getRootNode();
    NodeEdgeSet const& nodeEdges = pPipelineConfig->getNodeEdges();
    // edge
    std::vector<NodeEdge>::const_iterator iter = nodeEdges.begin();
    for (; iter != nodeEdges.end(); iter++) {
      err = pDAG->addEdge(iter->src, iter->dst);
      if (err != NSCam::OK) {
        return err;
      }
    }
    // root
    if (roots.size()) {
      pDAG->setRootNode(roots);
    } else {
      MY_LOGE("No RootNode!");
      return INVALID_OPERATION;
    }
  }
  //
  if (pDAG->getToposort().empty()) {
    MY_LOGE("It seems that the connection of nodes cannot from a DAG...");
    err = UNKNOWN_ERROR;
  }
  //
  return NSCam::OK;
}
/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<HalImageStreamBufferPoolT>
NSCam::v3::NSPipelineContext::createHalStreamBufferPool(
    const char* username, std::shared_ptr<IImageStreamInfo> pStreamInfo) {
  std::string const name =
      base::StringPrintf("%s:%s", username, pStreamInfo->getStreamName());
  //
  IImageStreamInfo::BufPlanes_t const& bufPlanes = pStreamInfo->getBufPlanes();
  size_t bufStridesInBytes[3] = {0};
  size_t bufBoundaryInBytes[3] = {0};
  size_t bufCustomSizeInBytes[3] = {0};
  size_t bufReusableSizeInBytes[3] = {0};
  for (size_t i = 0; i < bufPlanes.size(); i++) {
    bufStridesInBytes[i] = bufPlanes[i].rowStrideInBytes;
    bufCustomSizeInBytes[i] = bufPlanes[i].sizeInBytes;
  }
  std::shared_ptr<HalImageStreamBufferPoolT> pPool;

  if (pStreamInfo->getImgFormat() == eImgFmt_BLOB) {
    IImageBufferAllocator::ImgParam const imgParam(bufStridesInBytes[0],
                                                   bufBoundaryInBytes[0]);

    pPool = std::make_shared<HalImageStreamBufferPoolT>(
        name.c_str(), HalImageStreamBufferAllocatorT(pStreamInfo, imgParam));

  } else {
    IImageBufferAllocator::ImgParam const imgParam(
        pStreamInfo->getImgFormat(), pStreamInfo->getImgSize(),
        bufStridesInBytes, bufBoundaryInBytes, bufCustomSizeInBytes,
        bufReusableSizeInBytes, bufPlanes.size());

    pPool = std::make_shared<HalImageStreamBufferPoolT>(
        name.c_str(), HalImageStreamBufferAllocatorT(pStreamInfo, imgParam));
  }

  if (pPool == nullptr) {
    MY_LOGE("Fail to new a image pool:%s", name.c_str());
    return nullptr;
  }
  //
  MERROR err =
      pPool->initPool(pStreamInfo->getStreamName(), pStreamInfo->getMaxBufNum(),
                      pStreamInfo->getMinInitBufNum());
  if (NSCam::OK != err) {
    MY_LOGE("%s: initPool err:%d(%s)", name.c_str(), err, ::strerror(-err));
    return nullptr;
  }
  if (NSCam::OK != pPool->commitPool(username)) {
    MY_LOGE("%s: commitPool err:%d(%s)", name.c_str(), err, ::strerror(-err));
    return nullptr;
  }
  //
  return pPool;
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
collect_from_NodeIOMaps::getStreamSet(NodeIOMaps const& nodeIOMap,
                                      StreamSet* collected) {
  for (auto& i : nodeIOMap) {
    IOMapSet const& IOMapSet = i.second;
    IOMapSet::const_iterator pIOMap = IOMapSet.begin();
    for (; pIOMap != IOMapSet.end(); pIOMap++) {
      collected->add(pIOMap->vIn);
      collected->add(pIOMap->vOut);
    }
  }
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<IPipelineDAG> NSCam::v3::NSPipelineContext::constructDAG(
    IPipelineDAG const* pConfigDAG,
    NodeSet const& rootNodes,
    NodeEdgeSet const& edges) {
  NodeSet requestNodeSet;
  {
    NodeEdgeSet::const_iterator iter = edges.begin();
    for (; iter != edges.end(); iter++) {
      requestNodeSet.add(iter->src);
      requestNodeSet.add(iter->dst);
    }
    //
    NodeSet::const_iterator iterNode = rootNodes.begin();
    for (; iterNode != rootNodes.end(); iterNode++) {
      requestNodeSet.add(*iterNode);
    }
  }

  //
  std::shared_ptr<IPipelineDAG> pDAG;
  pDAG.reset(IPipelineDAG::create());
  for (size_t i = 0; i < requestNodeSet.size(); i++) {
    NodeId_T const nodeId = requestNodeSet[i];
    IPipelineDAG::NodeObj_T obj = pConfigDAG->getNode(nodeId);
    if (obj.val == -1) {  // invalid
      MY_LOGE("cannot find node %#" PRIxPTR, nodeId);
      return nullptr;
    }
    pDAG->addNode(nodeId, obj.val);
  }
  // set root
  if (NSCam::OK != pDAG->setRootNode(rootNodes)) {
    MY_LOGE("set root node failed");
    return nullptr;
  }
  // set edges
  {
    std::vector<NodeEdge>::const_iterator iter = edges.begin();
    for (; iter != edges.end(); iter++) {
      if (NSCam::OK != pDAG->addEdge(iter->src, iter->dst)) {
        return nullptr;
      }
    }
  }
  //
  if (pDAG->getToposort().empty()) {
    MY_LOGE("It seems that the connection of nodes cannot from a DAG...");
    return nullptr;
  }
  //
  return pDAG;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
set_streaminfoset_from_config::operator()(Params const& rParams) {
  StreamSet const* pStreamSet = rParams.pStreamSet;
  StreamConfig const* pStreamConfig = rParams.pStreamConfig;
  IStreamInfoSetControl* pSetControl = rParams.pSetControl;
  //
  for (size_t i = 0; i < pStreamSet->size(); i++) {
    StreamId_T const streamId = pStreamSet->at(i);

#define search_then_add(_type_)                                \
  {                                                            \
    std::shared_ptr<StreamConfig::Item##_type_##Stream> item = \
        pStreamConfig->query##_type_(streamId);                \
    if (item.get()) {                                          \
      if (BehaviorOf(item->type) == eBehavior_HAL) {           \
        pSetControl->editHal##_type_().addStream(item->pInfo); \
      } else {                                                 \
        pSetControl->editApp##_type_().addStream(item->pInfo); \
      }                                                        \
      continue;                                                \
    }                                                          \
  }

    // search from configured images, then from configured meta
    search_then_add(Image);
    search_then_add(Meta);
#undef search_then_add
    //
    MY_LOGE("cannot find stream(%#" PRIx64 ") from configuration", streamId);
    MY_LOGW("=== dump configuration begin ===");
    pStreamConfig->dump();
    MY_LOGW("=== dump configuration end ===");
    return NAME_NOT_FOUND;
  }
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
collect_from_stream_config::operator()(Params const& rParams) {
  struct impl {
#define impl_query(_type_)                                                    \
  MERROR query(StreamConfig const* rpStreamConfig, StreamSet const* pStreams, \
               _type_##StreamInfoMapT* pvAppInfos,                            \
               _type_##StreamInfoMapT* pvHalInfos) {                          \
    if (rpStreamConfig == nullptr)                                            \
      return UNKNOWN_ERROR;                                                   \
    for (size_t i = 0; i < pStreams->size(); i++) {                           \
      StreamId_T const streamId = pStreams->at(i);                            \
      std::shared_ptr<StreamConfig::Item##_type_##Stream> pItem =             \
          rpStreamConfig->query##_type_(streamId);                            \
      if (!pItem.get()) {                                                     \
        MY_LOGE("cannot find %s stream %#" PRIx64 "", #_type_, streamId);     \
        return BAD_VALUE;                                                     \
      }                                                                       \
      if (BehaviorOf(pItem->type) == eBehavior_APP) {                         \
        pvAppInfos->emplace(streamId, pItem->pInfo);                          \
      } else if (BehaviorOf(pItem->type) == eBehavior_HAL) {                  \
        pvHalInfos->emplace(streamId, pItem->pInfo);                          \
      } else {                                                                \
        MY_LOGE("should not happen");                                         \
        return UNKNOWN_ERROR;                                                 \
      }                                                                       \
    }                                                                         \
    return NSCam::OK;                                                         \
  }
    //
    impl_query(Image) impl_query(Meta)
#undef impl_query
  };
  //
  MERROR err = NSCam::OK;
  err =
      impl().query(rParams.pStreamConfig, rParams.pvImageStreamSet,
                   rParams.pvAppImageStreamInfo, rParams.pvHalImageStreamInfo);
  if (err != NSCam::OK) {
    return err;
  }
  //
  err = impl().query(rParams.pStreamConfig, rParams.pvMetaStreamSet,
                     rParams.pvAppMetaStreamInfo, rParams.pvHalMetaStreamInfo);
  return err;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
update_streaminfo_to_set::operator()(Params const& rParams) {
#define updateInfo(_name_, _type_, pStreamInfoMap)                       \
  do {                                                                   \
    IStreamInfoSetControl::Map<I##_type_##StreamInfo>& map =             \
        pSetControl->edit##_name_##_type_();                             \
    for (auto it = pStreamInfoMap->begin(); it != pStreamInfoMap->end(); \
         ++it) {                                                         \
      map.addStream(it->second);                                         \
      if (FRAME_STREAMINFO_DEBUG_ENABLE) {                               \
        std::string str = it->second->toString();                        \
        MY_LOGD("update info: %s", str.c_str());                         \
      }                                                                  \
    }                                                                    \
  } while (0)

  IStreamInfoSetControl* pSetControl = rParams.pSetControl;
  if (!pSetControl) {
    return UNKNOWN_ERROR;
  }

  updateInfo(App, Image, rParams.pvAppImageStreamInfo);
  updateInfo(Hal, Image, rParams.pvHalImageStreamInfo);
  updateInfo(App, Meta, rParams.pvAppMetaStreamInfo);
  updateInfo(Hal, Meta, rParams.pvHalMetaStreamInfo);

#undef updateInfo
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
update_streambuffers_to_frame::updateAppMetaSB(
    MetaStreamInfoMapT const& rvStreamInfo,
    MetaStreamBufferMapsT const& rvSBuffers,
    PipelineFrameT* pFrame) const {
  typedef IMetaStreamBuffer SBufferT;
  //
  std::vector<std::shared_ptr<SBufferT>> vStreamBuffers;
  for (auto& i : rvStreamInfo) {
    StreamId_T const streamId = i.first;
    std::shared_ptr<SBufferT> SBuffer = nullptr;
    if (rvSBuffers.find(streamId) != rvSBuffers.end()) {
      SBuffer = rvSBuffers.at(streamId);
    }
    if (!SBuffer) {  // allocate here
      std::shared_ptr<IMetaStreamInfo> pStreamInfo = i.second;
      SBuffer = HalMetaStreamBufferAllocatorT(pStreamInfo)();
    }
    //
    vStreamBuffers.push_back(SBuffer);
  }
  //
  auto pBufMap = pFrame->editMap_AppMeta();
  pBufMap->setCapacity(vStreamBuffers.size());
  for (size_t i = 0; i < vStreamBuffers.size(); i++) {
    MY_LOGD_IF(FRAMEE_STREAMBUFFER_DEBUG_ENABLE, "stream %#" PRIx64,
               vStreamBuffers[i]->getStreamInfo()->getStreamId());
    pBufMap->add(vStreamBuffers[i]);
  }
  //
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
update_streambuffers_to_frame::updateHalMetaSB(
    MetaStreamInfoMapT const& rvStreamInfo,
    HalMetaStreamBufferMapsT const& rvSBuffers,
    PipelineFrameT* pFrame) const {
  typedef HalMetaStreamBuffer SBufferT;
  //
  std::vector<std::shared_ptr<SBufferT>> vStreamBuffers;
  for (auto& i : rvStreamInfo) {
    StreamId_T const streamId = i.first;
    std::shared_ptr<SBufferT> SBuffer = nullptr;
    if (rvSBuffers.find(streamId) != rvSBuffers.end()) {
      SBuffer = rvSBuffers.at(streamId);
    }
    if (!SBuffer) {  // allocate here
      std::shared_ptr<IMetaStreamInfo> pStreamInfo = i.second;
      SBuffer = HalMetaStreamBufferAllocatorT(pStreamInfo)();
    }
    vStreamBuffers.push_back(SBuffer);
  }
  //
  auto pBufMap = pFrame->editMap_HalMeta();
  pBufMap->setCapacity(vStreamBuffers.size());
  for (size_t i = 0; i < vStreamBuffers.size(); i++) {
    MY_LOGD_IF(FRAMEE_STREAMBUFFER_DEBUG_ENABLE, "stream %#" PRIx64,
               vStreamBuffers[i]->getStreamInfo()->getStreamId());
    pBufMap->add(vStreamBuffers[i]);
  }
  //
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
update_streambuffers_to_frame::updateAppImageSB(
    ImageStreamInfoMapT const& rvStreamInfo,
    ImageStreamBufferMapsT const& rvSBuffers,
    PipelineFrameT* pFrame) const {
  if (rvStreamInfo.size() != rvSBuffers.size()) {
    MY_LOGE("collect rvStreamInfo size %zu != SB size %zu", rvStreamInfo.size(),
            rvSBuffers.size());
    return BAD_VALUE;
  }
  //
  auto pBufMap = pFrame->editMap_AppImage();
  //
  pBufMap->setCapacity(rvSBuffers.size());
  for (auto& it : rvSBuffers) {
    MY_LOGD_IF(FRAMEE_STREAMBUFFER_DEBUG_ENABLE, "stream %#" PRIx64,
               (it.second)->getStreamInfo()->getStreamId());
    pBufMap->add(it.second);
  }
  //
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
update_streambuffers_to_frame::updateHalImageSB(
    ImageStreamInfoMapT const& rvStreamInfo,
    HalImageStreamBufferMapsT const& vSBuffers,
    PipelineFrameT* pFrame) const {
  auto pBufMap = pFrame->editMap_HalImage();
  //
  pBufMap->setCapacity(rvStreamInfo.size());
  for (auto& i : rvStreamInfo) {
    MY_LOGD_IF(FRAMEE_STREAMBUFFER_DEBUG_ENABLE, "stream %#" PRIx64,
               (i.second)->getStreamId());
    std::shared_ptr<HalImageStreamBuffer> pBuffer = nullptr;
    if (vSBuffers.find((i.second)->getStreamId()) != vSBuffers.end()) {
      pBuffer = vSBuffers.at((i.second)->getStreamId());
    }
    if (pBuffer) {
      pBufMap->add(pBuffer);
    } else {
      pBufMap->add(i.second, nullptr);
    }
  }
  //
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
construct_FrameNodeMapControl::operator()(Params const& rParams) {
  typedef IPipelineFrameNodeMapControl FrameNodeMapT;
  //
  struct setINodeIOStreams {
    MVOID operator()(IOMapSet const& rImageIOMapSet,
                     IOMapSet const& rMetaIOMapSet,
                     IStreamInfoSet const* const pReqStreamInfoSet,
                     std::shared_ptr<FrameNodeMapT::INode> pNode) {
      typedef NSCam::v3::Utils::SimpleStreamInfoSetControl StreamInfoSetT;
      std::shared_ptr<StreamInfoSetT> pIStreams =
          std::make_shared<StreamInfoSetT>();
      std::shared_ptr<StreamInfoSetT> pOStreams =
          std::make_shared<StreamInfoSetT>();
      pNode->setIStreams(pIStreams);
      pNode->setOStreams(pOStreams);

#define setINodeIO(type, rIOMapSet)                                   \
  do {                                                                \
    IOMapSet::const_iterator it = rIOMapSet.begin();                  \
    for (; it != rIOMapSet.end(); it++) {                             \
      IPipelineFrame::type##InfoIOMap map;                            \
      for (size_t i = 0; i < it->vIn.size(); i++) {                   \
        StreamId_T const streamId = it->vIn[i];                       \
        std::shared_ptr<I##type##StreamInfo> pStreamInfo =            \
            pReqStreamInfoSet->get##type##InfoFor(streamId);          \
        map.vIn.emplace(streamId, pStreamInfo);                       \
        pIStreams->edit##type().emplace(streamId, pStreamInfo);       \
      }                                                               \
      for (size_t i = 0; i < it->vOut.size(); i++) {                  \
        StreamId_T const streamId = it->vOut[i];                      \
        std::shared_ptr<I##type##StreamInfo> pStreamInfo =            \
            pReqStreamInfoSet->get##type##InfoFor(streamId);          \
        map.vOut.emplace(streamId, pStreamInfo);                      \
        pOStreams->edit##type().emplace(streamId, pStreamInfo);       \
      }                                                               \
      pNode->editInfoIOMapSet().m##type##InfoIOMapSet.push_back(map); \
    }                                                                 \
  } while (0)

      setINodeIO(Image, rImageIOMapSet);
      setINodeIO(Meta, rMetaIOMapSet);

#undef setINodeIO
    }
    //
    MVOID dumpINodeIO(FrameNodeMapT::INode* pNode) {
      MY_LOGD("nodeId %#" PRIxPTR, pNode->getNodeId());
      InfoIOMapSet const& aIOMapSet = pNode->getInfoIOMapSet();
#define dump(type, rIOMapSet)                                         \
  do {                                                                \
    for (size_t idx = 0; idx < rIOMapSet.size(); idx++) {             \
      IPipelineFrame::type##InfoIOMap const& aIOMap = rIOMapSet[idx]; \
      std::string inStream, outStream;                                \
      for (auto& i : aIOMap.vIn) {                                    \
        inStream += base::StringPrintf("(%#" PRIx64 ")", i.first);    \
      }                                                               \
      for (auto& i : aIOMap.vOut) {                                   \
        outStream += base::StringPrintf("(%#" PRIx64 ")", i.first);   \
      }                                                               \
      MY_LOGD("%s #%zu", #type, idx);                                 \
      MY_LOGD("    In : %s", inStream.c_str());                       \
      MY_LOGD("    Out: %s", outStream.c_str());                      \
    }                                                                 \
  } while (0)
      dump(Image, aIOMapSet.mImageInfoIOMapSet);
      dump(Meta, aIOMapSet.mMetaInfoIOMapSet);
#undef dump
    }
  };
  //
  FrameNodeMapT* pNodeMap = rParams.pMapControl;
  //
  std::vector<IPipelineDAG::NodeObj_T> const& rToposort =
      rParams.pReqDAG->getToposort();
  std::vector<IPipelineDAG::NodeObj_T>::const_iterator it = rToposort.begin();
  for (; it != rToposort.end(); it++) {
    NodeId_T const nodeId = it->id;
    //
    auto pNode = pNodeMap->getNodeAt(pNodeMap->addNode(nodeId));
    //
    setINodeIOStreams()(rParams.pImageNodeIOMaps->at(nodeId),
                        rParams.pMetaNodeIOMaps->at(nodeId),
                        rParams.pReqStreamInfoSet, pNode);
    //
#if 0  // to do
       // debug
        if (FRAMENODEMAP_DEBUG_ENABLE) {
            setINodeIOStreams().dumpINodeIO(pNode);
        }
#endif
  }
  //
  return NSCam::OK;
}

/******************************************************************************
 *
 ******************************************************************************/
struct evaluate_buffer_users::Imp {
  typedef IPipelineFrameNodeMapControl FrameNodeMapT;
  typedef std::vector<IPipelineDAG::NodeObj_T> ToposortT;
  IPipelineDAG const* mpPipelineDAG;
  std::vector<IPipelineDAG::Edge> mvEdges;

  MERROR
  operator()(Params* rParams) {
    CAM_TRACE_NAME("evaluate_request_buffer_users");
    mpPipelineDAG = rParams->pPipelineDAG;
    mvEdges.clear();
    rParams->pPipelineDAG->getEdges(&mvEdges);
    //
    evaluateAppImage(*rParams);
    evaluateHalImage(*rParams);
    evaluateAppMeta(*rParams);
    evaluateHalMeta(*rParams);
    //
    return NSCam::OK;
  }

#define _IMP_EVALUATE_(_NAME_, _TYPE_)                                       \
  MERROR                                                                     \
  evaluate##_NAME_##_TYPE_(Params& rParams) {                                \
    MakeUser_##_NAME_##_TYPE_ makeUser(rParams.pProvider, rParams.pNodeMap); \
    doIt(makeUser, rParams.pBufferSet->editMap_##_NAME_##_TYPE_());          \
    return NSCam::OK;                                                        \
  }

  _IMP_EVALUATE_(App, Image);
  _IMP_EVALUATE_(App, Meta);
  _IMP_EVALUATE_(Hal, Image);
  _IMP_EVALUATE_(Hal, Meta);

#undef _IMP_EVALUATE_

  template <class MakeUserT, class MapT>
  MVOID doIt(MakeUserT const& makeUser, MapT pBufMap) {
    ToposortT const& rToposort = mpPipelineDAG->getToposort();
    for (size_t i = 0; i < pBufMap->size(); i++) {
      std::shared_ptr<IUsersManager> const& pUsersManager =
          pBufMap->usersManagerAt(i);

      // User graph of current buffer
      std::shared_ptr<IUsersManager::IUserGraph> userGraph =
          pUsersManager->createGraph();

      // Add users
      ToposortT::const_iterator user = rToposort.begin();
      do {
        userGraph->addUser(makeUser(pBufMap->streamInfoAt(i).get(), user->id));
        //
        user++;
      } while (user != rToposort.end());

      // Add edges
      for (size_t j = 0; j < mvEdges.size(); j++) {
        userGraph->addEdge(mvEdges.at(j).src, mvEdges.at(j).dst);
      }

      // Enqueue graph
      pUsersManager->enqueUserGraph(userGraph);
      pUsersManager->finishUserSetup();
    }
  }

  struct MakeUserBase {
    NodeConfig const* mpProvider;
    FrameNodeMapT const* mpNodeMap;

    IUsersManager::User makeImageUser(IImageStreamInfo const* pStreamInfo,
                                      NodeId_T const nodeId) const {
      StreamId_T const streamId = pStreamInfo->getStreamId();
      //
      IUsersManager::User user;
      user.mUserId = nodeId;
      //
      auto pNode = mpNodeMap->getNodeFor(nodeId);
      refineUser(&user, streamId, pNode->getOStreams()->getImageInfoMap(),
                 pNode->getIStreams()->getImageInfoMap());
      if (IUsersManager::Category::NONE != user.mCategory) {
        user.mUsage =
            mpProvider->queryMinimalUsage(nodeId, pStreamInfo->getStreamId());
      }
      //
      return user;
    }

    IUsersManager::User makeMetaUser(IMetaStreamInfo const* pStreamInfo,
                                     NodeId_T const nodeId) const {
      StreamId_T const streamId = pStreamInfo->getStreamId();
      //
      IUsersManager::User user;
      user.mUserId = nodeId;
      //
      auto pNode = mpNodeMap->getNodeFor(nodeId);
      refineUser(&user, streamId, pNode->getOStreams()->getMetaInfoMap(),
                 pNode->getIStreams()->getMetaInfoMap());
      //
      return user;
    }

    template <class StreamsT>
    MVOID refineUser(IUsersManager::User* rUser,
                     StreamId_T const streamId,
                     StreamsT const& pOStreams,
                     StreamsT const& pIStreams) const {
      if (pOStreams != 0 && pIStreams != 0) {
        if (0 <= pOStreams->indexOfKey(streamId)) {
          rUser->mCategory = IUsersManager::Category::PRODUCER;
          return;
        }
        //
        if (0 <= pIStreams->indexOfKey(streamId)) {
          rUser->mCategory = IUsersManager::Category::CONSUMER;
          return;
        }
        //
        MY_LOGD("streamId:%#" PRIx64 " nodeId:%#" PRIxPTR
                ": not found in IO streams",
                streamId, rUser->mUserId);
      } else {
        MY_LOGW("streamId:%#" PRIx64 " nodeId:%#" PRIxPTR
                ": no IO streams(%p,%p)",
                streamId, rUser->mUserId, pIStreams.get(), pOStreams.get());
      }
      //
      rUser->mCategory = IUsersManager::Category::NONE;
      rUser->mReleaseFence = rUser->mAcquireFence;
    }
  };

#define _DEFINE_MAKEUSER_(_NAME_, _TYPE_)                                    \
  struct MakeUser_##_NAME_##_TYPE_ : public MakeUserBase {                   \
    MakeUser_##_NAME_##_TYPE_(NodeConfig const* pProvider,                   \
                              FrameNodeMapT const* pNodeMap) {               \
      mpProvider = pProvider;                                                \
      mpNodeMap = pNodeMap;                                                  \
    }                                                                        \
                                                                             \
    IUsersManager::User operator()(I##_TYPE_##StreamInfo const* pStreamInfo, \
                                   NodeId_T const nodeId) const {            \
      return make##_TYPE_##User(pStreamInfo, nodeId);                        \
    }                                                                        \
  };

  _DEFINE_MAKEUSER_(App, Image);
  _DEFINE_MAKEUSER_(App, Meta);
  _DEFINE_MAKEUSER_(Hal, Image);
  _DEFINE_MAKEUSER_(Hal, Meta);

#undef _DEFINE_MAKEUSER_
};

/******************************************************************************
 *
 ******************************************************************************/
MERROR
evaluate_buffer_users::operator()(Params* rParams) {
  return Imp()(rParams);
}

/******************************************************************************
 *
 ******************************************************************************/
StreamConfig::~StreamConfig() {}

/******************************************************************************
 *
 ******************************************************************************/
MERROR
StreamConfig::acquireHalStreamBuffer(
    MUINT32 const requestNo,
    std::shared_ptr<IImageStreamInfo> const pStreamInfo,
    std::shared_ptr<HalImageStreamBuffer>* rpStreamBuffer) const {
  MERROR err = UNKNOWN_ERROR;
  StreamId_T const streamId = pStreamInfo->getStreamId();
  std::shared_ptr<ItemImageStream> item = queryImage(streamId);
  switch (HalBehaviorOf(item->type)) {
    case eBehavior_HAL_POOL: {
      std::shared_ptr<HalImageStreamBufferPoolT> pPool = item->pPool;
      //
      MY_LOGE_IF(pPool == 0,
                 "NULL HalImageStreamBufferPool - stream:%#" PRIx64 "(%s)",
                 streamId, pStreamInfo->getStreamName());
      //
      err = pPool == 0
                ? UNKNOWN_ERROR
                : pPool->acquireFromPool(pPool->poolName(), rpStreamBuffer,
                                         NSCam::Utils::s2ns(10));
      MY_LOGE_IF(
          NSCam::OK != err || *rpStreamBuffer == 0,
          "[acquireFromPool] err:%d(%s) pStreamBuffer:%p stream:%#" PRIx64
          "(%s)",
          err, ::strerror(-err), rpStreamBuffer->get(), streamId,
          pStreamInfo->getStreamName());
    } break;
    case eBehavior_HAL_PROVIDER: {
      std::shared_ptr<IStreamBufferProviderT> pProvider = item->pProvider;
      //
      MY_LOGE_IF(pProvider == 0,
                 "NULL HalImageStreamBufferProvider - stream:%#" PRIx64 "(%s)",
                 streamId, pStreamInfo->getStreamName());
      //
      std::shared_ptr<HalImageStreamBuffer> pStreamBuffer;
      err = pProvider == 0 ? UNKNOWN_ERROR
                           : pProvider->dequeStreamBuffer(
                                 requestNo, pStreamInfo, pStreamBuffer);
      *rpStreamBuffer = pStreamBuffer;
      //
      MY_LOGW(
          "[acquireFromProvider] err:%d(%s) pStreamBuffer:%p stream:%#" PRIx64
          "(%s)",
          err, ::strerror(-err), rpStreamBuffer->get(), streamId,
          pStreamInfo->getStreamName());
    } break;
    case eBehavior_HAL_RUNTIME: {
      std::string const str = base::StringPrintf(
          "%s StreamId:%#" PRIx64 " %dx%d %p %p", pStreamInfo->getStreamName(),
          pStreamInfo->getStreamId(), pStreamInfo->getImgSize().w,
          pStreamInfo->getImgSize().h, pStreamInfo.get(), item->pInfo.get());
      IImageStreamInfo::BufPlanes_t const& bufPlanes =
          pStreamInfo->getBufPlanes();
      size_t bufStridesInBytes[3] = {0};
      size_t bufBoundaryInBytes[3] = {0};
      for (size_t i = 0; i < bufPlanes.size(); i++) {
        bufStridesInBytes[i] = bufPlanes[i].rowStrideInBytes;
      }
      IImageBufferAllocator::ImgParam const imgParam(
          pStreamInfo->getImgFormat(), pStreamInfo->getImgSize(),
          bufStridesInBytes, bufBoundaryInBytes, bufPlanes.size());
      //
      *rpStreamBuffer = HalImageStreamBufferAllocatorT(pStreamInfo, imgParam)();
      err = rpStreamBuffer->get() ? NSCam::OK : UNKNOWN_ERROR;
      if (err != NSCam::OK) {
        MY_LOGE("Fail to allocate - %s", str.c_str());
      }
    } break;
    default:
      MY_LOGW("not supported type 0x%x stream:%#" PRIx64 "(%s)", item->type,
              streamId, pStreamInfo->getStreamName());
  }

  return err;
}

/******************************************************************************
 *
 ******************************************************************************/
void StreamConfig::dumpState() const {
  pthread_rwlock_rdlock(&mRWLock);

  pthread_rwlock_unlock(&mRWLock);
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
StreamConfig::dump() const {
  dumpState();
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
NodeConfig::addNode(NodeId_T const nodeId, std::shared_ptr<ContextNode> pNode) {
  pthread_rwlock_wrlock(&mRWLock);
  mConfig_NodeMap.emplace(nodeId, pNode);
  pthread_rwlock_unlock(&mRWLock);
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
NodeConfig::setImageStreamUsage(NodeId_T const nodeId,
                                StreamUsageMap const& usgMap) {
  pthread_rwlock_wrlock(&mRWLock);
  mNodeImageStreamUsage.emplace(nodeId, usgMap);
  pthread_rwlock_unlock(&mRWLock);
}

/******************************************************************************
 *
 ******************************************************************************/
std::shared_ptr<ContextNode> const NodeConfig::queryNode(
    NodeId_T const nodeId) const {
  pthread_rwlock_rdlock(&mRWLock);
  auto ret = mConfig_NodeMap.at(nodeId);
  pthread_rwlock_unlock(&mRWLock);
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
MUINT
NodeConfig::queryMinimalUsage(NodeId_T const nodeId,
                              StreamId_T const streamId) const {
  pthread_rwlock_rdlock(&mRWLock);
  auto index_node = mNodeImageStreamUsage.find(nodeId);
  if (index_node == mNodeImageStreamUsage.end()) {
    MY_LOGW("cannot find usage for (NodeId %#" PRIxPTR ", streamId %#" PRIx64
            ")",
            nodeId, streamId);
    pthread_rwlock_unlock(&mRWLock);
    return 0;
  }
  //
  StreamUsageMap const& pStreamUsgMap = index_node->second;
  auto index_stream = pStreamUsgMap.find(streamId);
  if (index_stream == pStreamUsgMap.end()) {
    MY_LOGW("cannot find usage for (NodeId %#" PRIxPTR ", streamId %#" PRIx64
            ")",
            nodeId, streamId);
    pthread_rwlock_unlock(&mRWLock);
    return 0;
  }
  //
  pthread_rwlock_unlock(&mRWLock);
  return index_stream->second;
}

/******************************************************************************
 *
 ******************************************************************************/
void NodeConfig::dumpState() const {
  struct dump {
    static std::string StreamInfo(
        std::map<StreamId_T, std::shared_ptr<IImageStreamInfo>> const& vector) {
      std::string os;
      for (auto& it : vector) {
        auto const streamId = it.first;
        os += base::StringPrintf("%#" PRIx64 " ", streamId);
      }
      return os;
    }
    static std::string StreamInfo(
        std::map<StreamId_T, std::shared_ptr<IMetaStreamInfo>> const& vector) {
      std::string os;
      for (auto& it : vector) {
        auto const streamId = it.first;
        os += base::StringPrintf("%#" PRIx64 " ", streamId);
      }
      return os;
    }
    static std::string StreamInfoSetControl(
        std::shared_ptr<const IStreamInfoSetControl> s) {
      std::string os;
      os += " .AppImage={ ";
      os += dump::StreamInfo(s->getAppImage());
      os += "}";
      os += " .HalImage={ ";
      os += dump::StreamInfo(s->getHalImage());
      os += "}";
      os += " .AppMeta={ ";
      os += dump::StreamInfo(s->getAppMeta());
      os += "}";
      os += " .HalMeta={ ";
      os += dump::StreamInfo(s->getHalMeta());
      os += "}";
      return os;
    }
  };

  pthread_rwlock_rdlock(&mRWLock);
  for (auto& i : mConfig_NodeMap) {
    std::shared_ptr<ContextNode> pNode = i.second;
  }
  pthread_rwlock_unlock(&mRWLock);
}

/******************************************************************************
 *
 ******************************************************************************/
void PipelineConfig::dumpState() const {
  std::string os;
  os += ".root={";
  for (size_t i = 0; i < mRootNodes.size(); i++) {
    os += base::StringPrintf(" %#" PRIxPTR " ", mRootNodes[i]);
  }
  os += "}";

  os += ", .edges={";
  for (size_t i = 0; i < mNodeEdges.size(); i++) {
    os += base::StringPrintf("(%#" PRIxPTR " -> %#" PRIxPTR ")",
                             mNodeEdges[i].src, mNodeEdges[i].dst);
  }
  os += "}";
}

/******************************************************************************
 *
 ******************************************************************************/
MVOID
DefaultDispatcher::onDispatchFrame(
    std::shared_ptr<IPipelineFrame> const& pFrame, Pipeline_NodeId_T nodeId) {
  std::shared_ptr<IPipelineNodeMap const> pPipelineNodeMap =
      pFrame->getPipelineNodeMap();
  if (pPipelineNodeMap == nullptr || pPipelineNodeMap->isEmpty()) {
    MY_LOGE("[frameNo:%d] Bad PipelineNodeMap:%p", pFrame->getFrameNo(),
            pPipelineNodeMap.get());
    return;
  }
  //
  IPipelineDAG::NodeObjSet_T nextNodes;
  MERROR err = pFrame->getPipelineDAG().getOutAdjacentNodes(nodeId, &nextNodes);
  if (!err && !nextNodes.empty()) {
    for (size_t i = 0; i < nextNodes.size(); i++) {
      std::shared_ptr<IPipelineNode> pNextNode =
          pPipelineNodeMap->nodeAt(nextNodes[i].val);
      if (pNextNode != nullptr) {
        pthread_rwlock_rdlock(&mFlushLock);
        Pipeline_NodeId_T nextNode_id = pNextNode->getNodeId();
        MUINT32 enqueue_count;
        IPipelineDAG::NodeObjSet_T InAdjacentNodes;

        // get Input Adjacent nodes of next node
        err = pFrame->getPipelineDAG().getInAdjacentNodes(nextNode_id,
                                                          &InAdjacentNodes);
        if (err) {
          MY_LOGE("Get InAdjacentNodes of next node error (%d)", err);
          pthread_rwlock_unlock(&mFlushLock);
          return;
        }
        {
          pthread_rwlock_wrlock(&mRWLock);
          // get In request counter of next node
          err =
              pFrame->getPipelineDAGSp()->addInAdjacentNodesReqCnt(nextNode_id);
          if (err) {
            MY_LOGE("Get InAdjacentNodes of next node error (%d)", err);
            pthread_rwlock_unlock(&mFlushLock);
            return;
          }
          err = pFrame->getPipelineDAG().getInAdjacentNodesReqCnt(
              nextNode_id, &enqueue_count);
          if (err) {
            MY_LOGE("Get InAdjacentNodes of next node error (%d)", err);
            pthread_rwlock_unlock(&mFlushLock);
            return;
          }
          pthread_rwlock_unlock(&mRWLock);
        }
        // after next node receive all of requests, enqueue to next node
        if ((size_t)enqueue_count >= InAdjacentNodes.size()) {
          MY_LOGD("[requestNo:%d] [frameNo:%d] node: %#" PRIxPTR "-> %#" PRIxPTR
                  ", enqueue to next node (%d/%zu)",
                  pFrame->getRequestNo(), pFrame->getFrameNo(), nodeId,
                  nextNode_id, enqueue_count, InAdjacentNodes.size());
          if (mInFlush == MTRUE) {
            pNextNode->flush(pFrame);
          } else {
            pNextNode->queue(pFrame);
          }
        } else {
          MY_LOGD("[requestNo:%d] [frameNo:%d] node: %#" PRIxPTR "-> %#" PRIxPTR
                  ", not enqueue to next node yet (%d/%zu)",
                  pFrame->getRequestNo(), pFrame->getFrameNo(), nodeId,
                  nextNode_id, enqueue_count, InAdjacentNodes.size());
        }
        pthread_rwlock_unlock(&mFlushLock);
      }
    }
  }
}

/******************************************************************************
 *
 ******************************************************************************/
std::string* NSCam::v3::NSPipelineContext::dump(IOMap const& rIomap) {
  struct dumpStreamSet {
    MVOID operator()(const char* str, StreamSet const& set, std::string* log) {
      for (size_t i = 0; i < set.size(); i++) {
        if (i == 0) {
          *log += base::StringPrintf("%s: stream ", str);
        }
        *log += base::StringPrintf("(%#" PRIx64 ")", set[i]);
      }
    }
  };
  std::string* ret = new std::string("");
  dumpStreamSet()("In", rIomap.vIn, ret);
  dumpStreamSet()("Out", rIomap.vOut, ret);
  //
  return ret;
}

/******************************************************************************
 *
 ******************************************************************************/
