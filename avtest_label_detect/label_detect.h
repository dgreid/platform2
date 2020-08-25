// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef AVTEST_LABEL_DETECT_LABEL_DETECT_H_
#define AVTEST_LABEL_DETECT_LABEL_DETECT_H_

#include <stdbool.h>
#include <stdint.h>

#if defined(USE_V4L2_CODEC)
#include <linux/videodev2.h>
#endif  // defined(USE_V4L2_CODEC)

#if defined(USE_VAAPI)
#include <va/va.h>
#endif  // defined (USE_VAAPI)

/* main.c */
extern int verbose;
#define TRACE(...)         \
  do {                     \
    if (verbose)           \
      printf(__VA_ARGS__); \
  } while (0)

/* table_lookup.c */
extern void detect_label_by_board_name(void);

/* util.c */
extern int do_ioctl(int fd, int request, void* arg);
extern bool is_any_device(const char* pattern, bool (*func)(int fd));
extern bool is_any_device_with_path(const char* pattern,
                                    bool (*func)(const char* dev_path, int fd));
extern void convert_fourcc_to_str(uint32_t fourcc, char* str);

/* util_v4l2 */
#if defined(USE_V4L2_CODEC)
extern bool is_v4l2_support_format(int fd,
                                   enum v4l2_buf_type buf_type,
                                   uint32_t fourcc);
extern bool is_hw_video_acc_device(int fd);
extern bool is_hw_jpeg_acc_device(int fd);
bool get_v4l2_max_resolution(int fd,
                             uint32_t fourcc,
                             int32_t* const resolution_width,
                             int32_t* const resolution_height);
#endif  // defined(USE_V4L2_CODEC)

/* util_vaapi */
#if defined(USE_VAAPI)
bool is_vaapi_support_formats(int fd,
                              const VAProfile* profiles,
                              VAEntrypoint entrypoint,
                              unsigned int format);
bool get_vaapi_max_resolution(int fd,
                              const VAProfile* profiles,
                              VAEntrypoint entrypoit,
                              int32_t* const resolution_width,
                              int32_t* const resolution_height);
#endif  // defined(USE_VAAPI)

/* detectors */
extern bool detect_builtin_usb_camera(void);
extern bool detect_builtin_mipi_camera(void);
extern bool detect_vivid_camera(void);
extern bool detect_builtin_camera(void);
extern bool detect_builtin_or_vivid_camera(void);
extern bool detect_video_acc_h264(void);
extern bool detect_video_acc_vp8(void);
extern bool detect_video_acc_vp9(void);
extern bool detect_video_acc_vp9_2(void);
extern bool detect_video_acc_enc_h264(void);
extern bool detect_video_acc_enc_vp8(void);
extern bool detect_video_acc_enc_vp9(void);
extern bool detect_jpeg_acc_dec(void);
extern bool detect_jpeg_acc_enc(void);
bool detect_4k_device_h264(void);
bool detect_4k_device_vp8(void);
bool detect_4k_device_vp9(void);
bool detect_4k_device_enc_h264(void);
bool detect_4k_device_enc_vp8(void);
bool detect_4k_device_enc_vp9(void);
#endif  // AVTEST_LABEL_DETECT_LABEL_DETECT_H_
