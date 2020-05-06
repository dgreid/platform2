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

#ifndef CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_IMGSENSOR_SRC_IMGSENSOR_CUSTOM_INFO_H_
#define CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_IMGSENSOR_SRC_IMGSENSOR_CUSTOM_INFO_H_

#include <kd_imgsensor_define.h>
#include <linux/v4l2-subdev.h>
#include "mtkcam/drv/sensor/img_sensor.h"

#define SCENARIO_ID_MAX 5
struct IMGSENSOR_SENSOR_LIST gimgsensor_sensor_list[MAX_NUM_OF_SUPPORT_SENSOR] =
    {
        {OV5695_SENSOR_ID, SENSOR_DRVNAME_OV5695_MIPI_RAW, NULL},
        {OV2685_SENSOR_ID, SENSOR_DRVNAME_OV2685_MIPI_RAW, NULL},
        {OV8856_SENSOR_ID, SENSOR_DRVNAME_OV8856_MIPI_RAW, NULL},
        {OV02A10_SENSOR_ID, SENSOR_DRVNAME_OV02A10_MIPI_RAW, NULL},
        {GC5035_SENSOR_ID, SENSOR_DRVNAME_GC5035_MIPI_RAW, NULL},
        /*  ADD sensor driver before this line */
        {0, {0}, NULL}, /* end of list */
};

// sensor order of win size info  must match with gImgsensor_info
static SENSOR_WINSIZE_INFO_STRUCT gImgsensor_winsize_info[][SCENARIO_ID_MAX] = {
    {
        // ov5695
        {2592, 1944, 0, 0, 2592, 1944, 2592, 1944, 0, 0, 2592, 1944, 0, 0, 2592,
         1944}, /*preview */
        {2592, 1944, 0, 0, 2592, 1944, 2592, 1944, 0, 0, 2592, 1944, 0, 0, 2592,
         1944}, /*capture */
        {2592, 1944, 0, 0, 2592, 1944, 1280, 720, 0, 0, 1280, 720, 0, 0, 1280,
         720}, /* video */
        {2592, 1944, 0, 0, 2592, 1944, 640, 480, 0, 0, 640, 480, 0, 0, 640,
         480}, /* hs video */
        {2592, 1944, 0, 0, 2592, 1944, 1296, 972, 0, 0, 1296, 972, 0, 0, 1296,
         972} /*slim video */
    },
    {
        // ov2685
        {1600, 1200, 0, 0, 1600, 1200, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600,
         1200}, /*preview */
        {1600, 1200, 0, 0, 1600, 1200, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600,
         1200}, /*capture */
        {1600, 1200, 0, 0, 1600, 1200, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600,
         1200}, /* video */
        {1600, 1200, 0, 0, 1600, 1200, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600,
         1200}, /* hs video */
        {1600, 1200, 0, 0, 1600, 1200, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600,
         1200} /* slim video */
    },
    {// ov8856
     {3296, 2480, 0, 0, 3296, 2480, 3264, 2448, 0, 0, 3264, 2448, 0, 0, 3264,
      2448},
     {3296, 2480, 0, 0, 3296, 2480, 3264, 2448, 0, 0, 3264, 2448, 0, 0, 3264,
      2448},
     {3296, 2480, 0, 0, 3296, 2480, 1632, 1224, 0, 0, 1632, 1224, 0, 0, 1632,
      1224},
     {3296, 2480, 0, 0, 3296, 2480, 640, 480, 0, 0, 640, 480, 0, 0, 640, 480},
     {3296, 2480, 0, 0, 3296, 2480, 1632, 1224, 0, 0, 1632, 1224, 0, 0, 1632,
      1224}},
    {
        // ov02a10
        {1600, 1200, 0, 0, 1600, 1200, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600,
         1200}, /*preview */
        {1600, 1200, 0, 0, 1600, 1200, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600,
         1200}, /*capture */
        {1600, 1200, 0, 0, 1600, 1200, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600,
         1200}, /* video */
        {1600, 1200, 0, 0, 1600, 1200, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600,
         1200}, /* hs video */
        {1600, 1200, 0, 0, 1600, 1200, 1600, 1200, 0, 0, 1600, 1200, 0, 0, 1600,
         1200} /* slim video */
    },
    {
        // gc5035
        {2592, 1944, 0, 0, 2592, 1944, 2592, 1944, 0, 0, 2592, 1944, 0, 0, 2592,
         1944}, /* preview */
        {2592, 1944, 0, 0, 2592, 1944, 2592, 1944, 0, 0, 2592, 1944, 0, 0, 2592,
         1944}, /* capture */
        {2592, 1944, 0, 0, 2592, 1944, 1296, 972, 0, 0, 1296, 972, 0, 0, 1296,
         972}, /* video */
        {2592, 1944, 656, 492, 1280, 960, 640, 480, 0, 0, 640, 480, 0, 0, 640,
         480}, /* hs video */
        {2592, 1944, 16, 252, 2560, 1440, 1280, 720, 0, 0, 1280, 720, 0, 0,
         1280, 720} /* slim video */
    },
};

static struct imgsensor_info_struct gImgsensor_info[] = {
    {
        .sensor_id =
            OV5695_SENSOR_ID, /* record sensor id defined in Kd_imgsensor.h */

        .checksum_value = 0x6c259b92, /* checksum value for Camera Auto Test */

        .sensor_agc_param_map =
            {
                {0, 0},
            },

        .pre =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 45000000,    /* record different mode's pclk */
                .linelength = 740,   /* record different mode's linelength */
                .framelength = 2024, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2592, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    1944, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .cap =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 45000000,
                .linelength = 740,
                .framelength = 2024,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 2592,
                .grabwindow_height = 1944,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .cap1 =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 45000000,
                .linelength = 740,
                .framelength = 2024,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 2592,
                .grabwindow_height = 1944,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },

        .normal_video =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 45000000,
                .linelength = 672,
                .framelength = 2232,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1280,
                .grabwindow_height = 720,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .hs_video =
            {
                /*data rate 600 Mbps/lane */
                .pclk = 45000000,
                .linelength = 672,
                .framelength = 558,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 640,
                .grabwindow_height = 480,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 1200,
            },
        .slim_video =
            {
                /*data rate 792 Mbps/lane */
                .pclk = 45000000,
                .linelength = 740,
                .framelength = 1012,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1296,
                .grabwindow_height = 972,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 600,
            },
        .custom1 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*         following for
                   MIPIDataLowPwr2HighSpeedSettleDelayCount by different
                   scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*         following for GetDefaultFramerateByScenario() */
                .max_framerate = 300,
            },

        .custom2 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom3 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom4 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom5 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .ae_shut_delay_frame = 0,
        /* shutter delay frame for AE cycle, 2 frame with
           ispGain_delay-shut_delay=2-0=2 */
        .ae_sensor_gain_delay_frame = 0,
        /* sensor gain delay frame for AE cycle,2 frame with
           ispGain_delay-sensor_gain_delay=2-0=2 */
        .ae_ispGain_delay_frame = 2, /* isp gain delay frame for AE cycle */
        .ihdr_support = 0,           /* 1, support; 0,not support */
        .ihdr_le_firstline = 0,      /* 1,le first ; 0, se first */
        .temperature_support = 1,    /* 1, support; 0,not support */
        .sensor_mode_num = 10,       /* support sensor mode num */

        .cap_delay_frame = 1,      /* enter capture delay frame num */
        .pre_delay_frame = 2,      /* enter preview delay frame num */
        .video_delay_frame = 1,    /* enter video delay frame num */
        .hs_video_delay_frame = 3, /* enter high speed video  delay frame num */
        .slim_video_delay_frame = 3, /* enter slim video delay frame num */

        .margin = 10,     /* sensor framelength & shutter margin */
        .min_shutter = 1, /* min shutter */
        .max_frame_length =
            0xffff, /* max framelength by sensor register's limitation */

        .isp_driving_current = ISP_DRIVING_4MA, /* mclk driving current */
        .sensor_interface_type =
            SENSOR_INTERFACE_TYPE_MIPI, /* sensor_interface_type */
        .mipi_sensor_type =
            MIPI_OPHY_NCSI2, /* 0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2 */
        .mipi_settle_delay_mode = MIPI_SETTLEDELAY_AUTO,
        /* 0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANNUAL */
        .sensor_output_dataformat =
            SENSOR_OUTPUT_FORMAT_RAW_B, /* sensor output first pixel color */
        .mclk = 24, /* mclk value, suggest 24 or 26 for 24Mhz or 26Mhz */
        /* record sensor support all write id addr, only supprt 4must end with
           0xff */
        .i2c_speed = 400,                    /* i2c read/write speed */
        .mipi_lane_num = SENSOR_MIPI_4_LANE, /* mipi lane num */

        .SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorInterruptDelayLines = 4,
        .SensorResetActiveHigh = false,
        .SensorResetDelayCount = 5,
        .SensorMasterClockSwitch = 0,
        .PDAF_Support = PDAF_SUPPORT_CAMSV,
/* 0: NO PDAF, 1: PDAF Raw Data mode, 2:PDAF VC mode */
#if defined(OV5695_ZHDR)
        .HDR_Support = 3, /*0: NO HDR, 1: iHDR, 2:mvHDR, 3:zHDR */
        /*0: no support, 1: G0,R0.B0, 2: G0,R0.B1, 3: G0,R1.B0, 4: G0,R1.B1 */
        /*5: G1,R0.B0, 6: G1,R0.B1, 7: G1,R1.B0, 8: G1,R1.B1 */
        .ZHDR_Mode = 8,
#else
        .HDR_Support = 2,
#endif
        .SensorClockDividCount = 3,
        .SensorClockRisingCount = 0,
        .SensorClockFallingCount = 2,
        .SensorPixelClockCount = 3,
        .SensorDataLatchCount = 2,
        .MIPIDataLowPwr2HighSpeedTermDelayCount = 0,
        .MIPICLKLowPwr2HighSpeedTermDelayCount = 0,
        .SensorWidthSampling = 0,
        .SensorHightSampling = 0,
        .SensorPacketECCOrder = 1,
        .SensorGainfactor = 6,
        .SensorHFlip = 0,
        .SensorVFlip = 0,
    },
    {
        .sensor_id =
            OV2685_SENSOR_ID, /* record sensor id defined in Kd_imgsensor.h */

        .checksum_value = 0x6c259b92, /* checksum value for Camera Auto Test */

        .sensor_agc_param_map =
            {
                {0, 0},
            },

        .pre =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 66000000,    /* record different mode's pclk */
                .linelength = 1700,  /* record different mode's linelength */
                .framelength = 1294, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    1600, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    1200, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .cap =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 66000000,
                .linelength = 1700,
                .framelength = 1294,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1600,
                .grabwindow_height = 1200,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .cap1 =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 66000000,
                .linelength = 1700,
                .framelength = 1294,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1600,
                .grabwindow_height = 1200,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },

        .normal_video =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 66000000,
                .linelength = 1700,
                .framelength = 1294,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1600,
                .grabwindow_height = 1200,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .hs_video =
            {
                /*data rate 600 Mbps/lane */
                .pclk = 66000000,
                .linelength = 1700,
                .framelength = 1294,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1600,
                .grabwindow_height = 1200,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .slim_video =
            {
                /*data rate 792 Mbps/lane */
                .pclk = 66000000,
                .linelength = 1700,
                .framelength = 1294,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1600,
                .grabwindow_height = 1200,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .custom1 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*         following for
                   MIPIDataLowPwr2HighSpeedSettleDelayCount by different
                   scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*         following for GetDefaultFramerateByScenario() */
                .max_framerate = 300,
            },

        .custom2 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom3 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom4 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom5 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .ae_shut_delay_frame = 0,
        /* shutter delay frame for AE cycle, 2 frame with
           ispGain_delay-shut_delay=2-0=2 */
        .ae_sensor_gain_delay_frame = 0,
        /* sensor gain delay frame for AE cycle,2 frame with
           ispGain_delay-sensor_gain_delay=2-0=2 */
        .ae_ispGain_delay_frame = 2, /* isp gain delay frame for AE cycle */
        .ihdr_support = 0,           /* 1, support; 0,not support */
        .ihdr_le_firstline = 0,      /* 1,le first ; 0, se first */
        .temperature_support = 1,    /* 1, support; 0,not support */
        .sensor_mode_num = 10,       /* support sensor mode num */

        .cap_delay_frame = 1,      /* enter capture delay frame num */
        .pre_delay_frame = 2,      /* enter preview delay frame num */
        .video_delay_frame = 1,    /* enter video delay frame num */
        .hs_video_delay_frame = 3, /* enter high speed video  delay frame num */
        .slim_video_delay_frame = 3, /* enter slim video delay frame num */

        .margin = 10,     /* sensor framelength & shutter margin */
        .min_shutter = 1, /* min shutter */
        .max_frame_length =
            0xffff, /* max framelength by sensor register's limitation */

        .isp_driving_current = ISP_DRIVING_4MA, /* mclk driving current */
        .sensor_interface_type =
            SENSOR_INTERFACE_TYPE_MIPI, /* sensor_interface_type */
        .mipi_sensor_type =
            MIPI_OPHY_NCSI2, /* 0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2 */
        .mipi_settle_delay_mode = MIPI_SETTLEDELAY_AUTO,
        /* 0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANNUAL */
        .sensor_output_dataformat =
            SENSOR_OUTPUT_FORMAT_RAW_B, /* sensor output first pixel color */
        .mclk = 24, /* mclk value, suggest 24 or 26 for 24Mhz or 26Mhz */
        /* record sensor support all write id addr, only supprt 4must end with
           0xff */
        .i2c_speed = 400,                    /* i2c read/write speed */
        .mipi_lane_num = SENSOR_MIPI_4_LANE, /* mipi lane num */

        .SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorInterruptDelayLines = 4,
        .SensorResetActiveHigh = false,
        .SensorResetDelayCount = 5,
        .SensorMasterClockSwitch = 0,
        .PDAF_Support = PDAF_SUPPORT_CAMSV,
/* 0: NO PDAF, 1: PDAF Raw Data mode, 2:PDAF VC mode */
#if defined(OV2685_ZHDR)
        .HDR_Support = 3, /*0: NO HDR, 1: iHDR, 2:mvHDR, 3:zHDR */
        /*0: no support, 1: G0,R0.B0, 2: G0,R0.B1, 3: G0,R1.B0, 4: G0,R1.B1 */
        /*5: G1,R0.B0, 6: G1,R0.B1, 7: G1,R1.B0, 8: G1,R1.B1 */
        .ZHDR_Mode = 8,
#else
        .HDR_Support = 2,
#endif
        .SensorClockDividCount = 3,
        .SensorClockRisingCount = 0,
        .SensorClockFallingCount = 2,
        .SensorPixelClockCount = 3,
        .SensorDataLatchCount = 2,
        .MIPIDataLowPwr2HighSpeedTermDelayCount = 0,
        .MIPICLKLowPwr2HighSpeedTermDelayCount = 0,
        .SensorWidthSampling = 0,
        .SensorHightSampling = 0,
        .SensorPacketECCOrder = 1,
        .SensorGainfactor = 3,
        .SensorHFlip = 0,
        .SensorVFlip = 0,
    },
    {
        .sensor_id =
            OV8856_SENSOR_ID, /* record sensor id defined in Kd_imgsensor.h */

        .checksum_value = 0xb1893b4f, /* checksum value for Camera Auto Test */

        .sensor_agc_param_map =
            {
                {0, 0},
            },

        .pre =
            {
                .pclk = 144000000,   /*record different mode's pclk*/
                .linelength = 1932,  /*record different mode's linelength*/
                .framelength = 2482, /*record different mode's framelength*/
                .startx = 0, /*record different mode's startx of grabwindow*/
                .starty = 0, /*record different mode's starty of grabwindow*/
                .grabwindow_width = 3264,
                .grabwindow_height = 2448,
                .mipi_data_lp2hs_settle_dc = 85,
                .max_framerate = 300,
            },
        .cap =
            {
                .pclk = 144000000,
                .linelength = 1932,
                .framelength = 2482,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 3264,
                .grabwindow_height = 2448,
                .mipi_data_lp2hs_settle_dc = 85,
                .max_framerate = 300,
            },
        .cap1 =
            {
                /*capture for 15fps*/
                .pclk = 144000000,
                .linelength = 1932,
                .framelength = 4964,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 3264,
                .grabwindow_height = 2448,
                .mipi_data_lp2hs_settle_dc = 85,
                .max_framerate = 150,
            },
        .normal_video =
            {
                /* cap*/
                .pclk = 144000000,
                .linelength = 1932,
                .framelength = 2482,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1632,
                .grabwindow_height = 1224,
                .mipi_data_lp2hs_settle_dc = 85,
                .max_framerate = 300,
            },
        .hs_video =
            {
                .pclk = 144000000,  /*record different mode's pclk*/
                .linelength = 1932, /*record different mode's linelength*/
                .framelength = 620, /*record different mode's framelength*/
                .startx = 0, /*record different mode's startx of grabwindow*/
                .starty = 0, /*record different mode's starty of grabwindow*/
                .grabwindow_width = 640,
                .grabwindow_height = 480,
                .mipi_data_lp2hs_settle_dc = 85,
                .max_framerate = 1200,
            },
        .slim_video =
            {
                /*pre*/
                .pclk = 144000000,   /*record different mode's pclk*/
                .linelength = 1932,  /*record different mode's linelength*/
                .framelength = 2482, /*record different mode's framelength*/
                .startx = 0, /*record different mode's startx of grabwindow*/
                .starty = 0, /*record different mode's starty of grabwindow*/
                .grabwindow_width = 1632,
                .grabwindow_height = 1224,
                .mipi_data_lp2hs_settle_dc = 85,
                .max_framerate = 300,
            },
        .custom1 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .custom2 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .custom3 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom4 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom5 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .ae_shut_delay_frame = 0,
        .ae_sensor_gain_delay_frame = 0,
        .ae_ispGain_delay_frame = 2, /*isp gain delay frame for AE cycle*/
        .ihdr_support = 0,           /*1, support; 0,not support*/
        .ihdr_le_firstline = 0,      /*1,le first ; 0, se first*/

        /*support sensor mode num ,don't support Slow motion*/
        .sensor_mode_num = 5,
        .cap_delay_frame = 3,        /*enter capture delay frame num*/
        .pre_delay_frame = 3,        /*enter preview delay frame num*/
        .video_delay_frame = 3,      /*enter video delay frame num*/
        .hs_video_delay_frame = 3,   /*enter high speed video  delay frame num*/
        .slim_video_delay_frame = 3, /*enter slim video delay frame num*/

        .margin = 6,      /*sensor framelength & shutter margin*/
        .min_shutter = 6, /*min shutter*/

        /*max framelength by sensor register's limitation*/
        .max_frame_length = 0x90f7,

        .isp_driving_current = ISP_DRIVING_6MA, /*mclk driving current*/

        /*Sensor_interface_type*/
        .sensor_interface_type = SENSOR_INTERFACE_TYPE_MIPI,

        /*0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2*/
        .mipi_sensor_type = MIPI_OPHY_NCSI2,

        /*0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANNUAL*/
        .mipi_settle_delay_mode = MIPI_SETTLEDELAY_MANUAL,

        /*sensor output first pixel color*/
        .sensor_output_dataformat = SENSOR_OUTPUT_FORMAT_RAW_B,

        .mclk = 24,       /*mclk value, suggest 24 or 26 for 24Mhz or 26Mhz*/
        .i2c_speed = 400, /* i2c read/write speed */
        .mipi_lane_num = SENSOR_MIPI_4_LANE, /*mipi lane num*/

        .SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorInterruptDelayLines = 4,
        .SensorResetActiveHigh = false,
        .SensorResetDelayCount = 5,
        .SensorMasterClockSwitch = 0,
        .PDAF_Support = PDAF_SUPPORT_CAMSV,
/* 0: NO PDAF, 1: PDAF Raw Data mode, 2:PDAF VC mode */
#if defined(OV8856_ZHDR)
        .HDR_Support = 3, /*0: NO HDR, 1: iHDR, 2:mvHDR, 3:zHDR */
        /*0: no support, 1: G0,R0.B0, 2: G0,R0.B1, 3: G0,R1.B0, 4: G0,R1.B1 */
        /*5: G1,R0.B0, 6: G1,R0.B1, 7: G1,R1.B0, 8: G1,R1.B1 */
        .ZHDR_Mode = 8,
#else
        .HDR_Support = 2,
#endif
        .SensorClockDividCount = 3,
        .SensorClockRisingCount = 0,
        .SensorClockFallingCount = 2,
        .SensorPixelClockCount = 3,
        .SensorDataLatchCount = 2,
        .MIPIDataLowPwr2HighSpeedTermDelayCount = 0,
        .MIPICLKLowPwr2HighSpeedTermDelayCount = 0,
        .SensorWidthSampling = 0,
        .SensorHightSampling = 0,
        .SensorPacketECCOrder = 1,
        .SensorGainfactor = 3,
        .SensorHFlip = 0,
        .SensorVFlip = 0,
    },
    {
        .sensor_id =
            OV02A10_SENSOR_ID, /* record sensor id defined in Kd_imgsensor.h */

        .checksum_value = 0xb1893b4f, /* checksum value for Camera Auto Test */

        .sensor_agc_param_map =
            {
                {0, 0},
            },

        .pre =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 39000000,    /* record different mode's pclk */
                .linelength = 934,   /* record different mode's linelength */
                .framelength = 1390, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    1600, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    1200, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .cap =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 39000000,
                .linelength = 934,
                .framelength = 1390,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1600,
                .grabwindow_height = 1200,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .cap1 =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 39000000,
                .linelength = 934,
                .framelength = 1390,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1600,
                .grabwindow_height = 1200,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .normal_video =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 39000000,
                .linelength = 934,
                .framelength = 1390,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1600,
                .grabwindow_height = 1200,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .hs_video =
            {
                /*data rate 600 Mbps/lane */
                .pclk = 39000000,
                .linelength = 934,
                .framelength = 1390,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1600,
                .grabwindow_height = 1200,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .slim_video =
            {
                /*data rate 792 Mbps/lane */
                .pclk = 39000000,
                .linelength = 934,
                .framelength = 1390,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1600,
                .grabwindow_height = 1200,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .custom1 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*         following for
                   MIPIDataLowPwr2HighSpeedSettleDelayCount by different
                   scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*         following for GetDefaultFramerateByScenario() */
                .max_framerate = 300,
            },
        .custom2 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom3 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom4 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom5 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .ae_shut_delay_frame = 0,
        /* shutter delay frame for AE cycle, 2 frame with
           ispGain_delay-shut_delay=2-0=2 */
        .ae_sensor_gain_delay_frame = 0,
        /* sensor gain delay frame for AE cycle,2 frame with
           ispGain_delay-sensor_gain_delay=2-0=2 */
        .ae_ispGain_delay_frame = 2, /* isp gain delay frame for AE cycle */
        .ihdr_support = 0,           /* 1, support; 0,not support */
        .ihdr_le_firstline = 0,      /* 1,le first ; 0, se first */
        .temperature_support = 1,    /* 1, support; 0,not support */
        .sensor_mode_num = 10,       /* support sensor mode num */

        .cap_delay_frame = 1,      /* enter capture delay frame num */
        .pre_delay_frame = 2,      /* enter preview delay frame num */
        .video_delay_frame = 1,    /* enter video delay frame num */
        .hs_video_delay_frame = 3, /* enter high speed video  delay frame num */
        .slim_video_delay_frame = 3, /* enter slim video delay frame num */

        .margin = 10,     /* sensor framelength & shutter margin */
        .min_shutter = 1, /* min shutter */
        .max_frame_length =
            0xffff, /* max framelength by sensor register's limitation */

        .isp_driving_current = ISP_DRIVING_4MA, /* mclk driving current */
        .sensor_interface_type =
            SENSOR_INTERFACE_TYPE_MIPI, /* sensor_interface_type */
        .mipi_sensor_type =
            MIPI_OPHY_NCSI2, /* 0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2 */
        .mipi_settle_delay_mode = MIPI_SETTLEDELAY_AUTO,
        /* 0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANNUAL */
        .sensor_output_dataformat =
            SENSOR_OUTPUT_FORMAT_RAW_R, /* sensor output first pixel color */
        .mclk = 24, /* mclk value, suggest 24 or 26 for 24Mhz or 26Mhz */
        /* record sensor support all write id addr, only supprt 4must end with
           0xff */
        .i2c_speed = 400,                    /* i2c read/write speed */
        .mipi_lane_num = SENSOR_MIPI_4_LANE, /* mipi lane num */

        .SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorInterruptDelayLines = 4,
        .SensorResetActiveHigh = false,
        .SensorResetDelayCount = 5,
        .SensorMasterClockSwitch = 0,
        .PDAF_Support = PDAF_SUPPORT_CAMSV,
/* 0: NO PDAF, 1: PDAF Raw Data mode, 2:PDAF VC mode */
#if defined(OV02A10_ZHDR)
        .HDR_Support = 3, /*0: NO HDR, 1: iHDR, 2:mvHDR, 3:zHDR */
        /*0: no support, 1: G0,R0.B0, 2: G0,R0.B1, 3: G0,R1.B0, 4: G0,R1.B1 */
        /*5: G1,R0.B0, 6: G1,R0.B1, 7: G1,R1.B0, 8: G1,R1.B1 */
        .ZHDR_Mode = 8,
#else
        .HDR_Support = 2,
#endif
        .SensorClockDividCount = 3,
        .SensorClockRisingCount = 0,
        .SensorClockFallingCount = 2,
        .SensorPixelClockCount = 3,
        .SensorDataLatchCount = 2,
        .MIPIDataLowPwr2HighSpeedTermDelayCount = 0,
        .MIPICLKLowPwr2HighSpeedTermDelayCount = 0,
        .SensorWidthSampling = 0,
        .SensorHightSampling = 0,
        .SensorPacketECCOrder = 1,
        .SensorGainfactor = 6,
        .SensorHFlip = 0,
        .SensorVFlip = 0,
    },
    {
        .sensor_id =
            GC5035_SENSOR_ID, /* record sensor id defined in Kd_imgsensor.h */

        .checksum_value = 0xcde448ca, /* checksum value for Camera Auto Test */

        .sensor_agc_param_map =
            {
                /* GC5035 */
                {256, 0},
                {302, 1},
                {358, 2},
                {425, 3},
                {502, 8},
                {599, 9},
                {717, 10},
                {845, 11},
                {998, 12},
                {1203, 13},
                {1434, 14},
                {1710, 15},
                {1997, 16},
                {2355, 17},
                {2816, 18},
                {3318, 19},
                {3994, 20},
            },

        .pre =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 175200000,   /* record different mode's pclk */
                .linelength = 2920,  /* record different mode's linelength */
                .framelength = 2008, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2592, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    1944, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .cap =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 175200000,
                .linelength = 2920,
                .framelength = 2008,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 2592,
                .grabwindow_height = 1944,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .cap1 =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 141600000,
                .linelength = 2920,
                .framelength = 2008,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 2592,
                .grabwindow_height = 1944,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 240,
            },

        .normal_video =
            {
                /*data rate 1499.20 Mbps/lane */
                .pclk = 87600000,
                .linelength = 1460,
                .framelength = 2008,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1296,
                .grabwindow_height = 972,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .hs_video =
            {
                /*data rate 600 Mbps/lane */
                .pclk = 175200000,
                .linelength = 1896,
                .framelength = 1536,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1280,
                .grabwindow_height = 720,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 600,
            },
        .slim_video =
            {
                /*data rate 792 Mbps/lane */
                .pclk = 87600000,
                .linelength = 1460,
                .framelength = 2008,
                .startx = 0,
                .starty = 0,
                .grabwindow_width = 1280,
                .grabwindow_height = 720,
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                .max_framerate = 300,
            },
        .custom1 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*         following for
                   MIPIDataLowPwr2HighSpeedSettleDelayCount by different
                   scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*         following for GetDefaultFramerateByScenario() */
                .max_framerate = 300,
            },

        .custom2 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom3 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom4 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .custom5 =
            {
                /*data rate 1099.20 Mbps/lane */
                .pclk = 531000000,   /* record different mode's pclk */
                .linelength = 6024,  /* record different mode's linelength */
                .framelength = 2896, /* record different mode's framelength */
                .startx = 0, /* record different mode's startx of grabwindow */
                .starty = 0, /* record different mode's starty of grabwindow */
                .grabwindow_width =
                    2672, /* record different mode's width of grabwindow */
                .grabwindow_height =
                    2008, /* record different mode's height of grabwindow */
                /*     following for MIPIDataLowPwr2HighSpeedSettleDelayCount by
                   different scenario    */
                .mipi_data_lp2hs_settle_dc = 85, /* unit , ns */
                /*     following for GetDefaultFramerateByScenario()    */
                .max_framerate = 300,
            },
        .ae_shut_delay_frame = 0,
        /* shutter delay frame for AE cycle, 2 frame with
           ispGain_delay-shut_delay=2-0=2 */
        .ae_sensor_gain_delay_frame = 0,
        /* sensor gain delay frame for AE cycle,2 frame with
           ispGain_delay-sensor_gain_delay=2-0=2 */
        .ae_ispGain_delay_frame = 2, /* isp gain delay frame for AE cycle */
        .ihdr_support = 0,           /* 1, support; 0,not support */
        .ihdr_le_firstline = 0,      /* 1,le first ; 0, se first */
        .temperature_support = 1,    /* 1, support; 0,not support */
        .sensor_mode_num = 5,        /* support sensor mode num */

        .cap_delay_frame = 2,      /* enter capture delay frame num */
        .pre_delay_frame = 2,      /* enter preview delay frame num */
        .video_delay_frame = 2,    /* enter video delay frame num */
        .hs_video_delay_frame = 2, /* enter high speed video  delay frame num */
        .slim_video_delay_frame = 2, /* enter slim video delay frame num */

        .margin = 16,     /* sensor framelength & shutter margin */
        .min_shutter = 4, /* min shutter */
        .max_frame_length =
            0x3fff, /* max framelength by sensor register's limitation */

        .isp_driving_current = ISP_DRIVING_6MA, /* mclk driving current */
        .sensor_interface_type =
            SENSOR_INTERFACE_TYPE_MIPI, /* sensor_interface_type */
        .mipi_sensor_type =
            MIPI_OPHY_NCSI2, /* 0,MIPI_OPHY_NCSI2;  1,MIPI_OPHY_CSI2 */
        .mipi_settle_delay_mode = MIPI_SETTLEDELAY_AUTO,
        /* 0,MIPI_SETTLEDELAY_AUTO; 1,MIPI_SETTLEDELAY_MANNUAL */
        .sensor_output_dataformat =
            SENSOR_OUTPUT_FORMAT_RAW_R, /* sensor output first pixel color */
        .mclk = 24, /* mclk value, suggest 24 or 26 for 24Mhz or 26Mhz */
        /* record sensor support all write id addr, only support 4must end with
           0xff */
        .i2c_speed = 400,                    /* i2c read/write speed */
        .mipi_lane_num = SENSOR_MIPI_4_LANE, /* mipi lane num */

        .SensorClockPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorClockFallingPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorHsyncPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorVsyncPolarity = SENSOR_CLOCK_POLARITY_LOW,
        .SensorInterruptDelayLines = 4,
        .SensorResetActiveHigh = false,
        .SensorResetDelayCount = 5,
        .SensorMasterClockSwitch = 0,
        .PDAF_Support = PDAF_SUPPORT_CAMSV,
/* 0: NO PDAF, 1: PDAF Raw Data mode, 2:PDAF VC mode */
#if defined(GC5035_ZHDR)
        .HDR_Support = 3, /*0: NO HDR, 1: iHDR, 2:mvHDR, 3:zHDR */
        /*0: no support, 1: G0,R0.B0, 2: G0,R0.B1, 3: G0,R1.B0, 4: G0,R1.B1 */
        /*5: G1,R0.B0, 6: G1,R0.B1, 7: G1,R1.B0, 8: G1,R1.B1 */
        .ZHDR_Mode = 8,
#else
        .HDR_Support = 2,
#endif
        .SensorClockDividCount = 3,
        .SensorClockRisingCount = 0,
        .SensorClockFallingCount = 2,
        .SensorPixelClockCount = 3,
        .SensorDataLatchCount = 2,
        .MIPIDataLowPwr2HighSpeedTermDelayCount = 0,
        .MIPICLKLowPwr2HighSpeedTermDelayCount = 0,
        .SensorWidthSampling = 0,
        .SensorHightSampling = 0,
        .SensorPacketECCOrder = 1,
        .SensorGainfactor = 2,
        .SensorHFlip = 0,
        .SensorVFlip = 0,
    }};

#endif  // CAMERA_HAL_MEDIATEK_MTKCAM_CUSTOM_MT8183_HAL_IMGSENSOR_SRC_IMGSENSOR_CUSTOM_INFO_H_
