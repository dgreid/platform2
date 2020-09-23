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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_DRV_SENSOR_IMG_SENSOR_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_DRV_SENSOR_IMG_SENSOR_H_

#define SENSOR_GAIN_MAP_SIZE 17

enum IMGSENSOR_MODE {
  IMGSENSOR_MODE_INIT,
  IMGSENSOR_MODE_PREVIEW,
  IMGSENSOR_MODE_CAPTURE,
  IMGSENSOR_MODE_VIDEO,
  IMGSENSOR_MODE_HIGH_SPEED_VIDEO,
  IMGSENSOR_MODE_SLIM_VIDEO,
  IMGSENSOR_MODE_CUSTOM1,
  IMGSENSOR_MODE_CUSTOM2,
  IMGSENSOR_MODE_CUSTOM3,
  IMGSENSOR_MODE_CUSTOM4,
  IMGSENSOR_MODE_CUSTOM5,
};

struct imgsensor_mode_struct {
  MUINT32 pclk;        /* record different mode's pclk */
  MUINT32 linelength;  /* record different mode's linelength */
  MUINT32 framelength; /* record different mode's framelength */

  MUINT8 startx; /* record different mode's startx of grabwindow */
  MUINT8 starty; /* record different mode's startx of grabwindow */

  MUINT16 grabwindow_width;  /* record different mode's width of grabwindow */
  MUINT16 grabwindow_height; /* record different mode's height of grabwindow */

  /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by different
   * scenario    */
  MUINT8 mipi_data_lp2hs_settle_dc;

  /*     following for GetDefaultFramerateByScenario()    */
  MUINT16 max_framerate;
};

/* SENSOR PRIVATE STRUCT FOR VARIABLES*/
struct imgsensor_struct {
  MUINT8 mirror; /* mirrorflip information */

  MUINT8 sensor_mode; /* record IMGSENSOR_MODE enum value */

  MUINT32 shutter; /* current shutter */
  MUINT16 gain;    /* current gain */

  MUINT32 pclk; /* current pclk */

  MUINT32 frame_length; /* current framelength */
  MUINT32 line_length;  /* current linelength */

  MUINT32 min_frame_length; /* current min  framelength to max framerate */
  MUINT16 dummy_pixel;      /* current dummypixel */
  MUINT16 dummy_line;       /* current dummline */

  MUINT16 current_fps; /* current max fps */
  bool autoflicker_en; /* record autoflicker enable or disable */
  bool test_pattern;   /* record test pattern mode or not */
  MSDK_SCENARIO_ID_ENUM current_scenario_id; /* current scenario id */
  MUINT8 hdr_mode;                           /* HDR mode */
  MUINT8 pdaf_mode;                          /* ihdr enable or disable */
  MUINT8 i2c_write_id; /* record current sensor's i2c write id */
};

/* SENSOR PRIVATE STRUCT FOR AGC PARAMETER */
struct imgsensor_agc_param_struct {
  MUINT32 auto_pregain;
  MUINT32 col_code;
};

/* SENSOR PRIVATE STRUCT FOR CONSTANT*/
struct imgsensor_info_struct {
  MUINT32 sensor_id;      /* record sensor id defined in Kd_imgsensor.h */
  MUINT32 checksum_value; /* checksum value for Camera Auto Test */

  struct imgsensor_agc_param_struct sensor_agc_param_map[SENSOR_GAIN_MAP_SIZE];

  struct imgsensor_mode_struct pre; /* preview scenario relative information */
  struct imgsensor_mode_struct cap; /* capture scenario relative information */
  struct imgsensor_mode_struct cap1;
  /* capture for PIP 24fps relative information,capture1 mode must use same
   *framelength, linelength with Capture mode for shutter calculate
   */
  struct imgsensor_mode_struct
      normal_video; /* normal video  scenario relative information */
  struct imgsensor_mode_struct
      hs_video; /* high speed video scenario relative information */
  struct imgsensor_mode_struct
      slim_video; /* slim video for VT scenario relative information */
  struct imgsensor_mode_struct
      custom1; /* custom1 scenario relative information */
  struct imgsensor_mode_struct
      custom2; /* custom2 scenario relative information */
  struct imgsensor_mode_struct
      custom3; /* custom3 scenario relative information */
  struct imgsensor_mode_struct
      custom4; /* custom4 scenario relative information */
  struct imgsensor_mode_struct custom5;

  MUINT8 ae_shut_delay_frame;        /* shutter delay frame for AE cycle */
  MUINT8 ae_sensor_gain_delay_frame; /* sensor gain delay frame for AE cycle */
  MUINT8 ae_ispGain_delay_frame;     /* isp gain delay frame for AE cycle */
  MUINT8 ihdr_support;               /* 1, support; 0,not support */
  MUINT8 ihdr_le_firstline;          /* 1,le first ; 0, se first */
  MUINT8 temperature_support;        /* 1, support; 0,not support */
  MUINT8 sensor_mode_num;            /* support sensor mode num */

  MUINT8 cap_delay_frame;        /* enter capture delay frame num */
  MUINT8 pre_delay_frame;        /* enter preview delay frame num */
  MUINT8 video_delay_frame;      /* enter video delay frame num */
  MUINT8 hs_video_delay_frame;   /* enter high speed video  delay frame num */
  MUINT8 slim_video_delay_frame; /* enter slim video delay frame num */

  MUINT8 margin;       /* sensor framelength & shutter margin */
  MUINT32 min_shutter; /* min shutter */
  MUINT32
  max_frame_length; /* max framelength by sensor register's limitation */

  MUINT8 isp_driving_current;   /* mclk driving current */
  MUINT8 sensor_interface_type; /* sensor_interface_type */
  MUINT8 mipi_sensor_type;
  /* 0,MIPI_OPHY_NCSI2; 1,MIPI_OPHY_CSI2, default is NCSI2, don't modify this
   * para */
  MUINT8 mipi_settle_delay_mode;
  /* 0, high speed signal auto detect; 1, use settle delay,unit is ns,
   *default is auto detect, don't modify this para
   */
  MUINT8 sensor_output_dataformat; /* sensor output first pixel color */
  MUINT8 mclk;          /* mclk value, suggest 24 or 26 for 24Mhz or 26Mhz */
  MUINT32 i2c_speed;    /* i2c speed */
  MUINT8 mipi_lane_num; /* mipi lane num */
  /*=======================================================================*/
  /* New  start*/
  MUINT8 SensorClockPolarity;
  MUINT8 SensorClockFallingPolarity;
  MUINT8 SensorHsyncPolarity;
  MUINT8 SensorVsyncPolarity;
  MUINT8 SensorInterruptDelayLines;
  MUINT8 SensorResetActiveHigh;
  MUINT8 SensorResetDelayCount;
  MUINT8 SensorMasterClockSwitch;
  MUINT8 PDAF_Support;
  MUINT8 HDR_Support;
  MUINT8 ZHDR_Mode;
  MUINT8 SensorClockDividCount;
  MUINT8 SensorClockRisingCount;
  MUINT8 SensorClockFallingCount;
  MUINT8 SensorPixelClockCount;
  MUINT8 SensorDataLatchCount;
  MUINT8 MIPIDataLowPwr2HighSpeedTermDelayCount;
  MUINT8 MIPICLKLowPwr2HighSpeedTermDelayCount;
  MUINT8 SensorWidthSampling;
  MUINT8 SensorHightSampling;
  MUINT8 SensorPacketECCOrder;
  MUINT8 SensorGainfactor;
  MUINT8 SensorHFlip;
  MUINT8 SensorVFlip;
  /* New end*/
};

extern SENSOR_WINSIZE_INFO_STRUCT* getImgWinSizeInfo(MUINT8 idx,
                                                     MUINT32 scenario);
extern struct imgsensor_info_struct* getImgsensorInfo(MUINT8 infoIdx);
extern MUINT32 getNumOfSupportSensor(void);
extern const char* getSensorListName(MUINT8 listIdx);
extern MUINT32 getSensorListId(MUINT8 listIdx);
extern IMGSENSOR_SENSOR_LIST* getSensorList(MUINT8 listIdx);
extern MUINT32 getImgsensorType(MUINT8 infoIdx);
#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_DRV_SENSOR_IMG_SENSOR_H_
