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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_P2_P2_REQUEST_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_P2_P2_REQUEST_H_

#include "P2_Param.h"
#include "P2_LMVInfo.h"
#include "P2_Cropper.h"

#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace P2 {

class IP2Frame {
 public:
  virtual ~IP2Frame() {}
  virtual MVOID beginBatchRelease() = 0;
  virtual MVOID endBatchRelease() = 0;
  virtual MVOID notifyNextCapture() = 0;
};

class P2FrameHolder : virtual public IP2Frame {
 public:
  explicit P2FrameHolder(const std::shared_ptr<IP2Frame>& frame);
  virtual ~P2FrameHolder();
  virtual MVOID beginBatchRelease();
  virtual MVOID endBatchRelease();
  virtual MVOID notifyNextCapture();

 protected:
  std::shared_ptr<IP2Frame> getIP2Frame() const;

 private:
  std::shared_ptr<IP2Frame> mFrame;
};

class P2Request : virtual public P2FrameHolder {
 public:
  enum {
    RES_IN_IMG = (1 << 0),
    RES_IN_META = (1 << 1),
    RES_OUT_IMG = (1 << 2),
    RES_OUT_META = (1 << 3),
    RES_IMG = RES_IN_IMG | RES_OUT_IMG,
    RES_META = RES_IN_META | RES_OUT_META,
    RES_ALL = RES_IMG | RES_META
  };

 public:
  P2Request(const ILog& log,
            const std::shared_ptr<IP2Frame>& frameHolder,
            const P2Pack& p2Pack,
            const std::shared_ptr<P2InIDMap>& p2IdMap);
  explicit P2Request(const std::shared_ptr<P2Request>& request);
  P2Request(const std::shared_ptr<P2Request>& request, MUINT32 sensorID);
  virtual ~P2Request();

 public:
  MVOID updateSensorID();
  MVOID initIOInfo();
  MUINT32 getSensorID() const;
  std::shared_ptr<Cropper> getCropper() const;
  std::shared_ptr<Cropper> getCropper(MUINT32 sensorID) const;
  MBOOL hasInput() const;
  MBOOL hasOutput() const;
  MBOOL isResized() const;
  MBOOL isReprocess() const;
  MBOOL isPhysic() const;
  MBOOL isLarge() const;
  MVOID releaseResource(MUINT32 res);
  MVOID releaseMeta(ID_META id);
  MVOID releaseImg(ID_IMG id);
  P2MetaSet getMetaSet() const;
  MVOID updateMetaSet(const P2MetaSet& set);
  MVOID updateResult(MBOOL result);
  MVOID updateMetaResult(MBOOL result);
  MVOID dump() const;

  std::shared_ptr<P2Meta> getMeta(ID_META id) const;
  IMetadata* getMetaPtr(ID_META id) const;
  std::shared_ptr<P2Meta> getMeta(ID_META id, MUINT32 sensorID) const;
  IMetadata* getMetaPtr(ID_META id, MUINT32 sensorID) const;

  std::shared_ptr<P2Img> getImg(ID_IMG id) const;
  MBOOL isValidMeta(ID_META id) const;
  MBOOL isValidImg(ID_IMG id) const;
  std::shared_ptr<P2InIDMap> getIDMap() const;

 public:
  const ILog mLog;
  const P2Pack mP2Pack;
  P2DumpType mDumpType = P2_DUMP_NONE;

 public:
  /* Below data will NOT be copied during P2Request copy constructor */
  // TODO(mtk): replace policy by comment to policy by code
  std::unordered_map<ID_META, std::shared_ptr<P2Meta>> mMeta;
  std::unordered_map<ID_IMG, std::shared_ptr<P2Img>> mImg;
  std::vector<std::shared_ptr<P2Img>> mImgOutArray;

 protected:
  /* Below data will be copied during P2Request copy constructor */
  // TODO(mtk): replace policy by comment to policy by code
  std::shared_ptr<P2InIDMap> mInIDMap;

 private:
  MBOOL mIsResized = MFALSE;
  MBOOL mIsReprocess = MFALSE;
  MBOOL mIsPhysic = MFALSE;
  MUINT32 mSensorID = INVALID_SENSOR_ID;
};

class P2FrameRequest : public IP2Frame {
 public:
  P2FrameRequest(const ILog& log,
                 const P2Pack& pack,
                 const std::shared_ptr<P2InIDMap>& p2IdMap);
  virtual ~P2FrameRequest();
  virtual std::vector<std::shared_ptr<P2Request>> extractP2Requests() = 0;

 public:
  MUINT32 getMainSensorID() const;
  MUINT32 getFrameID() const;
  MVOID registerImgPlugin(const std::shared_ptr<P2ImgPlugin>& plugin,
                          MBOOL needSWRW = MFALSE);

 public:
  const ILog mLog;

 protected:
  ID_META mapID(MUINT32 sensorID, ID_META metaId);
  ID_IMG mapID(MUINT32 sensorID, ID_IMG imgId);

  P2Pack mP2Pack;
  std::shared_ptr<P2InIDMap> mInIDMap;
  std::list<std::shared_ptr<P2ImgPlugin>> mImgPlugin;
  MBOOL mNeedImageSWRW = MFALSE;
};

}  // namespace P2

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_P2_P2_REQUEST_H_
