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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_FEATURE_FEATURECORE_FEATUREPIPE_COMMON_INCLUDE_SEQUTIL_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_FEATURE_FEATURECORE_FEATUREPIPE_COMMON_INCLUDE_SEQUTIL_H_

#include <mtkcam/feature/featurePipe/util/VarMap.h>

#include "PipeLogHeaderBegin.h"
#include "DebugControl.h"
#define PIPE_TRACE TRACE_SEQ_UTIL
#define PIPE_CLASS_TAG "SeqUtil"
#include "PipeLog.h"
#include "SeqUtil_t.h"

#include <memory>
#include <string>

namespace NSCam {
namespace NSCamFeature {
namespace NSFeaturePipe {

template <typename T, class SeqConverter>
SequentialQueue<T, SeqConverter>::SequentialQueue() : mSeq(0) {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
}

template <typename T, class SeqConverter>
SequentialQueue<T, SeqConverter>::SequentialQueue(unsigned seq) : mSeq(seq) {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
}

template <typename T, class SeqConverter>
SequentialQueue<T, SeqConverter>::~SequentialQueue() {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
}

template <typename T, class SeqConverter>
bool SequentialQueue<T, SeqConverter>::empty() const {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
  return mQueue.empty();
}

template <typename T, class SeqConverter>
size_t SequentialQueue<T, SeqConverter>::size() const {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
  return mQueue.size();
}

template <typename T, class SeqConverter>
void SequentialQueue<T, SeqConverter>::enque(const T& val) {
  TRACE_FUNC_ENTER();
  if (mSeq != mSeqConverter(val)) {
    MY_LOGD("seq(%d) != enque(%d)", mSeq, mSeqConverter(val));
  }
  TRACE_FUNC_EXIT();
  return mQueue.push(val);
}

template <typename T, class SeqConverter>
bool SequentialQueue<T, SeqConverter>::deque(T* val) {
  TRACE_FUNC_ENTER();

  bool ret = false;
  if (!mQueue.empty()) {
    const T& min = mQueue.top();
    if (mSeqConverter(min) == mSeq) {
      *val = min;
      mQueue.pop();
      ++mSeq;
      ret = true;
    }
  }

  TRACE_FUNC_EXIT();
  return ret;
}

template <typename T, class SeqConverter>
bool SequentialQueue<T, SeqConverter>::DataGreater::operator()(
    const T& lhs, const T& rhs) const {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
  return mSeqConverter(lhs) > mSeqConverter(rhs);
}

template <typename Handler_T, class Enable>
SequentialHandler<Handler_T, Enable>::SequentialHandler() {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
}

template <typename Handler_T, class Enable>
SequentialHandler<Handler_T, Enable>::SequentialHandler(unsigned) {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
}

template <typename Handler_T, class Enable>
SequentialHandler<Handler_T, Enable>::~SequentialHandler() {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
}

template <typename Handler_T, class Enable>
template <typename DATA_T>
MBOOL SequentialHandler<Handler_T, Enable>::onData(DataID_T id,
                                                   const DATA_T& data,
                                                   Handler_T* handler) {
  TRACE_FUNC_ENTER();
  MY_LOGE("Please define Handler_T::getSeq() before using SequentialHandler");
  TRACE_FUNC_EXIT();
  return handler->onData(id, data);
}

template <typename Handler_T, class Enable>
template <typename DATA_T>
MBOOL SequentialHandler<Handler_T, Enable>::onData(DataID_T id,
                                                   DATA_T* data,
                                                   Handler_T* handler) {
  TRACE_FUNC_ENTER();
  MY_LOGE("Please define Handler_T::getSeq() before using SequentialHandler");
  TRACE_FUNC_EXIT();
  return handler->onData(id, data);
}

template <typename Handler_T, class Enable>
MVOID SequentialHandler<Handler_T, Enable>::clear() {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
}

template <typename Handler_T>
SequentialHandler<
    Handler_T,
    typename std::enable_if<Handler_T::supportSeq>::type>::SequentialHandler()
    : mSeq(0) {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
}

template <typename Handler_T>
SequentialHandler<Handler_T,
                  typename std::enable_if<Handler_T::supportSeq>::type>::
    SequentialHandler(unsigned seq)
    : mSeq(seq) {
  TRACE_FUNC_ENTER();
  TRACE_FUNC_EXIT();
}

template <typename Handler_T>
SequentialHandler<Handler_T,
                  typename std::enable_if<Handler_T::supportSeq>::type>::
    ~SequentialHandler() {
  TRACE_FUNC_ENTER();
  clear();
  TRACE_FUNC_EXIT();
}

template <typename Handler_T>
template <typename DATA_T>
MBOOL SequentialHandler<Handler_T,
                        typename std::enable_if<Handler_T::supportSeq>::type>::
    onData(DataID_T id, const DATA_T& data, Handler_T* handler) {
  TRACE_FUNC_ENTER();

  SequentialQueue<DATA_T, HandlerSeqConverter>* seqQueue =
      getSeqQueue<DATA_T, HandlerSeqConverter>(id);
  seqQueue->enque(data);

  DATA_T d;
  while (seqQueue->deque(&d)) {
    handler->onData(id, d);
  }

  TRACE_FUNC_EXIT();
  return MTRUE;
}

template <typename Handler_T>
template <typename DATA_T>
MBOOL SequentialHandler<Handler_T,
                        typename std::enable_if<Handler_T::supportSeq>::type>::
    onData(DataID_T id, DATA_T* data, Handler_T* handler) {
  TRACE_FUNC_ENTER();

  SequentialQueue<DATA_T, HandlerSeqConverter>* seqQueue =
      getSeqQueue<DATA_T, HandlerSeqConverter>(id);
  seqQueue->enque(*data);

  DATA_T d;
  while (seqQueue->deque(&d)) {
    handler->onData(id, d);
  }

  TRACE_FUNC_EXIT();
  return MTRUE;
}

template <typename Handler_T>
MVOID SequentialHandler<
    Handler_T,
    typename std::enable_if<Handler_T::supportSeq>::type>::clear() {
  TRACE_FUNC_ENTER();
  mQueueMap.clear();
  TRACE_FUNC_EXIT();
}

template <typename Handler_T>
template <typename DATA_T>
unsigned
SequentialHandler<Handler_T,
                  typename std::enable_if<Handler_T::supportSeq>::type>::
    HandlerSeqConverter::operator()(const DATA_T& data) const {
  return Handler_T::getSeq(data);
}

template <typename Handler_T>
template <typename DATA_T, class SeqConverter>
SequentialQueue<DATA_T, SeqConverter>* SequentialHandler<
    Handler_T,
    typename std::enable_if<Handler_T::supportSeq>::type>::getSeqQueue(DataID_T
                                                                           id) {
  TRACE_FUNC_ENTER();
  std::string key;
  key = std::string(Handler_T::ID2Name(id)) +
        std::string(getTypeNameID<DATA_T>());

  typename CONTAINER::const_iterator it = mQueueMap.find(key);

  if (it == mQueueMap.end()) {
    mQueueMap[key] =
        std::make_shared<SequentialQueue<DATA_T, SeqConverter>>(mSeq);
  }

  TRACE_FUNC_EXIT();
  return static_cast<SequentialQueue<DATA_T, SeqConverter>*>(
      mQueueMap[key].get());
}

};  // namespace NSFeaturePipe
};  // namespace NSCamFeature
};  // namespace NSCam

#include "PipeLogHeaderEnd.h"
#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_FEATURE_FEATURECORE_FEATUREPIPE_COMMON_INCLUDE_SEQUTIL_H_
