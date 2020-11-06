// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include <ctype.h>
#include <libgen.h>
#include <linux/media.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <unistd.h>

#include "label_detect.h"

/* Checks if the given device is a USB camera that is not vivid. */
static bool is_real_usb_camera(int fd) {
  struct v4l2_capability cap;

  if (do_ioctl(fd, VIDIOC_QUERYCAP, &cap))
    return false;

  /* we assume all the UVC devices on Chrome OS are USB cameras. */
  return strcmp((const char*)cap.driver, "uvcvideo") == 0;
}

/* Checks if the given device is a vivid emulating a USB camera. */
bool is_vivid_camera(int fd) {
  struct v4l2_capability cap;

  if (do_ioctl(fd, VIDIOC_QUERYCAP, &cap))
    return false;

  if (strcmp((const char*)cap.driver, "vivid"))
    return false;

  /* Check if vivid is emulating a video capture device. */
  bool check_caps = false;
  check_caps |= (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
                !(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT);

  check_caps |= (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) &&
                !(cap.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE);

  return check_caps;
}

/* Fills vendor_id with idVendor for a given device */
static bool get_vendor_id(const char* dev_path, char vendor_id[5]) {
  /* Copy dev_path because basename() may modify its contents. */
  char* copied_dev_path = strdup(dev_path);
  if (copied_dev_path == NULL)
    return false;

  char* dev_name = basename(copied_dev_path);
  char vid_path[MAXPATHLEN];
  snprintf(vid_path, MAXPATHLEN, "/sys/class/video4linux/%s/device/../idVendor",
           dev_name);

  FILE* fp = fopen(vid_path, "r");
  if (fp == NULL) {
    TRACE("failed to open %s\n", vid_path);
    free(copied_dev_path);
    return false;
  }

  bool ret = true;
  if (fgets(vendor_id, 5, fp) == NULL) {
    TRACE("failed to read %s\n", vid_path);
    ret = false;
  }

  free(copied_dev_path);
  fclose(fp);
  return ret;
}

/* Checks if the device is a builtin USB camera. */
static bool is_builtin_usb_camera(const char* dev_path, int fd) {
  if (!is_real_usb_camera(fd))
    return false;

  /*
   * Check if the camera is not an external one.
   * We assume that all external cameras in the lab are made by Logitech.
   *
   * TODO(keiichiw): If non-Logitech external cameras are used in the lab,
   * we need to add more vendor IDs here.
   * If there are many kinds of external cameras, we might want to have a list
   * of vid:pid of builtin cameras instead.
   */
  const char kLogitechVendorId[] = "046d";
  char vendor_id[5];
  if (!get_vendor_id(dev_path, vendor_id)) {
    TRACE("failed to get vendor ID\n");
    return false;
  }

  return strcmp(vendor_id, kLogitechVendorId) != 0;
}

/* Checks if the device is a builtin MIPI camera. */
static bool is_builtin_mipi_camera(int fd) {
  struct media_device_info info;
  struct media_entity_desc desc;

  memset(&info, 0, sizeof(info));
  if (do_ioctl(fd, MEDIA_IOC_DEVICE_INFO, &info) != 0) {
    TRACE("failed to get media device info\n");
    return false;
  }
  if (strcmp(info.driver, "uvcvideo") == 0)
    return false;

  memset(&desc, 0, sizeof(desc));
  for (desc.id = MEDIA_ENT_ID_FLAG_NEXT;
       !do_ioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &desc);
       desc.id |= MEDIA_ENT_ID_FLAG_NEXT) {
    if (desc.type == MEDIA_ENT_T_V4L2_SUBDEV_SENSOR)
      return true;
  }

  return false;
}

/*
 * Exported functions
 */

static const char kVideoDeviceName[] = "/dev/video*";

/* Determines "builtin_usb_camera" label. */
bool detect_builtin_usb_camera(void) {
  return is_any_device_with_path(kVideoDeviceName, is_builtin_usb_camera);
}

/* Determines "builtin_mipi_camera" label. */
bool detect_builtin_mipi_camera(void) {
  static const char kMediaDeviceName[] = "/dev/media*";

  return is_any_device(kMediaDeviceName, is_builtin_mipi_camera);
}

/* Determines "builtin_vivid_camera" label. */
bool detect_vivid_camera(void) {
  return is_any_device(kVideoDeviceName, is_vivid_camera);
}

/* Determines "builtin_camera" label. */
bool detect_builtin_camera(void) {
  return detect_builtin_usb_camera() || detect_builtin_mipi_camera();
}

/* Determines "builtin_or_vivid_camera" label. */
bool detect_builtin_or_vivid_camera(void) {
  return detect_builtin_camera() || detect_vivid_camera();
}
