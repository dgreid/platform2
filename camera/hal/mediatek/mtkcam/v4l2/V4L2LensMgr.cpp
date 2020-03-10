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

#define LOG_TAG "v4l2_lens_mgr"

#include <mtkcam/v4l2/property_strings.h>
#include <mtkcam/v4l2/V4L2LensMgr.h>

// MTKCAM
#include <mtkcam/utils/std/Log.h>  // log
#include <property_lib.h>

// STL
#include <memory>   // std::shared_ptr
#include <string>   // std::string
#include <thread>   // std::yield
#include <utility>  // std::utility

#include <sys/ioctl.h>  // ioctl
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>  // open

#include <linux/media.h>
#include <linux/v4l2-subdev.h>

#define V4L2LENSMGR_MAX_MDEV_NUM 5  // maximum media device number to enumerate

namespace v4l2 {

/* static instances */
std::once_flag V4L2LensMgr::m_onceFlgLogLvl;
int V4L2LensMgr::m_logLevel;

V4L2LensMgr::V4L2LensMgr(uint32_t sensorIdx)
    : m_sensorIdx(sensorIdx), m_fd_sdev(-1) {
  std::call_once(m_onceFlgLogLvl, []() {
    m_logLevel = property_get_int32(PROP_V4L2_LENSMGR_LOGLEVEL, 2);
  });

  // create IHal3A
  MAKE_Hal3A(
      m_pHal3A, [](IHal3A* p) { p->destroyInstance(LOG_TAG); }, sensorIdx,
      LOG_TAG);

  if (openLensDriver() == false) {
    CAM_LOGD("no lens driver to open");
  }

  m_lensCfgs.reserve(10);  // avoid frequency reallocations
}

V4L2LensMgr::~V4L2LensMgr() {
  if (m_fd_sdev != -1) {
    ::close(m_fd_sdev);
  }
}

void V4L2LensMgr::validate() {
  NS3Av3::IPC_LensConfig_T cfg = {0};
  cfg.cmd = NS3Av3::IPC_LensConfig_T::ASK_TO_START;  // ask to start
  m_pHal3A->send3ACtrl(E3ACtrl_IPC_AF_ExchangeLensConfig,
                       reinterpret_cast<MINTPTR>(&cfg), 0);
}

void V4L2LensMgr::invalidate() {
  NS3Av3::IPC_LensConfig_T cfg = {0};
  cfg.cmd = NS3Av3::IPC_LensConfig_T::ASK_TO_STOP;  // ask to stop
  m_pHal3A->send3ACtrl(E3ACtrl_IPC_AF_ExchangeLensConfig,
                       reinterpret_cast<MINTPTR>(&cfg), 0);
}

int V4L2LensMgr::start() {
  validate();
  return V4L2DriverWorker::start();
}

int V4L2LensMgr::stop() {
  // notify 3A framework to stop IPC
  invalidate();
  // stop job
  return V4L2DriverWorker::stop();
}

void V4L2LensMgr::job() {
  MBOOL result = 0;
  IPC_LensConfig_T lensConfig = {0};
  lensConfig.cmd = IPC_LensConfig_T::ASK_FOR_A_CMD;

  /* deque a lens config from IHal3A */
  result = m_pHal3A->send3ACtrl(E3ACtrl_IPC_AF_ExchangeLensConfig,
                                reinterpret_cast<MINTPTR>(&lensConfig), 0);

  /* checks succeeded or not */
  if (result != MTRUE || lensConfig.succeeded == 0) {
    /* hint to reschedule */
    std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return;
  }

  switch (lensConfig.cmd) {
    case IPC_LensConfig_T::CMD_FOCUS_ABSOULTE:
      moveMCU(lensConfig.val.focus_pos);
      break;

    case IPC_LensConfig_T::CMD_IS_SUPPORT_LENS:
      lensConfig.cmd = IPC_LensConfig_T::ACK_IS_SUPPORT_LENS;
      lensConfig.val.is_support = isLensDriverOpened() ? 1 : 0;
      lensConfig.succeeded = 1;
      m_pHal3A->send3ACtrl(E3ACtrl_IPC_AF_ExchangeLensConfig,
                           reinterpret_cast<MINTPTR>(&lensConfig), 0);

      break;

    default:
      CAM_LOGW("deque an IPC lens config but not support cmd(%d)",
               lensConfig.cmd);
      std::this_thread::yield();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      return;
      break;
  }
}

void V4L2LensMgr::enqueConfig(const IPC_LensConfig_T& param) {
  switch (param.cmd) {
    case IPC_LensConfig_T::CMD_FOCUS_ABSOULTE: {
      std::unique_lock<std::mutex> lk(m_lockLensCfg);
      /* only keeps a CMD_FOCUS_ABSOULTE in container */
      for (auto i = m_lensCfgs.begin(); i != m_lensCfgs.end();) {
        if (i->cmd == IPC_LensConfig_T::CMD_FOCUS_ABSOULTE) {
          i = m_lensCfgs.erase(i);
          break;  // basically, we do only have a CMD_FOCUS_ABSOULTE in
                  // container
        } else {
          ++i;
        }
      }
      m_lensCfgs.push_back(param);
      lk.unlock();
      CAM_LOGD("enqued lens config (FOCUS_ABSOLUTE");
      m_condLensCfg.notify_all();
    } break;

    default:
      break;
  }
}

int V4L2LensMgr::dequeConfig(IPC_LensConfig_T* p_result) {
  std::unique_lock<std::mutex> lk(m_lockLensCfg);

  /* check still queuing or not */
  if (m_bEnableQueuing.load(std::memory_order_relaxed) == false) {
    return -EPERM;
  }

  /* if there's no configurations */
  while (m_lensCfgs.size() <= 0) {
    auto r = m_condLensCfg.wait_for(
        lk, std::chrono::milliseconds(33));  // 33 ms per-frame
    if (r == std::cv_status::timeout) {
      return -ETIMEDOUT;
    }
  }
  /* move the first element */
  *p_result = std::move(m_lensCfgs.front());
  m_lensCfgs.erase(m_lensCfgs.begin());
  return 0;
}

bool V4L2LensMgr::openLensDriver() {
  auto i2c_idx = 1 << (1 + m_sensorIdx);  // sensor idx 0 --> i2c idx is 2
  int r = getSubDevice(i2c_idx, &m_fd_sdev);
  if (r != 0) {
    CAM_LOGD("cannot find lens driver, errcode=%d", r);
    return false;
  }
  return true;
}

int V4L2LensMgr::moveMCU(int64_t pos) {
  if (isLensDriverOpened() == false) {
    return -ENOENT;
  }

  struct v4l2_control control = {};
  control.id = V4L2_CID_FOCUS_ABSOLUTE;
  control.value = static_cast<int>(pos);

  CAM_LOGD("lens subdev = %d, ctrl id = %d, value = %d", m_fd_sdev, control.id,
           control.value);

  int r = ioctl(m_fd_sdev, VIDIOC_S_CTRL, &control);

  if (r != 0) {
    CAM_LOGE("cannot set V4L2_CID_FOCUS_ABSOLUTE, err = %d", r);
  } else {
    CAM_LOGD("set focus absolutely to %d", (int)pos);
  }
  return r;
}

int V4L2LensMgr::getSubDevice(size_t i2c_idx, int* p_r_sdev_fd) {
  char dev_name[64];
  char subdev_name[64];

  int num_media_devices = 0;
  int dev_fd = -1, sd_fd = -1, r = 0;

  /* traverse all media devices */
  while (true) {
    struct media_device_info mdev_info = {};

    /* try to open media device */
    ::snprintf(dev_name, sizeof(dev_name), "/dev/media%d", num_media_devices++);
    dev_fd = ::open(dev_name, O_RDWR | O_NONBLOCK);

    if (dev_fd < 0) {
      /* if retry times is greater than V4L2LENSMGR_MAX_MDEV_NUM, stop trying */
      if (num_media_devices > V4L2LENSMGR_MAX_MDEV_NUM) {
        CAM_LOGD("no media device anymore (at mdev %d)", num_media_devices - 1);
        return -ENOENT;
      }
      /* if open failed, try to open the next one */
      CAM_LOGD("open mdev %d failed, keep trying the next", num_media_devices);
      continue;
    }

    /* enumerate media device info */
    r = ioctl(dev_fd, MEDIA_IOC_DEVICE_INFO, &mdev_info);
    if (r < 0) {
      ::close(dev_fd);
      continue;
    }

    CAM_LOGD("get mdev_info.driver   : %s", mdev_info.driver);
    CAM_LOGD("get mdev_info.model    : %s", mdev_info.model);
    CAM_LOGD("get mdev_info.serial   : %s", mdev_info.serial);
    CAM_LOGD("get mdev_info.bus_info : %s", mdev_info.bus_info);

    /* traverse all entities */
    int next_entity_id = 1;
    while (true) {
      struct media_entity_desc entity = {0};
      entity.id = next_entity_id | MEDIA_ENT_ID_FLAG_NEXT;
      r = ioctl(dev_fd, MEDIA_IOC_ENUM_ENTITIES, &entity);
      /* update the next entity id */
      next_entity_id = entity.id;

      if (r < 0) {
        if (next_entity_id & MEDIA_IOC_ENUM_ENTITIES) {
          /* no more entity, break loop */
          break;
        }
        continue;
      }

      CAM_LOGD("entity name %s, type 0x%x, group id %d, major %d minor %d",
               entity.name, entity.type, entity.group_id, entity.dev.major,
               entity.dev.minor);

      if (entity.type == MEDIA_ENT_T_V4L2_SUBDEV_LENS) {
        /* parse i2c index by name first */
        int dev_i2c_idx = getI2Cindex(entity.name);
        /* checks if the i2c index is what we want */
        if (dev_i2c_idx != i2c_idx) {
          CAM_LOGD(
              "found lens driver \"%s\", but i2c index(%d) is not "
              "what we want (%d).",
              entity.name, dev_i2c_idx, i2c_idx);
          continue;  // find the next entity
        }

        char __tempbuf[64] = {0};
        if (0 == getSubDevName(entity.dev.major, entity.dev.minor, __tempbuf)) {
          ::snprintf(subdev_name, sizeof(subdev_name), "/dev/%s", __tempbuf);
          sd_fd = ::open(subdev_name, O_RDWR);

          if (sd_fd >= 0) {
            CAM_LOGD("found lens driver %s", subdev_name);
          } else {
            CAM_LOGE("cannot open lens driver %s", subdev_name);
          }
          break;
        }
      }
    }  // while: traverse all entities of the given media device

    /* if found subdev, break loop */
    ::close(dev_fd);
    dev_fd = -1;
    if (sd_fd >= 0) {
      *p_r_sdev_fd = sd_fd;
      break;
    }
  }  // while: traverse media devices

  if (sd_fd >= 0) {
    CAM_LOGD("open lens driver %s", subdev_name);
  } else {
    CAM_LOGD("no lens driver to open (target i2c idx=%d)", i2c_idx);
  }

  return 0;
}

int V4L2LensMgr::getSubDevName(int major, int minor, char* r_subdev_name) {
  char uevent_path[64] = {0};
  int fd = -1, r = 0;
  char* ptr = nullptr;

  /* constructs file path */
  ::snprintf(uevent_path, sizeof(uevent_path), "/sys/dev/char/%d:%d/uevent",
             major, minor);

  fd = ::open(uevent_path, O_RDONLY);

  if (fd < 0)
    return -ENOENT;

  size_t filesize = ::lseek(fd, (off_t)0, SEEK_END);
  (void)::lseek(fd, (off_t)0, SEEK_SET);

  if (filesize <= 0) {
    close(fd);
    return -ENOENT;
  }

  /* allocate character buffer */
  std::unique_ptr<char[]> uevent = std::make_unique<char[]>(filesize + 1);
  uevent[filesize] = '\0';

  /* read buffer */
  r = ::read(fd, uevent.get(), filesize);
  close(fd);

  ptr = ::strstr(uevent.get(), "DEVNAME");

  if (ptr == nullptr) {
    return -ENOENT;
  }

  r = ::sscanf(ptr, "DEVNAME=%s\n", r_subdev_name);
  CAM_LOGD("subdev name is %s", r_subdev_name);
  return 0;
}

int V4L2LensMgr::getI2Cindex(std::string dev_name) {
  /*
   * syntax is XXXXX 0-XXXXX
   *                 ^ idx of i2c
   */

  auto pos = dev_name.find_first_of("-");
  /* not found or sytax not valid */
  if (pos == std::string::npos || pos <= 0) {
    return -1;
  }

  /* pick up one character before token "-" */
  auto str_idx = dev_name.substr(pos - 1, 1);

  /* checks character is valid (ASCII 0-9) */
  if (str_idx[0] < '0' || str_idx[0] > '9') {
    return -1;
  }

  return std::atoi(str_idx.c_str());
}
};  // namespace v4l2
