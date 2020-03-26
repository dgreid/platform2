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

#define LOG_TAG "FDVT_Drv_V4L2"

#include "mtkcam/drv/fdvt/4.0/cam_fdvt_v4l2.h"

#include <mutex>
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <mtkcam/utils/std/Log.h>
#include <poll.h>
#include <property_service/property_lib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

/******************************************************************************
 *
 *****************************************************************************/
//-------------------------------------------//
//  Global face detection related parameter  //
//-------------------------------------------//
#define MTK_V4L2_FMT_NOT_SUPPORT 0
#define MAX_SCAN_DEV_COUNT 32

#define V4L2_CID_USER_MTK_FD_BASE (V4L2_CID_USER_BASE + 0x10c0)
/* Set the face angle and directions to be detected */
#define V4L2_CID_MTK_FD_DETECT_POSE (V4L2_CID_USER_MTK_FD_BASE + 1)
/* Set image widths for an input image to be scaled down for face detection */
#define V4L2_CID_MTK_FD_SCALE_DOWN_IMG_WIDTH (V4L2_CID_USER_MTK_FD_BASE + 2)
/* Set image heights for an input image to be scaled down for face detection */
#define V4L2_CID_MTK_FD_SCALE_DOWN_IMG_HEIGHT (V4L2_CID_USER_MTK_FD_BASE + 3)
/* Set the length of scale down size array */
#define V4L2_CID_MTK_FD_SCALE_IMG_NUM (V4L2_CID_USER_MTK_FD_BASE + 4)
/* Set the detection speed, usually reducing accuracy. */
#define V4L2_CID_MTK_FD_DETECT_SPEED (V4L2_CID_USER_MTK_FD_BASE + 5)
/* Select the detection model or algorithm to be used. */
#define V4L2_CID_MTK_FD_DETECTION_MODEL (V4L2_CID_USER_MTK_FD_BASE + 6)

#define MAX_V4L2_CONTROL 6

enum face_angle {
  MTK_FACE_FRONT,
  MTK_FACE_RIGHT_50,
  MTK_FACE_LEFT_50,
  MTK_FACE_RIGHT_90,
  MTK_FACE_LEFT_90,
  MTK_FACE_ANGLE_NUM,
};

struct face_direction_def {
  MUINT16 MTK_FD_FACE_DIR_0 : 1, MTK_FD_FACE_DIR_30 : 1, MTK_FD_FACE_DIR_60 : 1,
      MTK_FD_FACE_DIR_90 : 1, MTK_FD_FACE_DIR_120 : 1, MTK_FD_FACE_DIR_150 : 1,
      MTK_FD_FACE_DIR_180 : 1, MTK_FD_FACE_DIR_210 : 1, MTK_FD_FACE_DIR_240 : 1,
      MTK_FD_FACE_DIR_270 : 1, MTK_FD_FACE_DIR_300 : 1,
      MTK_FD_FACE_DIR_330 : 1, : 4;
};

#define MEDIA_FD_DEVICE_MODEL "mtk-fd-4.0"
#define MEDIA_FD_ENITY_NAME "mtk-fd-4.0-source"

/******************************************************************************
 *
 ******************************************************************************/
static MINT32 g_fdFDVT = -1;
static std::mutex g_FDInitMutex;
static bool g_IsFirstEnque = true;
static MINT32 g_UserCount = 0;
static MBOOL g_EnquedStatus = MFALSE;

/******************************************************************************
 * Data structure
 *****************************************************************************/
struct buffer {
  void* start;
  size_t length;
};

struct fdvt_device {
  int fd = -1;
  struct buffer* bufs[2];
  int bufs_mapped_cnt[2];
};

struct fdvt_inport_info {
  unsigned int fmt;
  unsigned int width;
  unsigned int height;
};

struct fdvt_system {
  char match_name[MATCH_NAME_STR_SIZE_MAX];
  struct fdvt_device node;
  struct fdvt_inport_info in_port_info;
  int media_ctrl_fd;
  int req_fd;
};

static struct fdvt_system g_fdvt_ctx;

/******************************************************************************
 * Static local function declear
 ******************************************************************************/
static int xioctl(int fh, int request, void* arg);
static void show_errno(const char* s);
static int set_img_format(int fd,
                          struct v4l2_format* fmt,
                          unsigned int buf_type,
                          unsigned int pixelformat,
                          int width,
                          int height);
static int get_img_format(int fd,
                          struct v4l2_format* fmt,
                          unsigned int buf_type,
                          unsigned int pixelformat,
                          int width,
                          int height);
static int open_device(char* dev_name);
static int queue_buf(int fd, unsigned int buf_type, int index);
static int queue_dma_buf(
    int fd, unsigned int buf_type, int index, int size, int req_fd, int dma_fd);
static void stream_on(int fd, unsigned int buf_type);
static void stream_off(int fd, unsigned int buf_type);
static void unmmap_buffer(struct buffer* buffers,
                          unsigned int n_buffers,
                          v4l2_memory mem_type);
static void close_device(int* fd);
static int request_buffers(int fd,
                           unsigned int buf_type,
                           int buf_count,
                           v4l2_memory mem_type);
static int query_map_buffer(int m2m_fd,
                            int req_count,
                            struct buffer** buffer_info,
                            int* mapped_count,
                            unsigned int buf_type,
                            v4l2_memory mem_type);
static void setup_meta_data(int fd, FdDrv_input_struct* FdDrv_input);
static int dequeue_buf(int fd, unsigned buf_type, v4l2_memory mem_type);
static int open_fdvt_media_entities();
static MUINT32 get_v4l2_image_format(MUINT8 fmt);

/******************************************************************************
 * FDVT Function
 *****************************************************************************/
MINT32 FDVT_IOCTL_CloseDriver() {
  if (g_fdFDVT > 0) {
    close(g_fdFDVT);
    g_fdFDVT = -1;
  }

  return S_Detection_OK;
}

MINT32 FDVT_OpenDriverWithUserCount(MUINT32 learning_type) {
  std::lock_guard<std::mutex> lock(g_FDInitMutex);

  if (g_UserCount == 0) {
    MY_LOGI("FDVT_Init, HW FD Open CLK");
    g_fdvt_ctx.node.bufs[0] = g_fdvt_ctx.node.bufs[1] = nullptr;
    g_fdvt_ctx.node.fd = g_fdvt_ctx.req_fd = g_fdvt_ctx.media_ctrl_fd = -1;
    int ret = open_fdvt_media_entities();
    if (S_Detection_OK != ret) {
      MY_LOGE("FDVT_IOCTL_OpenDriver failed");
      return E_Detection_Driver_Fail;
    }
  } else if (g_UserCount < 0) {
    MY_LOGE("FDVT UserCount(%d) < 0", g_UserCount);
    return E_Detection_Driver_Fail;
  }
  ++g_UserCount;

  return g_UserCount;
}

void FDVT_Close_Driver() {
  if (g_fdvt_ctx.node.fd > 0) {
    stream_off(g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_META_CAPTURE);
    stream_off(g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
  }

  if (g_fdvt_ctx.node.bufs[1] != nullptr) {
    unmmap_buffer(g_fdvt_ctx.node.bufs[1], g_fdvt_ctx.node.bufs_mapped_cnt[1],
                  V4L2_MEMORY_MMAP);
    g_fdvt_ctx.node.bufs[1] = nullptr;
  }

  if (g_fdvt_ctx.node.fd > 0) {
    request_buffers(g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_META_CAPTURE, 0,
                    V4L2_MEMORY_MMAP);
    request_buffers(g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 0,
                    V4L2_MEMORY_DMABUF);
  }

  if (g_fdvt_ctx.node.fd > 0) {
    close_device(&g_fdvt_ctx.node.fd);
    g_fdvt_ctx.node.fd = -1;
  }

  if (g_fdvt_ctx.req_fd > 0) {
    close_device(&g_fdvt_ctx.req_fd);
    g_fdvt_ctx.req_fd = -1;
  }

  if (g_fdvt_ctx.media_ctrl_fd > 0) {
    close_device(&g_fdvt_ctx.media_ctrl_fd);
    g_fdvt_ctx.media_ctrl_fd = -1;
  }

  g_IsFirstEnque = true;
}

MINT32 FDVT_CloseDriverWithUserCount() {
  std::lock_guard<std::mutex> lock(g_FDInitMutex);

  --g_UserCount;
  if (g_UserCount == 0) {
    MY_LOGI("FDVT_Uninit, HW FD Close CLK");
    FDVT_Close_Driver();
  }

  return g_UserCount;
}

void FDVT_Stream_On(FdDrv_input_struct* FdDrv_input) {
  g_fdvt_ctx.in_port_info.fmt =
      get_v4l2_image_format(FdDrv_input->source_img_fmt);
  struct v4l2_format v4l2_fmt_in;
  int meta_out_buf_cnt = 0;

  MY_LOGD("img fmt: %d, v4l2 img fmt: 0x%x", FdDrv_input->source_img_fmt,
          g_fdvt_ctx.in_port_info.fmt);

  g_fdvt_ctx.in_port_info.width = FdDrv_input->source_img_width[0];
  g_fdvt_ctx.in_port_info.height = FdDrv_input->source_img_height[0];

  set_img_format(g_fdvt_ctx.node.fd, &v4l2_fmt_in,
                 V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, g_fdvt_ctx.in_port_info.fmt,
                 g_fdvt_ctx.in_port_info.width, g_fdvt_ctx.in_port_info.height);

  request_buffers(g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, 1,
                  V4L2_MEMORY_DMABUF);

  meta_out_buf_cnt = request_buffers(
      g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_META_CAPTURE, 1, V4L2_MEMORY_MMAP);

  query_map_buffer(g_fdvt_ctx.node.fd, meta_out_buf_cnt,
                   &g_fdvt_ctx.node.bufs[1],
                   &g_fdvt_ctx.node.bufs_mapped_cnt[1],
                   V4L2_BUF_TYPE_META_CAPTURE, V4L2_MEMORY_MMAP);

  stream_on(g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
  stream_on(g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_META_CAPTURE);

  xioctl(g_fdvt_ctx.media_ctrl_fd, MEDIA_IOC_REQUEST_ALLOC, &g_fdvt_ctx.req_fd);
}

void FDVT_Enque(FdDrv_input_struct* FdDrv_input) {
  struct v4l2_format v4l2_fmt_in;
  /* Support single buffer en-enque */
  int frameIdx = 0;
  unsigned int sizeimage;

  MY_LOGD("FDVT Enque Start");

  g_EnquedStatus = true;

  if (g_IsFirstEnque) {
    FDVT_Stream_On(FdDrv_input);
    g_IsFirstEnque = false;
  }

  sizeimage = get_img_format(
      g_fdvt_ctx.node.fd, &v4l2_fmt_in, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
      g_fdvt_ctx.in_port_info.fmt, g_fdvt_ctx.in_port_info.width,
      g_fdvt_ctx.in_port_info.height);

  // set input meta data
  FdDrv_input->RIP_feature =
      (FdDrv_input->RIP_feature < 13)
          ? pose[FdDrv_input->RIP_feature - FD_POSE_OFFEST]
          : pose[0];
  setup_meta_data(g_fdvt_ctx.node.fd, FdDrv_input);

  queue_dma_buf(g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE, frameIdx,
                sizeimage, g_fdvt_ctx.req_fd, FdDrv_input->memFd);

  xioctl(g_fdvt_ctx.req_fd, MEDIA_REQUEST_IOC_QUEUE, NULL);

  queue_buf(g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_META_CAPTURE, frameIdx);

  MY_LOGD("FDVT Enque End");
}

void FDVT_Deque(FdDrv_output_struct* FdDrv_output) {
  int frameIdx = 0;

  MY_LOGD("FDVT Deque Start");

  if (!g_EnquedStatus) {
    MY_LOGE("Should not call FD Deque before calling FD Enque!");
    return;
  }
  g_EnquedStatus = MFALSE;

  dequeue_buf(g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
              V4L2_MEMORY_DMABUF);
  dequeue_buf(g_fdvt_ctx.node.fd, V4L2_BUF_TYPE_META_CAPTURE, V4L2_MEMORY_MMAP);

  struct fd_user_output* fd_result =
      (struct fd_user_output*)(g_fdvt_ctx.node.bufs[1][frameIdx].start);

  FDVT_RIPindexFromHWtoFW(fd_result);
  FdDrv_output->new_face_number = fd_result->face_number;

  for (int i = 0; i < MAX_FACE_SEL_NUM; ++i) {
    FdDrv_output->face_candi_pos_x0[i] = fd_result->face[i].x0;
    FdDrv_output->face_candi_pos_y0[i] = fd_result->face[i].y0;
    FdDrv_output->face_candi_pos_x1[i] = fd_result->face[i].x1;
    FdDrv_output->face_candi_pos_y1[i] = fd_result->face[i].y1;
    FdDrv_output->face_feature_set_index[i] = fd_result->face[i].face_idx;
    FdDrv_output->rip_dir[i] = fd_result->face[i].rip_dir;
    FdDrv_output->rop_dir[i] = fd_result->face[i].rop_dir;
    FdDrv_output->face_reliabiliy_value[i] =
        fd_result->face[i].fcv1 | (fd_result->face[i].fcv2 << 11);
    FdDrv_output->display_flag[i] = MTRUE;
    FdDrv_output->detected_face_size_label[i] = fd_result->face[i].det_size;
  }

  xioctl(g_fdvt_ctx.req_fd, MEDIA_REQUEST_IOC_REINIT, NULL);
  MY_LOGD("FDVT Deque End && face_num = %d", FdDrv_output->new_face_number);
}

/******************************************************************************
 * FDVT function: Deprecated
 ******************************************************************************/

MINT32 FDVT_GetModelVersion() {
  return 117;
}

void FDVT_RIPindexFromHWtoFW(fd_user_output* FD_Result) {
  MUINT32 m = 0;

  for (m = 0; m < FD_Result->face_number; m++) {
    MY_LOGD("FD_Result[m].rip_dir = %d", FD_Result->face[m].rip_dir);
    switch (FD_Result->face[m].rip_dir) {
      case 0:
        FD_Result->face[m].rip_dir = 1;
        break;
      case 1:
        FD_Result->face[m].rip_dir = 2;
        break;
      case 2:
        FD_Result->face[m].rip_dir = 4;
        break;
      case 3:
        FD_Result->face[m].rip_dir = 6;
        break;
      case 4:
        FD_Result->face[m].rip_dir = 8;
        break;
      case 5:
        FD_Result->face[m].rip_dir = 10;
        break;
      case 6:
        FD_Result->face[m].rip_dir = 12;
        break;
      case 7:
        FD_Result->face[m].rip_dir = 11;
        break;
      case 8:
        FD_Result->face[m].rip_dir = 9;
        break;
      case 9:
        FD_Result->face[m].rip_dir = 7;
        break;
      case 10:
        FD_Result->face[m].rip_dir = 5;
        break;
      case 11:
        FD_Result->face[m].rip_dir = 3;
        break;
      default:
        FD_Result->face[m].rip_dir = 1;
        MY_LOGE(" Error !! Feature pose out of range in main.cpp !!");
        break;
    }
  }
}

/******************************************************************************
 * Local function
 ******************************************************************************/
static int xioctl(int fh, int request, void* arg) {
  int r = 0;

  do {
    r = ioctl(fh, request, arg);
  } while (-1 == r && EINTR == errno);

  if (r)
    MY_LOGE("%d error %d:%s", request, errno, strerror(errno));

  return r;
}

static void show_errno(const char* s) {
  MY_LOGE("%s error %d:%s", s, errno, strerror(errno));
}

static int set_img_format(int fd,
                          struct v4l2_format* fmt,
                          unsigned int buf_type,
                          unsigned int pixelformat,
                          int width,
                          int height) {
  CLEAR(*fmt);

  fmt->type = buf_type;

  /* invalid format params & change to use default format */
  if (pixelformat == MTK_V4L2_FMT_NOT_SUPPORT || width <= 0 || height <= 0) {
    pixelformat = V4L2_PIX_FMT_YUYV;
    width = 640;
    height = 480;
    MY_LOGE("Invalid params: apply default params");
  }

  fmt->fmt.pix_mp.pixelformat = pixelformat;
  fmt->fmt.pix_mp.width = width;
  fmt->fmt.pix_mp.height = height;

  // currently only support 1 plane
  fmt->fmt.pix_mp.num_planes = 1;

  if (xioctl(fd, VIDIOC_S_FMT, fmt)) {
    MY_LOGE("Failed to set fmt:0x%x (%d*%d)", fmt->fmt.pix_mp.pixelformat,
            fmt->fmt.pix_mp.width, fmt->fmt.pix_mp.height);
    return -1;
  }

  MY_LOGD("Format set: fmt(0x%x), w(%d), h(%d), plan_num(%d)",
          fmt->fmt.pix_mp.pixelformat, fmt->fmt.pix_mp.width,
          fmt->fmt.pix_mp.height, fmt->fmt.pix_mp.num_planes);

  return 0;
}

static int get_img_format(int fd,
                          struct v4l2_format* fmt,
                          unsigned int buf_type,
                          unsigned int pixelformat,
                          int width,
                          int height) {
  CLEAR(*fmt);

  fmt->type = buf_type;

  if (xioctl(fd, VIDIOC_G_FMT, fmt)) {
    MY_LOGE("Failed to get fmt:%d", fmt->type);
    return 0;
  }

  MY_LOGD("Format get: fmt(0x%x), w(%d), h(%d), size(%d), plan_num(%d)",
          fmt->fmt.pix_mp.pixelformat, fmt->fmt.pix_mp.width,
          fmt->fmt.pix_mp.height, fmt->fmt.pix_mp.plane_fmt[0].sizeimage,
          fmt->fmt.pix_mp.num_planes);

  return fmt->fmt.pix_mp.plane_fmt[0].sizeimage;
}

static int open_device(char* dev_name) {
  struct stat st;
  int fd = 0;

  MY_LOGD("open_device:%s", dev_name);

  if (-1 == stat(dev_name, &st)) {
    show_errno("stat device");
    exit(EXIT_FAILURE);
  }

  if (!S_ISCHR(st.st_mode)) {
    MY_LOGE("%s is no character device", dev_name);
    exit(EXIT_FAILURE);
  }

  fd = open(dev_name, O_RDWR | O_NONBLOCK, 0);
  if (-1 == fd) {
    show_errno("Cannot open device");
    exit(EXIT_FAILURE);
  }
  return fd;
}

static int queue_dma_buf(int fd,
                         unsigned int buf_type,
                         int index,
                         int size,
                         int req_fd,
                         int dma_fd) {
  struct v4l2_buffer buf;
  struct v4l2_plane planes[1];

  CLEAR(buf);
  CLEAR(planes);

  buf.index = index;
  buf.type = buf_type;
  buf.memory = V4L2_MEMORY_DMABUF;

  if (req_fd > 0) {
    buf.request_fd = req_fd;
    buf.flags |= V4L2_BUF_FLAG_REQUEST_FD;
  }

  if (V4L2_TYPE_IS_MULTIPLANAR(buf_type)) {
    buf.m.planes = planes;
    buf.m.planes[0].m.fd = dma_fd;
    buf.m.planes[0].bytesused = size;
    buf.m.planes[0].length = size;
    buf.length = 1;
  }

  if (xioctl(fd, VIDIOC_QBUF, &buf)) {
    MY_LOGE("Failed to queue dma buf req_fd:%d dma_fd:%d", req_fd, dma_fd);
    return -1;
  }
  MY_LOGD("VIDIOC_DMA_QBUF Done");

  return 0;
}

static int queue_buf(int fd, unsigned int buf_type, int index) {
  struct v4l2_buffer buf;

  CLEAR(buf);

  buf.type = buf_type;
  buf.memory = V4L2_MEMORY_MMAP;
  buf.index = index;

  if (xioctl(fd, VIDIOC_QBUF, &buf)) {
    MY_LOGE("Failed to queue dma buf buf_type:%d idx:%d", buf_type, index);
    return -1;
  }

  MY_LOGD("VIDIOC_QBUF: fd(%x), type(%d), idx(%d)", fd, buf_type, buf.index);
  return 0;
}

static void stream_off(int fd, unsigned int buf_type) {
  unsigned int type = buf_type;

  xioctl(fd, VIDIOC_STREAMOFF, &type);
  MY_LOGD("VIDIOC_STREAMOFF (fd:%x) success, buf type(%d)", fd, buf_type);
}

static void unmmap_buffer(struct buffer* buffers,
                          unsigned int n_buffers,
                          v4l2_memory mem_type) {
  if (mem_type == V4L2_MEMORY_DMABUF) {
    MY_LOGD("V4L2_MEMORY_DMABUF no need unmap");
    return;
  }
  if (buffers == NULL) {
    MY_LOGE("unmmap NULL Buffer");
    return;
  }

  for (int i = 0; i < n_buffers; ++i) {
    MY_LOGD("munmap %d va %p length %zu", i, buffers[i].start,
            buffers[i].length);
    if (-1 == munmap(buffers[i].start, buffers[i].length)) {
      MY_LOGE("munmap failed(%d), start(%p), length(%zu)", i, buffers[i].start,
              buffers[i].length);
      break;
    }
  }

  free(buffers);
}

static void close_device(int* fd) {
  if (-1 == close(*fd))
    show_errno("close");

  *fd = -1;
}

static int request_buffers(int fd,
                           unsigned int buf_type,
                           int buf_count,
                           v4l2_memory mem_type) {
  struct v4l2_requestbuffers reqbuf;

  CLEAR(reqbuf);

  reqbuf.count = buf_count;
  reqbuf.type = buf_type;
  reqbuf.memory = mem_type;

  if (xioctl(fd, VIDIOC_REQBUFS, &reqbuf)) {
    MY_LOGE("Buffer request cnt:%d type:%d mem:%d", buf_count, buf_type,
            mem_type);
    return 0;
  }
  MY_LOGD("%d buffers requested for fd(%x)", reqbuf.count, fd);

  return reqbuf.count;
}

static void stream_on(int fd, unsigned int buf_type) {
  unsigned int type = buf_type;

  xioctl(fd, VIDIOC_STREAMON, &type);
  MY_LOGD("VIDIOC_STREAMON(fd:%x) success, buf type(%d)", fd, buf_type);
}

static int query_map_buffer(int fd,
                            int req_count,
                            struct buffer** buffer_info,
                            int* mapped_count,
                            unsigned int buf_type,
                            v4l2_memory mem_type) {
  struct buffer* buffers =
      reinterpret_cast<buffer*>(malloc(req_count * sizeof(buffer)));
  int i = 0;
  for (i = 0; i < req_count; i++) {
    struct v4l2_buffer buf;
    /* Currently only support 1 plane */
    const int plane_idx = 0;
    struct v4l2_plane planes[1];
    int buf_len = buf.length;
    int offset = 0;
    CLEAR(buf);
    CLEAR(planes);
    buf.type = buf_type;
    buf.memory = mem_type;
    buf.index = i;
    // query image buffer
    if (V4L2_TYPE_IS_MULTIPLANAR(buf_type)) {
      buf.m.planes = planes;
      buf.length = 1;
    }
    MY_LOGD("Query buf: fd(%d), buf_type(%d), buf_len(%d)", fd, buf.type,
            buf.length);
    if (buf.m.planes != NULL) {
      MY_LOGD("plane buf_len(%d)", buf.m.planes[plane_idx].length);
    }
    if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
      MY_LOGE("querybuf output %d", i);
      continue;
    }
    if (V4L2_TYPE_IS_MULTIPLANAR(buf_type)) {
      buf_len = buf.m.planes[plane_idx].length;
      offset = buf.m.planes[plane_idx].m.mem_offset;
    } else {
      buf_len = buf.length;
      offset = buf.m.offset;
    }
    MY_LOGD("mmap info: fd(%d), buf_type(%d), offset(%d), size(%d)", fd,
            buf.type, offset, buf_len);
    if (mem_type != V4L2_MEMORY_DMABUF) {
      buffers[i].start =
          mmap(NULL, buf_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);
      MY_LOGD("Has mapped buffer %d: %p", i, buffers[i].start);
      if (buffers[i].start == MAP_FAILED) {
        MY_LOGE("Error!! Failed to map buffer %d", i);
      }
    }
    buffers[i].length = buf_len;
  }
  *buffer_info = buffers;
  *mapped_count = i;
  if (i != req_count) {
    MY_LOGE("-Error, buffer count: %d, mapped: %d", req_count, i);
    return -1;
  }
  return 0;
}

static inline void set_fd_face_pose(MUINT16* face_directions, uint8_t pose) {
  switch (pose) {
    case 0:
      face_directions[MTK_FACE_FRONT] = 0x3ff;
      return;
    case 1:
      face_directions[MTK_FACE_FRONT] = 0x5ff;
      return;
    case 2:
      face_directions[MTK_FACE_FRONT] = 0x9ff;
      return;
    case 3:
      face_directions[MTK_FACE_FRONT] = 0x11ff;
      face_directions[MTK_FACE_RIGHT_50] = 0x1;
      return;
    default:
      face_directions[MTK_FACE_FRONT] = 0x3ff;
      return;
  }
}

static void setup_meta_data(int fd, FdDrv_input_struct* FdDrv_input) {
  struct v4l2_ext_control ctrl[MAX_V4L2_CONTROL];
  struct v4l2_ext_controls ctrls;
  MUINT16 face_directions[MTK_FACE_ANGLE_NUM] = {0};
  int ret;

  CLEAR(ctrl);
  CLEAR(ctrls);

  ctrl[0].id = V4L2_CID_MTK_FD_SCALE_DOWN_IMG_WIDTH;
  ctrl[0].size = sizeof(FdDrv_input->source_img_width);
  ctrl[0].p_u16 = FdDrv_input->source_img_width;

  ctrl[1].id = V4L2_CID_MTK_FD_SCALE_DOWN_IMG_HEIGHT;
  ctrl[1].size = sizeof(FdDrv_input->source_img_height);
  ctrl[1].p_u16 = FdDrv_input->source_img_height;

  ctrl[2].id = V4L2_CID_MTK_FD_SCALE_IMG_NUM;
  ctrl[2].value = FdDrv_input->scale_num_from_user;

  set_fd_face_pose(face_directions, FdDrv_input->RIP_feature);
  ctrl[3].id = V4L2_CID_MTK_FD_DETECT_POSE;
  ctrl[3].size = sizeof(face_directions);
  ctrl[3].p_u16 = face_directions;

  ctrl[4].id = V4L2_CID_MTK_FD_DETECT_SPEED;
  ctrl[4].value = FdDrv_input->GFD_skip;

  ctrl[5].id = V4L2_CID_MTK_FD_DETECTION_MODEL;
  ctrl[5].value = FdDrv_input->dynamic_change_model[0];

  ctrls.ctrl_class = V4L2_CTRL_ID2CLASS(ctrl[0].id);
  ctrls.count = MAX_V4L2_CONTROL;
  ctrls.request_fd = g_fdvt_ctx.req_fd;
  ctrls.controls = &ctrl[0];
  ctrls.which = V4L2_CTRL_WHICH_REQUEST_VAL;

  ret = xioctl(fd, VIDIOC_S_EXT_CTRLS, &ctrls);
  if (ret < 0)
    MY_LOGE("Unable to set control");
}

static int poll_buf(int v4lfd) {
  int ret = 0;
  struct pollfd fds[] = {
      {.fd = v4lfd, .events = POLLIN | POLLOUT},
  };
  /* only listen 1 fd in this test */
  while ((ret = poll(fds, 1, 5000)) > 0) {
    if (fds[0].revents & POLLIN) {
      MY_LOGD("poll got POLLIN event from FD:0x%llx, revents:0x%llx",
              (uint64_t)v4lfd, (uint64_t)fds[0].revents);
      break;
    }
    if (fds[0].revents & POLLOUT) {
      MY_LOGD("poll got POLLOUT event from FD:0x%llx, revents:0x%llx",
              (uint64_t)v4lfd, (uint64_t)fds[0].revents);
      break;
    }
  }
  if (ret == 0) {
    MY_LOGE("poll timeout for POLLIN event from FD:%llx, revents%llx",
            (uint64_t)v4lfd, (uint64_t)fds[0].revents);
    perror("-poll");
  }
  return 0;
}

/* return the buf idx dequeued */
static int dequeue_buf(int fd, unsigned buf_type, v4l2_memory mem_type) {
  struct v4l2_buffer buf;
  struct v4l2_plane planes[1];
  int ret = 0;
  unsigned int bytes_used = 0;
  unsigned int buf_len = 0;

  CLEAR(buf);
  CLEAR(planes);

  poll_buf(fd);

  buf.type = buf_type;
  buf.memory = mem_type;
  buf.index = 0;

  if (V4L2_TYPE_IS_MULTIPLANAR(buf_type)) {
    buf.m.planes = planes;
    buf.length = 1;
  }

  ret = xioctl(fd, VIDIOC_DQBUF, &buf);
  if (ret) {
    MY_LOGE("Unable to dequeue buffer: type(%d), idx(%d)", buf_type, buf.index);
    return -1;
  }

  // for debugging
  if (buf_type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
    bytes_used = buf.m.planes[0].bytesused;
    buf_len = buf.m.planes[0].length;
  } else {
    bytes_used = buf.bytesused;
    buf_len = buf.length;
  }

  MY_LOGD(
      "VIDIOC_DQBUF Done: fd(%x), type(%d), idx(%d), buf.bytesused(%d),"
      " len(%u)",
      fd, buf_type, buf.index, bytes_used, buf_len);

  return 0;
}

static int open_fdvt_media_entities() {
  int media_fd;
  int index = 0;
  int ret = 0;
  char device_namee[MATCH_NAME_STR_SIZE_MAX];
  bool found_media_device = false;
  struct media_device_info info;
  struct media_entity_desc entity;
  for (index = 0; index < MAX_SCAN_DEV_COUNT; index++) {
    snprintf(device_namee, sizeof(device_namee), "/dev/media%d", index);
    media_fd = open(device_namee, O_RDWR);
    if (media_fd < 0) {
      MY_LOGE("Cannot open %s:%d", device_namee, errno);
      return -1;
    }
    MY_LOGD("check %s, fd=%d", device_namee, media_fd);
    ret = xioctl(media_fd, MEDIA_IOC_DEVICE_INFO, &info);
    if (ret < 0) {
      MY_LOGD("Failed to get device info for %s", device_namee);
    } else {
      MY_LOGD("Media Device Info: model:%s", info.model);
      if (!strcmp(MEDIA_FD_DEVICE_MODEL, info.model)) {
        g_fdvt_ctx.media_ctrl_fd = media_fd;
        found_media_device = true;
        break;
      }
    }
    close(media_fd);
  }
  if (!found_media_device) {
    MY_LOGE("Can't found media device");
    return -1;
  }
  index = 0;
  do {
    memset(&entity, 0, sizeof(struct media_entity_desc));
    entity.id = index | MEDIA_ENT_ID_FLAG_NEXT;
    ret = xioctl(media_fd, MEDIA_IOC_ENUM_ENTITIES, &entity);
    if (ret < 0) {
      if (errno == EINVAL)
        break;
    } else {
      /* match the node */
      if (!strcmp(entity.name, MEDIA_FD_ENITY_NAME)) {
        snprintf(device_namee, sizeof(device_namee), "/dev/char/%d:%d",
                 entity.dev.major, entity.dev.minor);
        MY_LOGD("Devce name is found:%s", device_namee);
        g_fdvt_ctx.node.fd = open_device(device_namee);
        return 0;
      } else {
        index = entity.id + 1;
        MY_LOGD("unknown entity: %s", entity.name);
      }
    }
  } while (ret == 0 && index < MAX_SCAN_DEV_COUNT);
  return -1;
}

static MUINT32 get_v4l2_image_format(MUINT8 fmt) {
  MUINT32 v4l2_img_fmt[8] = {
      MTK_V4L2_FMT_NOT_SUPPORT, MTK_V4L2_FMT_NOT_SUPPORT, V4L2_PIX_FMT_VYUY,
      V4L2_PIX_FMT_UYVY,        V4L2_PIX_FMT_YVYU,        V4L2_PIX_FMT_YUYV,
      MTK_V4L2_FMT_NOT_SUPPORT, MTK_V4L2_FMT_NOT_SUPPORT,
  };

  return v4l2_img_fmt[fmt % 8];
}
