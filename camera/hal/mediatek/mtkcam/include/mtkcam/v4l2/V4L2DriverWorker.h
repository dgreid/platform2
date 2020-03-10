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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_V4L2_V4L2DRIVERWORKER_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_V4L2_V4L2DRIVERWORKER_H_

// STL
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace v4l2 {

class V4L2DriverWorker {
 public:
  V4L2DriverWorker();
  V4L2DriverWorker(const V4L2DriverWorker&) = delete;
  V4L2DriverWorker(V4L2DriverWorker&&) = delete;
  V4L2DriverWorker& operator=(const V4L2DriverWorker&) = delete;
  V4L2DriverWorker& operator=(V4L2DriverWorker&&) = delete;

 public:
  virtual ~V4L2DriverWorker();

 public:
  /**
   * Start a loop to do job() continuously
   *  @return         0 for ok.
   */
  virtual int start();

  /**
   * Wait the current job() done, and stop the loop which just finished job().
   *  @return         0 for ok.
   */
  virtual int stop();

  /**
   * Manually ask the loop stop until the current job finished. This is an API
   * for caller who wants to manually exit the loop much faster. For example,
   * caller invokes requestExit, and requestExit returns immediately, caller
   * could manually trigger something to exit job() to stop loop much faster.
   *  @return         0 for ok, otherwise check errcode.
   */
  virtual int requestExit();

 protected:
  /**
   * Derived class has to implement this method.
   */
  virtual void job() = 0;

 protected:
  std::thread m_workerThread;
  std::atomic<bool> m_workerThreadAlive;
  std::mutex m_workerMutex;
  bool m_worker_status;
};      // class V4L2DriverWorker
};      // namespace v4l2
#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_INCLUDE_MTKCAM_V4L2_V4L2DRIVERWORKER_H_
