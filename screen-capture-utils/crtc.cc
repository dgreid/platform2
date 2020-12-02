// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "screen-capture-utils/crtc.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <base/files/file_enumerator.h>
#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/stl_util.h>

namespace screenshot {

namespace {

constexpr const char kDrmDeviceDir[] = "/dev/dri";
constexpr const char kDrmDeviceGlob[] = "card?";

bool PopulatePlanePosition(int fd, uint32_t plane_id, PlanePosition* pos) {
  struct {
    const char* name;
    uint64_t val;
  } crtc_props[4] = {
      {"CRTC_X", 0}, {"CRTC_Y", 0}, {"CRTC_W", 0}, {"CRTC_H", 0},
      // TODO(dcastagna): Handle SRC_ and rotation
  };

  ScopedDrmObjectPropertiesPtr props(
      drmModeObjectGetProperties(fd, plane_id, DRM_MODE_OBJECT_PLANE));
  if (!props) {
    return false;
  }

  int found = 0;
  for (int i = 0; i < props->count_props; i++) {
    ScopedDrmPropertyPtr prop(drmModeGetProperty(fd, props->props[i]));
    if (!prop) {
      continue;
    }

    for (int j = 0; j < base::size(crtc_props); j++) {
      if (strcmp(crtc_props[j].name, prop->name) == 0) {
        crtc_props[j].val = props->prop_values[i];
        found++;
      }
    }
  }

  if (found != base::size(crtc_props)) {
    return false;
  }

  pos->x = static_cast<int32_t>(crtc_props[0].val);
  pos->y = static_cast<int32_t>(crtc_props[1].val);
  pos->w = static_cast<uint32_t>(crtc_props[2].val);
  pos->h = static_cast<uint32_t>(crtc_props[3].val);
  return true;
}

std::vector<std::unique_ptr<Crtc>> GetConnectedCrtcs() {
  std::vector<std::unique_ptr<Crtc>> crtcs;

  std::vector<base::FilePath> paths;
  {
    base::FileEnumerator lister(base::FilePath(kDrmDeviceDir), false,
                                base::FileEnumerator::FILES, kDrmDeviceGlob);
    for (base::FilePath name = lister.Next(); !name.empty();
         name = lister.Next()) {
      paths.push_back(name);
    }
  }
  std::sort(paths.begin(), paths.end());

  for (base::FilePath path : paths) {
    base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ |
                              base::File::FLAG_WRITE);
    if (!file.IsValid())
      continue;

    // Set CAP_ATOMIC so we can query all planes and plane properties.
    bool atomic_modeset =
        drmSetClientCap(file.GetPlatformFile(), DRM_CLIENT_CAP_ATOMIC, 1) == 0;

    ScopedDrmModeResPtr resources(drmModeGetResources(file.GetPlatformFile()));
    if (!resources)
      continue;

    for (int index_connector = 0; index_connector < resources->count_connectors;
         ++index_connector) {
      ScopedDrmModeConnectorPtr connector(drmModeGetConnector(
          file.GetPlatformFile(), resources->connectors[index_connector]));
      if (!connector || connector->encoder_id == 0)
        continue;

      ScopedDrmModeEncoderPtr encoder(
          drmModeGetEncoder(file.GetPlatformFile(), connector->encoder_id));
      if (!encoder || encoder->crtc_id == 0)
        continue;

      ScopedDrmModeCrtcPtr crtc(
          drmModeGetCrtc(file.GetPlatformFile(), encoder->crtc_id));
      if (!crtc || !crtc->mode_valid || crtc->buffer_id == 0)
        continue;

      ScopedDrmModeFBPtr fb(
          drmModeGetFB(file.GetPlatformFile(), crtc->buffer_id));

      ScopedDrmModeFB2Ptr fb2(
          drmModeGetFB2(file.GetPlatformFile(), crtc->buffer_id));

      if (!fb && !fb2) {
        LOG(ERROR) << "getfb failed";
        continue;
      }

      std::unique_ptr<Crtc> res_crtc;

      // Multiplane is only handled by egl_capture, so don't bother if
      // GETFB2 isn't supported.
      if (fb2 && atomic_modeset) {
        ScopedDrmPlaneResPtr plane_res(
            drmModeGetPlaneResources(file.GetPlatformFile()));
        CHECK(plane_res) << " Failed to get plane resources";

        std::vector<Crtc::PlaneInfo> planes;
        for (uint32_t i = 0; i < plane_res->count_planes; i++) {
          ScopedDrmPlanePtr plane(
              drmModeGetPlane(file.GetPlatformFile(), plane_res->planes[i]));
          if (plane->crtc_id != encoder->crtc_id) {
            continue;
          }

          PlanePosition pos{};
          bool res = PopulatePlanePosition(file.GetPlatformFile(),
                                           plane->plane_id, &pos);
          if (!res) {
            LOG(WARNING) << "Failed to query plane position, skipping.\n";
            continue;
          }
          ScopedDrmModeFB2Ptr fb_info(
              drmModeGetFB2(file.GetPlatformFile(), plane->fb_id));
          if (!fb_info) {
            LOG(WARNING) << "Failed to query plane fb info, skipping.\n";
            continue;
          }

          planes.push_back(std::make_pair(std::move(fb_info), pos));
        }

        if (!planes.empty()) {
          res_crtc = std::make_unique<Crtc>(
              file.Duplicate(), std::move(connector), std::move(encoder),
              std::move(crtc), std::move(planes));
        }
      }

      if (!res_crtc) {
        res_crtc = std::make_unique<Crtc>(
            file.Duplicate(), std::move(connector), std::move(encoder),
            std::move(crtc), std::move(fb), std::move(fb2));
      }

      crtcs.push_back(std::move(res_crtc));
    }
  }

  return crtcs;
}

}  // namespace

Crtc::Crtc(base::File file,
           ScopedDrmModeConnectorPtr connector,
           ScopedDrmModeEncoderPtr encoder,
           ScopedDrmModeCrtcPtr crtc,
           ScopedDrmModeFBPtr fb,
           ScopedDrmModeFB2Ptr fb2)
    : file_(std::move(file)),
      connector_(std::move(connector)),
      encoder_(std::move(encoder)),
      crtc_(std::move(crtc)),
      fb_(std::move(fb)),
      fb2_(std::move(fb2)) {}

Crtc::Crtc(base::File file,
           ScopedDrmModeConnectorPtr connector,
           ScopedDrmModeEncoderPtr encoder,
           ScopedDrmModeCrtcPtr crtc,
           std::vector<PlaneInfo> planes)
    : file_(std::move(file)),
      connector_(std::move(connector)),
      encoder_(std::move(encoder)),
      crtc_(std::move(crtc)),
      planes_(std::move(planes)) {}

bool Crtc::IsInternalDisplay() const {
  switch (connector_->connector_type) {
    case DRM_MODE_CONNECTOR_eDP:
    case DRM_MODE_CONNECTOR_LVDS:
    case DRM_MODE_CONNECTOR_DSI:
    case DRM_MODE_CONNECTOR_VIRTUAL:
      return true;
    default:
      return false;
  }
}

// static
std::unique_ptr<Crtc> CrtcFinder::FindAnyDisplay() {
  auto crtcs = GetConnectedCrtcs();
  if (crtcs.empty())
    return nullptr;
  return std::move(crtcs[0]);
}

// static
std::unique_ptr<Crtc> CrtcFinder::FindInternalDisplay() {
  auto crtcs = GetConnectedCrtcs();
  for (auto& crtc : crtcs)
    if (crtc->IsInternalDisplay())
      return std::move(crtc);
  return nullptr;
}

// static
std::unique_ptr<Crtc> CrtcFinder::FindExternalDisplay() {
  auto crtcs = GetConnectedCrtcs();
  for (auto& crtc : crtcs)
    if (!crtc->IsInternalDisplay())
      return std::move(crtc);
  return nullptr;
}

// static
std::unique_ptr<Crtc> CrtcFinder::FindById(uint32_t crtc_id) {
  auto crtcs = GetConnectedCrtcs();
  for (auto& crtc : crtcs)
    if (crtc->crtc()->crtc_id == crtc_id)
      return std::move(crtc);
  return nullptr;
}

}  // namespace screenshot
