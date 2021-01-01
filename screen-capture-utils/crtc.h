// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SCREEN_CAPTURE_UTILS_CRTC_H_
#define SCREEN_CAPTURE_UTILS_CRTC_H_

#include <memory>
#include <utility>
#include <vector>

#include <base/files/file.h>
#include <base/macros.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "screen-capture-utils/ptr_util.h"

namespace screenshot {

struct PlanePosition {
  int32_t x;
  int32_t y;
  uint32_t w;
  uint32_t h;
};

class Crtc {
 public:
  using PlaneInfo = std::pair<ScopedDrmModeFB2Ptr, PlanePosition>;

  Crtc(base::File file,
       ScopedDrmModeConnectorPtr connector,
       ScopedDrmModeEncoderPtr encoder,
       ScopedDrmModeCrtcPtr crtc,
       ScopedDrmModeFBPtr fb,
       ScopedDrmModeFB2Ptr fb2);

  Crtc(base::File file,
       ScopedDrmModeConnectorPtr connector,
       ScopedDrmModeEncoderPtr encoder,
       ScopedDrmModeCrtcPtr crtc,
       ScopedDrmModeFBPtr fb,
       std::vector<PlaneInfo> planes);
  Crtc(const Crtc&) = delete;
  Crtc& operator=(const Crtc&) = delete;

  const base::File& file() const { return file_; }
  drmModeConnector* connector() const { return connector_.get(); }
  drmModeEncoder* encoder() const { return encoder_.get(); }
  drmModeCrtc* crtc() const { return crtc_.get(); }

  drmModeFB* fb() const { return fb_.get(); }
  drmModeFB2* fb2() const { return fb2_.get(); }
  const std::vector<PlaneInfo>& planes() const { return planes_; }

  uint32_t width() const { return crtc_->width; }
  uint32_t height() const { return crtc_->height; }

  bool IsInternalDisplay() const;

 private:
  base::File file_;
  ScopedDrmModeConnectorPtr connector_;
  ScopedDrmModeEncoderPtr encoder_;
  ScopedDrmModeCrtcPtr crtc_;
  ScopedDrmModeFBPtr fb_;
  ScopedDrmModeFB2Ptr fb2_;

  std::vector<PlaneInfo> planes_;
};

class CrtcFinder {
 public:
  static std::unique_ptr<Crtc> FindAnyDisplay();
  static std::unique_ptr<Crtc> FindInternalDisplay();
  static std::unique_ptr<Crtc> FindExternalDisplay();
  static std::unique_ptr<Crtc> FindById(uint32_t crtc_id);

 private:
  CrtcFinder() = delete;
};

}  // namespace screenshot

#endif  // SCREEN_CAPTURE_UTILS_CRTC_H_
