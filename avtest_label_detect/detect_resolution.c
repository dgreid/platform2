// Copyright 2017 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// resolution detectors

#if defined(USE_V4L2_CODEC)
#include <linux/videodev2.h>
#endif  // defined(USE_V4L2_CODEC)

#if defined(USE_VAAPI)
#include <va/va.h>
#endif  // defined(USE_VAAPI)

#include "label_detect.h"

#if defined(USE_VAAPI) || defined(USE_V4L2_CODEC)
static const int32_t width_4k = 3840;
static const int32_t height_4k = 2160;
#endif

#if defined(USE_VAAPI)
#if VA_CHECK_VERSION(0, 35, 0)

static const char kDRMDevicePattern[] = "/dev/dri/renderD*";

static const VAProfile va_profiles_h264[] = {
    VAProfileH264Baseline, VAProfileH264Main, VAProfileH264High,
    VAProfileH264ConstrainedBaseline, VAProfileNone};

static const VAProfile va_profiles_vp8[] = {VAProfileVP8Version0_3,
                                            VAProfileNone};

static const VAProfile va_profiles_vp9[] = {VAProfileVP9Profile0,
                                            VAProfileNone};

/* Determines if a VAAPI device associated with given |fd| supports
 * |va_profiles| for |va_entrypoint|, and its maximum resolution is larger
 * than 3840x2160.
 */
static bool is_vaapi_4k_device(int fd,
                               const VAProfile* va_profiles,
                               VAEntrypoint va_entrypoint) {
  int32_t resolution_width = 0;
  int32_t resolution_height = 0;
  if (is_vaapi_support_formats(fd, va_profiles, va_entrypoint,
                               VA_RT_FORMAT_YUV420)) {
    if (get_vaapi_max_resolution(fd, va_profiles, va_entrypoint,
                                 &resolution_width, &resolution_height)) {
      return resolution_width >= width_4k && resolution_height >= height_4k;
    }
  }
  return false;
}

// Determines if is_vaapi_4k_device() for H264 decoding.
static bool is_vaapi_4k_device_dec_h264(int fd) {
  return is_vaapi_4k_device(fd, va_profiles_h264, VAEntrypointVLD);
}

// Determines if is_vaapi_4k_device() for H264 encoding.
static bool is_vaapi_4k_device_enc_h264(int fd) {
  return (is_vaapi_4k_device(fd, va_profiles_h264, VAEntrypointEncSlice) ||
          is_vaapi_4k_device(fd, va_profiles_h264, VAEntrypointEncSliceLP));
}

// Determines if is_vaapi_4k_device() for VP8 decoding.
static bool is_vaapi_4k_device_dec_vp8(int fd) {
  return is_vaapi_4k_device(fd, va_profiles_vp8, VAEntrypointVLD);
}

// Determines if is_vaapi_4k_device() for VP8 encoding.
static bool is_vaapi_4k_device_enc_vp8(int fd) {
  return (is_vaapi_4k_device(fd, va_profiles_vp8, VAEntrypointEncSlice) ||
          is_vaapi_4k_device(fd, va_profiles_vp8, VAEntrypointEncSliceLP));
}

// Determines if is_vaapi_4k_device() for VP9 decoding.
static bool is_vaapi_4k_device_dec_vp9(int fd) {
  return is_vaapi_4k_device(fd, va_profiles_vp9, VAEntrypointVLD);
}

// Determines if is_vaapi_4k_device() for VP9 encoding.
static bool is_vaapi_4k_device_enc_vp9(int fd) {
  return (is_vaapi_4k_device(fd, va_profiles_vp9, VAEntrypointEncSlice) ||
          is_vaapi_4k_device(fd, va_profiles_vp9, VAEntrypointEncSliceLP));
}
#endif  // VA_CHECK_VERSION(0, 38, 1)
#endif  // defined(USE_VAAPI)

#if defined(USE_V4L2_CODEC)

static const char kVideoDevicePattern[] = "/dev/video*";

/* Determined if a V4L2 device associated with given |fd| supports |pix_fmt|
 * for |buf_type|, and its maximum resolution is larger than 3840x2160.
 */
static bool is_v4l2_4k_device(int fd,
                              enum v4l2_buf_type buf_type,
                              uint32_t pix_fmt) {
  int32_t resolution_width;
  int32_t resolution_height;
  if (!is_hw_video_acc_device(fd)) {
    return false;
  }
  if (is_v4l2_support_format(fd, buf_type, pix_fmt)) {
    if (get_v4l2_max_resolution(fd, pix_fmt, &resolution_width,
                                &resolution_height)) {
      return resolution_width >= width_4k && resolution_height >= height_4k;
    }
  }
  return false;
}

// Determines if is_v4l2_4k_device() for H264 decoding.
static bool is_v4l2_4k_device_dec_h264(int fd) {
  return is_v4l2_4k_device(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                           V4L2_PIX_FMT_H264) ||
         is_v4l2_4k_device(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                           V4L2_PIX_FMT_H264_SLICE);
}

// Determines if is_v4l2_4k_device() for H264 encoding.
static bool is_v4l2_4k_device_enc_h264(int fd) {
  return is_v4l2_4k_device(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                           V4L2_PIX_FMT_H264);
}

// Determines if is_v4l2_4k_device() for VP8 decoding.
static bool is_v4l2_4k_device_dec_vp8(int fd) {
  return is_v4l2_4k_device(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                           V4L2_PIX_FMT_VP8) ||
         is_v4l2_4k_device(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                           V4L2_PIX_FMT_VP8_FRAME);
}

// Determines if is_v4l2_4k_device() for VP8 encoding.
static bool is_v4l2_4k_device_enc_vp8(int fd) {
  return is_v4l2_4k_device(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                           V4L2_PIX_FMT_VP8);
}

// Determines if is_v4l2_4k_device() for VP9 decoding.
static bool is_v4l2_4k_device_dec_vp9(int fd) {
  return is_v4l2_4k_device(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                           V4L2_PIX_FMT_VP9) ||
         is_v4l2_4k_device(fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                           V4L2_PIX_FMT_VP9_FRAME);
}

// Determines if is_v4l2_4k_device() for VP9 encoding.
static bool is_v4l2_4k_device_enc_vp9(int fd) {
  return is_v4l2_4k_device(fd, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                           V4L2_PIX_FMT_VP9);
}

#endif  // defined(USE_V4L2_CODEC)

/* Determines "4k_video_h264". Return true, if either the VAAPI device
 * supports 4k resolution H264 decoding, has decoding entry point,
 * and input YUV420 formats. Or there is a
 * /dev/video* device supporting 4k resolution H264 decoding.
 */
bool detect_4k_device_h264(void) {
#if defined(USE_VAAPI)
  if (is_any_device(kDRMDevicePattern, is_vaapi_4k_device_dec_h264))
    return true;
#endif  // defined(USE_VAAPI)

#if defined(USE_V4L2_CODEC)
  if (is_any_device(kVideoDevicePattern, is_v4l2_4k_device_dec_h264))
    return true;
#endif  // defined(USE_V4L2_CODEC)

  return false;
}

/* Determines "4k_video_vp8". Return true, if either the VAAPI device
 * supports 4k resolution VP8 decoding, has decoding entry point,
 * and input YUV420 formats. Or there is a
 * /dev/video* device supporting 4k resolution VP8 decoding.
 */
bool detect_4k_device_vp8(void) {
#if defined(USE_VAAPI)
  if (is_any_device(kDRMDevicePattern, is_vaapi_4k_device_dec_vp8))
    return true;
#endif  // defined(USE_VAAPI)

#if defined(USE_V4L2_CODEC)
  if (is_any_device(kVideoDevicePattern, is_v4l2_4k_device_dec_vp8))
    return true;
#endif  // defined(USE_V4L2_CODEC)

  return false;
}

/* Determines "4k_video_vp9". Return true, if either the VAAPI device
 * supports 4k resolution VP9 decoding, has decoding entry point,
 * and input YUV420 formats. Or there is a
 * /dev/video* device supporting 4k resolution VP9 decoding.
 */
bool detect_4k_device_vp9(void) {
#if defined(USE_VAAPI)
  if (is_any_device(kDRMDevicePattern, is_vaapi_4k_device_dec_vp9))
    return true;
#endif  // defined(USE_VAAPI)

#if defined(USE_V4L2_CODEC)
  if (is_any_device(kVideoDevicePattern, is_v4l2_4k_device_dec_vp9))
    return true;
#endif  // defined(USE_V4L2_CODEC)

  return false;
}

/* Determines "4k_video_enc_h264". Return true, if either the VAAPI device
 * supports 4k resolution H264 encoding, has encoding entry point,
 * and input YUV420 formats. Or there is a
 * /dev/video* device supporting 4k resolution H264 encoding.
 */
bool detect_4k_device_enc_h264(void) {
#if defined(USE_VAAPI)
  if (is_any_device(kDRMDevicePattern, is_vaapi_4k_device_enc_h264))
    return true;
#endif  // defined(USE_VAAPI)

#if defined(USE_V4L2_CODEC)
  if (is_any_device(kVideoDevicePattern, is_v4l2_4k_device_enc_h264))
    return true;
#endif  // defined(USE_V4L2_CODEC)

  return false;
}

/* Determines "4k_video_enc_vp8". Return true, if either the VAAPI device
 * supports 4k resolution VP8 encoding, has encoding entry point,
 * and input YUV420 formats. Or there is a
 * /dev/video* device supporting 4k resolution VP8 encoding.
 */
bool detect_4k_device_enc_vp8(void) {
#if defined(USE_VAAPI)
  if (is_any_device(kDRMDevicePattern, is_vaapi_4k_device_enc_vp8))
    return true;
#endif  // defined(USE_VAAPI)

#if defined(USE_V4L2_CODEC)
  if (is_any_device(kVideoDevicePattern, is_v4l2_4k_device_enc_vp8))
    return true;
#endif  // defined(USE_V4L2_CODEC)

  return false;
}

/* Determines "4k_video_enc_vp9". Return true, if either the VAAPI device
 * supports 4k resolution VP9 encoding, has encoding entry point,
 * and input YUV420 formats. Or there is a
 * /dev/video* device supporting 4k resolution VP9 encoding.
 */
bool detect_4k_device_enc_vp9(void) {
#if defined(USE_VAAPI)
  if (is_any_device(kDRMDevicePattern, is_vaapi_4k_device_enc_vp9))
    return true;
#endif  // defined(USE_VAAPI)

#if defined(USE_V4L2_CODEC)
  if (is_any_device(kVideoDevicePattern, is_v4l2_4k_device_enc_vp9))
    return true;
#endif  // defined(USE_V4L2_CODEC)

  return false;
}
