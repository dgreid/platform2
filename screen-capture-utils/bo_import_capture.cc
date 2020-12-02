// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screen-capture-utils/bo_import_capture.h"

#include <sys/mman.h>

#include <utility>

#include <base/files/scoped_file.h>
#include <base/logging.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "screen-capture-utils/crtc.h"

namespace screenshot {

GbmBoDisplayBuffer::GbmBoDisplayBuffer(
    const Crtc* crtc, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    : crtc_(*crtc),
      device_(gbm_create_device(crtc_.file().GetPlatformFile())),
      x_(x),
      y_(y),
      width_(width),
      height_(height) {
  CHECK(device_) << "gbm_create_device failed";

  {
    int fd;
    int rv = drmPrimeHandleToFD(crtc_.file().GetPlatformFile(),
                                crtc_.fb()->handle, 0, &fd);
    CHECK_EQ(rv, 0) << "drmPrimeHandleToFD failed";
    buffer_fd_.reset(fd);
  }

  buffer_ = gbm_bo_map2(bo_.get(), x_, y_, width_, height_,
                        GBM_BO_TRANSFER_READ, &stride_, &map_data_, 0);
  PCHECK(buffer_ != MAP_FAILED) << "gbm_bo_map failed";
}

GbmBoDisplayBuffer::~GbmBoDisplayBuffer() {
  gbm_bo_unmap(bo_.get(), map_data_);
}

DisplayBuffer::Result GbmBoDisplayBuffer::Capture() {
  gbm_import_fd_data fd_data = {
      buffer_fd_.get(),
      crtc_.fb()->width,
      crtc_.fb()->height,
      crtc_.fb()->pitch,
      // TODO(djmk): The buffer format is hardcoded to ARGB8888, we should fix
      // this to query for the frambuffer's format instead.
      GBM_FORMAT_ARGB8888,
  };
  bo_.reset(gbm_bo_import(device_.get(), GBM_BO_IMPORT_FD, &fd_data,
                          GBM_BO_USE_SCANOUT));
  CHECK(bo_.get()) << "gbm_bo_import failed";

  return {width_, height_, stride_, buffer_};
}

}  // namespace screenshot
