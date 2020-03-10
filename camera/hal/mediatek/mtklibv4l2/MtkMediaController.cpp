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

#define LOG_TAG "MtkMediaController"

#include <mtklibv4l2/MtkMediaController.h>

#include <sys/stat.h>

#include "SysCall.h"

#include <map>      // std::map
#include <memory>   // std::shared_ptr
#include <string>   // std::string, std::make_shared
#include <utility>  // std::make_pair
#include <vector>   // std::vector

MtkMediaController::MtkMediaController(const char* modelname, const char* path)
    : mModelName(modelname), mPath(path), mFd(-1), mDeviceInfo{} {
  LOGD("[%s] name: %s, path: %s", __FUNCTION__, mModelName.c_str(),
       mPath.c_str());
}

MtkMediaController::~MtkMediaController() {
  LOGD("[%s]", __FUNCTION__);
}

int MtkMediaController::xioctl(int request, void* arg) const {
  int ret(0);
  if (mFd == -1) {
    LOGE("%s invalid device closed!", __FUNCTION__);
    return INVALID_OPERATION;
  }

  do {
    ret = SysCall::ioctl(mFd, request, arg);
  } while (-1 == ret && EINTR == errno);

  if (ret < 0)
    LOGW("%s: Request 0x%x failed: %s", __FUNCTION__, request, strerror(errno));

  return ret;
}

status_t MtkMediaController::open() {
  LOGD("[%s][%s] mdev path %s", mModelName.c_str(), __FUNCTION__,
       mPath.c_str());
  status_t ret = NO_ERROR;
  struct stat st;

  if (mFd != -1) {
    LOGW("Trying to open a device already open");
    return NO_ERROR;
  }

  if (stat(mPath.c_str(), &st) == -1) {
    LOGE("Error stat media device %s: %s", mPath.c_str(), strerror(errno));
    return UNKNOWN_ERROR;
  }

  if (!S_ISCHR(st.st_mode)) {
    LOGE("%s is not a device", mPath.c_str());
    return UNKNOWN_ERROR;
  }

  int fd = SysCall::open(mPath.c_str(), O_RDWR);
  if (fd < 0) {
    if (fd == -EPERM) {
      // Return permission denied, to allow skipping this device.
      // Our HAL may not want to really use this device at all.
      ret = PERMISSION_DENIED;
    } else {
      LOGE("Error opening media device %s: %d (%s)", mPath.c_str(), mFd,
           strerror(errno));
      ret = UNKNOWN_ERROR;
    }
  } else {
    mFd = fd;
  }

  return ret;
}

status_t MtkMediaController::getDeviceInfo() {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  CLEAR(&mDeviceInfo);
  int ret = xioctl(MEDIA_IOC_DEVICE_INFO, &mDeviceInfo);
  if (ret < 0) {
    LOGE("Failed to get media device information");
    return UNKNOWN_ERROR;
  }

  LOGD("Media device driver: %s, model: %s", mDeviceInfo.driver,
       mDeviceInfo.model);
  return NO_ERROR;
}

status_t MtkMediaController::init() {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;

  status = open();
  if (status != NO_ERROR) {
    LOGE("Error opening media device");
    return status;
  }

  status = getDeviceInfo();
  if (status != NO_ERROR) {
    LOGE("Error getting media info");
    close();
    return status;
  }

  return status;
}

status_t MtkMediaController::getMediaDevInfo(struct media_device_info* info) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  if (mFd < 0) {
    LOGE("Media controller isn't initialized");
    return UNKNOWN_ERROR;
  }

  *info = mDeviceInfo;
  LOGD("Media device driver: %s, model: %s", mDeviceInfo.driver,
       mDeviceInfo.model);
  return NO_ERROR;
}

status_t MtkMediaController::getDevName(string* devname) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  if (mFd < 0) {
    LOGE("Media controller isn't initialized");
    return UNKNOWN_ERROR;
  }

  *devname = mModelName;
  LOGD("Media device: %s", devname->c_str());
  return NO_ERROR;
}

status_t MtkMediaController::findMediaEntityById(
    int index, struct media_entity_desc* mediaEntityDesc) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  int ret = 0;
  CLEAR(mediaEntityDesc);
  mediaEntityDesc->id = index;

  ret = SysCall::ioctl(mFd, MEDIA_IOC_ENUM_ENTITIES, mediaEntityDesc);
  if (ret < 0) {
    if (errno == EINVAL && mEntityDesciptors.size() != 0)
      // ending up when no more entities left
      LOGD("Enumerating entities done %s", strerror(errno));
    else
      LOGW("Request enumerate entity failed: %s", strerror(errno));
    return UNKNOWN_ERROR;
  }
  return NO_ERROR;
}

status_t MtkMediaController::enumEntity(
    struct media_entity_desc* mediaEntityDesc) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;
  struct media_entity_desc entity = {};

  status = findMediaEntityById(mediaEntityDesc->id | MEDIA_ENT_ID_FLAG_NEXT,
                               &entity);

  if (status != NO_ERROR) {
    LOGD("No more media entities not found with id:%d", mediaEntityDesc->id);
    return status;
  }

  mEntityDesciptors.insert(std::make_pair(entity.name, entity));
  LOGD("entity name: %s, id: %d, pads: %d, links: %d", entity.name, entity.id,
       entity.pads, entity.links);

  *mediaEntityDesc = entity;

  return status;
}

status_t MtkMediaController::enumLink(struct media_links_enum* linksEnum) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  int ret = 0;
  ret = xioctl(MEDIA_IOC_ENUM_LINKS, linksEnum);
  if (ret < 0) {
    LOGE("Enumerating entity links failed: %s", strerror(errno));
    return UNKNOWN_ERROR;
  }

  return NO_ERROR;
}

status_t MtkMediaController::enumLinks(struct media_links_enum* linksEnum) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;

  status = enumLink(linksEnum);
  if (status != NO_ERROR) {
    LOGE("[%s] @%s enumLink fail", mModelName.c_str(), __FUNCTION__);
    return status;
  }

  struct media_entity_desc entityDesc = {};
  struct media_pad_desc* pads = nullptr;
  struct media_link_desc* links = nullptr;
  std::shared_ptr<MediaEntity> entity;
  bool bEntityFound = false;

  for (const auto& entityDesciptors : mEntityDesciptors) {
    if (linksEnum->entity == entityDesciptors.second.id) {
      entityDesc = entityDesciptors.second;
      bEntityFound = true;
      break;
    }
  }

  if (bEntityFound == false) {
    return NAME_NOT_FOUND;
  }

  pads = linksEnum->pads;
  links = linksEnum->links;

  entity = std::make_shared<MediaEntity>(entityDesc, links, pads);
  mEntities.insert(std::make_pair(entityDesc.name, entity));

  return NO_ERROR;
}

status_t MtkMediaController::setupLink(struct media_link_desc* linkDesc) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;

  LOGD("[source] entity %d, pad %d, [sink] entity %d, pad %d, flag 0x%08x",
       linkDesc->source.entity, linkDesc->source.index, linkDesc->sink.entity,
       linkDesc->sink.index, linkDesc->flags);

  int ret = xioctl(MEDIA_IOC_SETUP_LINK, linkDesc);
  if (ret < 0) {
    if (linkDesc->flags & MEDIA_LNK_FL_IMMUTABLE) {
      LOGW("Link is immutable");
      return status;
    } else {
      LOGE("Link setup failed: %s(%d)", strerror(errno), errno);
    }
    return UNKNOWN_ERROR;
  }
  // update mSinkEntitiesLinkDesc
  map<int, struct media_link_desc>::iterator it;
  for (it = mSinkEntitiesLinkDesc.begin(); it != mSinkEntitiesLinkDesc.end();
       it++) {
    if ((linkDesc->source.entity == it->second.source.entity) &&
        (linkDesc->sink.entity == it->second.sink.entity)) {
      LOGD(
          "[mSinkEntitiesLinkDesc@%s] [link source id(%d) index(%d), sink "
          "id(%d) index(%d), flags(0x%08x)->flags(0x%08x)]",
          __FUNCTION__, it->second.source.entity, it->second.source.index,
          it->second.sink.entity, it->second.sink.index, it->second.flags,
          linkDesc->flags);
      it->second.flags = linkDesc->flags;
      break;
    }
  }
  return status;
}

status_t MtkMediaController::allocateRequest(int* requestFd) {
  int ret = xioctl(MEDIA_IOC_REQUEST_ALLOC, requestFd);

  LOGD("[%s][%s] requestFd=0x%x", mModelName.c_str(), __FUNCTION__, *requestFd);
  if (ret < 0)
    return UNKNOWN_ERROR;

  return NO_ERROR;
}

status_t MtkMediaController::queueRequest(int requestFd) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);

  int ret = SysCall::ioctl(requestFd, MEDIA_REQUEST_IOC_QUEUE, NULL);
  if (ret < 0) {
    LOGW("queueRequest failed: 0x%x:%s", errno, strerror(errno));
    return UNKNOWN_ERROR;
  }

  return NO_ERROR;
}

status_t MtkMediaController::reInitRequest(int requestFd) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);

  int ret = SysCall::ioctl(requestFd, MEDIA_REQUEST_IOC_REINIT, NULL);
  if (ret < 0) {
    LOGW("queueRequest failed: 0x%x:%s", errno, strerror(errno));
    return UNKNOWN_ERROR;
  }

  return NO_ERROR;
}

status_t MtkMediaController::close() {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);

  if (mFd == -1) {
    LOGW("Device not opened!");
    return INVALID_OPERATION;
  }

  if (SysCall::close(mFd) < 0) {
    LOGE("Close media device failed: %s", strerror(errno));
    return UNKNOWN_ERROR;
  }

  mFd = -1;
  return NO_ERROR;
}

status_t MtkMediaController::enumAllLinks() {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;
  struct media_entity_desc entityDesc = {};
  struct media_links_enum linksEnum = {};
  std::shared_ptr<MediaEntity> entity;

  for (const auto& entityDesciptors : mEntityDesciptors) {
    string entityname = entityDesciptors.first;
    entityDesc = entityDesciptors.second;
    std::vector<struct media_pad_desc> pads;
    std::vector<struct media_link_desc> links;

    if (entityDesc.links > 0) {
      links.resize(entityDesc.links);
    }

    if (entityDesc.pads > 0) {
      pads.resize(entityDesc.pads);
    }

    LOGD("Creating entity - name: %s, id: %d, links: %d, pads: %d",
         entityDesc.name, entityDesc.id, entityDesc.links, entityDesc.pads);

    constexpr const char* MTK_ENT_NAME = "mtk-cam";
    if (strncmp(entityDesc.name, MTK_ENT_NAME, strlen(MTK_ENT_NAME)) != 0) {
      LOGD("EnumLinks not in topology %s", entityDesc.name);
      continue;
    }
    linksEnum.entity = entityDesc.id;
    linksEnum.pads = pads.data();
    linksEnum.links = links.data();
    status = enumLink(&linksEnum);
    if (status != NO_ERROR) {
      LOGE("Enumerate links of entity %d failed", entityDesc.id);
      continue;
    }

    /* MediaEntity copys |links.data()| and |pads.data()| */
    entity =
        std::make_shared<MediaEntity>(entityDesc, links.data(), pads.data());
    mEntities.insert(std::make_pair(entityDesc.name, entity));

    if (entityDesc.pads > 0) {
      for (int j = 0; j < entityDesc.pads; j++) {
        LOGD("pad entity id(%d) index(%d)", linksEnum.pads[j].entity,
             linksEnum.pads[j].index);
      }
    }
    if (entityDesc.links > 0) {
      for (int j = 0; j < entityDesc.links; j++) {
        mSinkEntitiesLinkDesc.insert(
            std::make_pair(linksEnum.links[j].sink.entity, links[j]));
        mSourceEntitiesLinkDesc.insert(
            std::make_pair(linksEnum.links[j].source.entity, links[j]));
        LOGD("link source id(%d) index(%d), sink id(%d) index(%d)",
             linksEnum.links[j].source.entity, linksEnum.links[j].source.index,
             linksEnum.links[j].sink.entity, linksEnum.links[j].sink.index);
      }
    }
  }
  if (mSinkEntitiesLinkDesc.size() > 0) {
    for (const auto& p : mSinkEntitiesLinkDesc) {
      LOGD(
          "[mSinkEntitiesLinkDesc] map <[sink id (%d)],[link source id(%d) "
          "index(%d), sink id(%d) index(%d), flags(0x%08x)]>",
          p.first, p.second.source.entity, p.second.source.index,
          p.second.sink.entity, p.second.sink.index, p.second.flags);
    }
    for (const auto& p : mSourceEntitiesLinkDesc) {
      LOGD(
          "[mSourceEntitiesLinkDesc] map <[source id (%d)],[link source id(%d) "
          "index(%d), sink id(%d) index(%d), flags(0x%08x)]>",
          p.first, p.second.source.entity, p.second.source.index,
          p.second.sink.entity, p.second.sink.index, p.second.flags);
    }
  }
  return NO_ERROR;
}

status_t MtkMediaController::getMediaEntity(
    std::shared_ptr<MediaEntity>* entity, const char* name) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);

  status_t status = NO_ERROR;
  string entityName(name);

  // check whether the MediaEntity object has already been created
  std::map<std::string, std::shared_ptr<MediaEntity>>::iterator itEntities =
      mEntities.find(entityName);

  if (itEntities != mEntities.end()) {
    *entity = itEntities->second;
  } else {
    LOGE("It has no %s media entity", name);
    return UNKNOWN_ERROR;
  }
  return status;
}

status_t MtkMediaController::getMediaEntityID(int* entityID, const char* name) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;

  string entityName(name);

  // check whether the MediaEntity object has already been created
  std::map<std::string, struct media_entity_desc>::iterator itEntities =
      mEntityDesciptors.find(entityName);

  if (itEntities != mEntityDesciptors.end()) {
    *entityID = itEntities->second.id;
  } else {
    LOGE("It has no %s media entity", name);
    return UNKNOWN_ERROR;
  }
  return status;
}

status_t MtkMediaController::getLinkDescbyEntityName(
    struct media_link_desc* linkdesc, const char* name) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;
  int entityid = -1;
  getMediaEntityID(&entityid, name);
  std::map<int, struct media_link_desc>::iterator itSinkEntity =
      mSinkEntitiesLinkDesc.find(entityid);
  std::map<int, struct media_link_desc>::iterator itSourceEntity =
      mSourceEntitiesLinkDesc.find(entityid);
  if (itSinkEntity != mSinkEntitiesLinkDesc.end()) {
    *linkdesc = itSinkEntity->second;
  } else if (itSourceEntity != mSourceEntitiesLinkDesc.end()) {
    *linkdesc = itSourceEntity->second;
    LOGD(
        "[%s][%s] query name = %s ; desc_link_desc (Source ID = %d->%d) / "
        "Flags=0x%8x)",
        mModelName.c_str(), __FUNCTION__, name, linkdesc->source.entity,
        linkdesc->sink.entity, linkdesc->flags);
  } else {
    LOGE("It has no %s media entity", name);
    return UNKNOWN_ERROR;
  }
  LOGD("[%s][%s] query name = %s ; desc_link_desc (ID = %d->%d) / Flags=0x%8x)",
       mModelName.c_str(), __FUNCTION__, name, linkdesc->source.entity,
       linkdesc->sink.entity, linkdesc->flags);
  return status;
}

status_t MtkMediaController::getLinkDescbyEntityName(media_link_desc* linkdesc,
                                                     const char* srcname,
                                                     const char* sinkname) {
  status_t status = NO_ERROR;
  int entityid = -1;
  struct media_link_desc linkdesctmp;

  getMediaEntityID(&entityid, srcname);
  status = getLinkDescbyEntityName(&linkdesctmp, sinkname);
  if (status == NO_ERROR && linkdesctmp.source.entity == entityid) {
    *linkdesc = linkdesctmp;
    return status;
  }

  getMediaEntityID(&entityid, sinkname);
  status = getLinkDescbyEntityName(&linkdesctmp, srcname);
  if (status == NO_ERROR && linkdesctmp.sink.entity == entityid) {
    *linkdesc = linkdesctmp;
    return status;
  }

  LOGE("It has no link %s->%s", srcname, sinkname);
  return UNKNOWN_ERROR;
}

/**
 * Resets (disables) all links between entities
 */
status_t MtkMediaController::resetAllLinks() {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;

  for (const auto& Entity : mEntities) {
    std::shared_ptr<MediaEntity> entity;
    std::vector<media_link_desc> links;
    struct media_entity_desc entityDesc = {};

    // Disable all links, except the immutable ones
    entity = Entity.second;
    entity->getEntityDesc(&entityDesc);
    entity->getLinkDesc(&links);

    LOGD("@%s: entity id %d ", __FUNCTION__, entityDesc.id);
    for (int j = 0; j < entityDesc.links; j++) {
      LOGD("@%s:ori link source id %d/%d, sink id %d/%d flags 0x%8x",
           __FUNCTION__, links[j].source.entity, links[j].source.index,
           links[j].sink.entity, links[j].sink.index, links[j].flags);
      if (links[j].flags & MEDIA_LNK_FL_IMMUTABLE)
        continue;

      links[j].flags &= ~MEDIA_LNK_FL_ENABLED;
      setupLink(&links[j]);
    }
  }
  return status;
}

status_t MtkMediaController::disableLink(media_link_desc* link) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;

  link->flags &= ~MEDIA_LNK_FL_ENABLED;
  setupLink(link);

  return status;
}

status_t MtkMediaController::enableLink(media_link_desc* link) {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;

  link->flags |= MEDIA_LNK_FL_ENABLED;
  setupLink(link);

  return status;
}

status_t MtkMediaController::storeAllLinks() {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;
  mInitialSinkEntitiesLinkDesc = mSinkEntitiesLinkDesc;
  mInitialSourceEntitiesLinkDesc = mSourceEntitiesLinkDesc;
  return status;
}

status_t MtkMediaController::enableAllLinks() {
  LOGD("[%s][%s] ", mModelName.c_str(), __FUNCTION__);
  status_t status = NO_ERROR;

  for (const auto& Entity : mEntities) {
    std::shared_ptr<MediaEntity> entity;
    struct media_entity_desc entityDesc = {};
    struct media_link_desc linkdesc = {};
    struct media_link_desc linkdescinit = {};
    std::vector<media_link_desc> links;

    // enable all links, except the immutable ones and already enable ones
    entity = Entity.second;
    entity->getEntityDesc(&entityDesc);
    entity->getLinkDesc(&links);

    for (int j = 0; j < entityDesc.links; j++) {
      std::map<int, struct media_link_desc>::iterator itEntities =
          mSinkEntitiesLinkDesc.find(links[j].sink.entity);
      std::map<int, struct media_link_desc>::iterator itEntitiesinit =
          mInitialSinkEntitiesLinkDesc.find(links[j].sink.entity);
      if ((itEntities != mSinkEntitiesLinkDesc.end()) &&
          (itEntitiesinit != mInitialSinkEntitiesLinkDesc.end())) {
        linkdesc = itEntities->second;
        linkdescinit = itEntitiesinit->second;
        if (links[j].source.entity == linkdesc.source.entity) {
          LOGD(
              "@%s:Found Link:  %d->%d, QueryLink/InitLink/CurrentLink "
              "0x%8x/0x%8x/0x%8x",
              __FUNCTION__, links[j].source.entity, links[j].sink.entity,
              links[j].flags, linkdescinit.flags, linkdesc.flags);
          if (linkdescinit.flags != linkdesc.flags) {
            enableLink(&linkdesc);
          }
        } else {
          LOGD("@%s:Not found1 %d->%d link in mSinkEntitiesLinkDesc",
               __FUNCTION__, links[j].source.entity, links[j].sink.entity);
        }
      } else {
        LOGD("@%s:Not found2 %d->%d link in mSinkEntitiesLinkDesc",
             __FUNCTION__, links[j].source.entity, links[j].sink.entity);
      }
    }
    for (int j = 0; j < entityDesc.links; j++) {
      std::map<int, struct media_link_desc>::iterator itEntities =
          mSourceEntitiesLinkDesc.find(links[j].source.entity);
      std::map<int, struct media_link_desc>::iterator itEntitiesinit =
          mInitialSourceEntitiesLinkDesc.find(links[j].source.entity);
      if ((itEntities != mSourceEntitiesLinkDesc.end()) &&
          (itEntitiesinit != mInitialSourceEntitiesLinkDesc.end())) {
        linkdesc = itEntities->second;
        linkdescinit = itEntitiesinit->second;
        if (links[j].sink.entity == linkdesc.sink.entity) {
          LOGD(
              "@%s:Found Link:  %d->%d, QueryLink/InitLink/CurrentLink "
              "0x%8x/0x%8x/0x%8x",
              __FUNCTION__, links[j].source.entity, links[j].sink.entity,
              links[j].flags, linkdescinit.flags, linkdesc.flags);
          if (linkdescinit.flags != linkdesc.flags) {
            enableLink(&linkdesc);
          }
        } else {
          LOGD("@%s:Not found1 %d->%d link in mSourceEntitiesLinkDesc",
               __FUNCTION__, links[j].source.entity, links[j].sink.entity);
        }
      } else {
        LOGD("@%s:Not found2 %d->%d link in mSourceEntitiesLinkDesc",
             __FUNCTION__, links[j].source.entity, links[j].sink.entity);
      }
    }
  }

  return status;
}
