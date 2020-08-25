// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screenshot/capture.h"

#include <sys/mman.h>

#include <utility>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "screenshot/crtc.h"

namespace screenshot {

GbmBoMap::GbmBoMap(ScopedGbmDevicePtr device,
                   ScopedGbmBoPtr bo,
                   uint32_t x,
                   uint32_t y,
                   uint32_t width,
                   uint32_t height)
    : device_(std::move(device)),
      bo_(std::move(bo)),
      width_(width),
      height_(height) {
  buffer_ = gbm_bo_map2(bo_.get(), x, y, width, height, GBM_BO_TRANSFER_READ,
                        &stride_, &map_data_, 0);
  PCHECK(buffer_ != MAP_FAILED) << "gbm_bo_map failed";
}

GbmBoMap::~GbmBoMap() {
  gbm_bo_unmap(bo_.get(), map_data_);
}

std::unique_ptr<GbmBoMap> Capture(
    const Crtc& crtc, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
  ScopedGbmDevicePtr device(gbm_create_device(crtc.file().GetPlatformFile()));
  CHECK(device) << "gbm_create_device failed";

  base::ScopedFD buffer_fd;
  {
    int fd;
    int rv = drmPrimeHandleToFD(crtc.file().GetPlatformFile(),
                                crtc.fb()->handle, 0, &fd);
    CHECK_EQ(rv, 0) << "drmPrimeHandleToFD failed";
    buffer_fd.reset(fd);
  }

  ScopedGbmBoPtr bo;
  {
    gbm_import_fd_data fd_data = {
        buffer_fd.get(),
        crtc.fb()->width,
        crtc.fb()->height,
        crtc.fb()->pitch,
        // TODO(djmk): The buffer format is hardcoded to ARGB8888, we should fix
        // this to query for the frambuffer's format instead.
        GBM_FORMAT_ARGB8888,
    };
    bo.reset(gbm_bo_import(device.get(), GBM_BO_IMPORT_FD, &fd_data,
                           GBM_BO_USE_SCANOUT));
  }
  CHECK(bo) << "gbm_bo_import failed";

  return std::make_unique<GbmBoMap>(std::move(device), std::move(bo), x, y,
                                    width, height);
}

}  // namespace screenshot
