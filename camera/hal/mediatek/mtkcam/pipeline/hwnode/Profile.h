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

/******************************************************************************
 *
 ******************************************************************************/
#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_PROFILE_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_PROFILE_H_

#include <mtkcam/utils/std/Profile.h>
namespace NSCam {
namespace v3 {

class DurationProfile {
 public:
  explicit DurationProfile(char const* const szSubjectName)
      : msSubjectName(szSubjectName),
        mi4Count(0),
        mnsStart(0),
        mnsEnd(0),
        mnsTotal(0),
        mnsWarning(1000000000LL),
        mbIsWarning(false) {}

  DurationProfile(char const* const szSubjectName, int64_t nsWarning)
      : msSubjectName(szSubjectName),
        mi4Count(0),
        mnsStart(0),
        mnsEnd(0),
        mnsTotal(0),
        mnsWarning(nsWarning),
        mbIsWarning(false) {}

  virtual ~DurationProfile() {}

  virtual void reset() {
    mi4Count = mnsStart = mnsEnd = mnsTotal = 0;
    mbIsWarning = false;
  }

  virtual void pulse_up() {
    struct timeval t;
    gettimeofday(&t, NULL);
    mnsStart = (t.tv_sec) * 1000000000LL + (t.tv_usec) * 1000LL;
    mbIsWarning = false;
  }
  virtual void pulse_down() {
    struct timeval t;
    gettimeofday(&t, NULL);
    mnsEnd = (t.tv_sec) * 1000000000LL + (t.tv_usec) * 1000LL;
    mbIsWarning = false;
    if (mnsStart != 0) {
      int64_t duration = mnsEnd - mnsStart;
      mnsTotal += duration;
      mi4Count++;
      mbIsWarning = print_overtime(duration);
      mnsStart = 0;
    }
  }
  virtual int64_t getAvgDuration() const {  // in milli-seconds
    return mi4Count == 0 ? 0 : NSCam::Utils::ns2ms(mnsTotal) / mi4Count;
  }
  virtual float getFps() const {
    return mnsTotal == 0
               ? 0
               : (static_cast<float>(mi4Count) / mnsTotal) * 1000000000LL;
  }
  virtual bool isWarning() const { return mbIsWarning; }

 protected:
  virtual bool print_overtime(int64_t duration) const {
    if (duration > mnsWarning) {
      CAM_LOGI("[%s] duration(%" PRId64 ") > (%" PRId64 ")", msSubjectName,
               duration, mnsWarning);
      return true;
    }
    return false;
  }

 protected:
  char const* msSubjectName;
  int32_t mi4Count;
  int64_t mnsStart;
  int64_t mnsEnd;
  int64_t mnsTotal;
  int64_t mnsWarning;
  bool mbIsWarning;
};
};  // namespace v3
};  // namespace NSCam

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_PIPELINE_HWNODE_PROFILE_H_
