/***************************************************************************
 * COPYRIGHT NOTICE
 * Copyright 2023 Horizon Robotics.
 * All rights reserved.
 ***************************************************************************/
#define pr_fmt(fmt)             "[sc230ai]:" fmt

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/i2c-dev.h>
#include "hb_i2c.h"
#include "hb_cam_utility.h"
#include "inc/sc230ai_setting.h"
#include "inc/sensor_effect_common.h"
#include "hb_camera_data_config.h"

int sc230ai_common_data_init(sensor_info_t *sensor_info);
static int32_t sensor_dynamic_switch_fps(sensor_info_t *sensor_info, uint32_t fps);
static int32_t sensor_update_fps_notify_driver(sensor_info_t *sensor_info);

int sensor_poweroff(sensor_info_t *sensor_info)
{
        int gpio, ret = RET_OK;
        if(sensor_info->gpio_num > 0) {
                for(gpio = 0; gpio < sensor_info->gpio_num; gpio++) {
                        if(sensor_info->gpio_pin[gpio] != -1) {
                                ret = vin_power_ctrl(sensor_info->gpio_pin[gpio],
                                sensor_info->gpio_level[gpio]);
                                if(ret < 0) {
                                        vin_err("vin_power_ctrl fail\n");
                                        return -HB_CAM_SENSOR_POWEROFF_FAIL;
                                }
                        }
                }
        }

        if (sensor_info->sen_devfd != 0) {
                close(sensor_info->sen_devfd);
                sensor_info->sen_devfd = -1;
        }
        return ret;
}

int sensor_poweron(sensor_info_t *sensor_info)
{
        int gpio, ret = RET_OK;
        vin_dbg("%s gpio_num = %d \n", sensor_info->sensor_name, sensor_info->gpio_num);
        if(sensor_info->gpio_num > 0) {
                for(gpio = 0; gpio < sensor_info->gpio_num; gpio++) {
                        vin_dbg("%s gpio_pin[%d] = %d \n", sensor_info->sensor_name, gpio, sensor_info->gpio_pin[gpio]);
                        if(sensor_info->gpio_pin[gpio] != -1) {
                                ret = vin_power_ctrl(sensor_info->gpio_pin[gpio],
                                        sensor_info->gpio_level[gpio]);
                                usleep(100 * 1000);  //100ms
                                ret |= vin_power_ctrl(sensor_info->gpio_pin[gpio],
                                        1 - sensor_info->gpio_level[gpio]);
                                if(ret < 0) {
                                        vin_err("vin_power_ctrl fail\n");
                                        return -HB_CAM_SENSOR_POWERON_FAIL;
                                }
                                usleep(100 * 1000);  //100ms
                        }
                }
        }

        return ret;
}

int sensor_init(sensor_info_t *sensor_info)
{
        int ret = RET_OK;
        int setting_size = 0;

        ret = sensor_poweron(sensor_info);
        if (ret < 0) {
                vin_err("%d : sensor reset %s fail\n",
                        __LINE__, sensor_info->sensor_name);
                return ret;  //-HB_CAM_SENSOR_POWERON_FAIL
        }

        switch(sensor_info->sensor_mode) {
                case NORMAL_M:  // 1: normal
                        vin_info("sc230ai in normal linear mode\n");
                        vin_info("bus_num = %d, sensor_addr = 0x%0x\n", sensor_info->bus_num, sensor_info->sensor_addr);

                        setting_size = sizeof(sc230ai_linear_30fps_init_setting) / sizeof(uint32_t) / 2;
                        ret = vin_write_array(sensor_info->bus_num, sensor_info->sensor_addr, 2,
                                                    setting_size, sc230ai_linear_30fps_init_setting);
                        if (ret < 0) {
                                vin_err("%d : init %s fail\n", __LINE__, sensor_info->sensor_name);
                                return -HB_CAM_I2C_WRITE_FAIL;
                        }
                        ret = sc230ai_common_data_init(sensor_info);
                        if (ret < 0) {
                                vin_err("%d : linear data init %s fail\n", __LINE__, sensor_info->sensor_name);
                                return -HB_CAM_INIT_FAIL;
                        }
                        break;
                case SLAVE_M:  // 6: slave mode
                        vin_info("sc230ai in slave linear mode\n");
                        vin_info("bus_num = %d, sensor_addr = 0x%0x\n", sensor_info->bus_num, sensor_info->sensor_addr);

                        setting_size = sizeof(sc230ai_linear_30fps_slave_mode_init_setting) / sizeof(uint32_t) / 2;
                        ret = vin_write_array(sensor_info->bus_num, sensor_info->sensor_addr, 2,
                                                    setting_size, sc230ai_linear_30fps_slave_mode_init_setting);
                        if (ret < 0) {
                                vin_err("%d : init %s fail\n", __LINE__, sensor_info->sensor_name);
                                return -HB_CAM_I2C_WRITE_FAIL;
                        }
                        ret = sc230ai_common_data_init(sensor_info);
                        if (ret < 0) {
                                vin_err("%d : linear data init %s fail\n", __LINE__, sensor_info->sensor_name);
                                return -HB_CAM_INIT_FAIL;
                        }
                        break;
                case DOL2_M:
                        vin_info("sc230ai in dol2 mode\n");
                        vin_info("bus_num = %d, sensor_addr = 0x%0x\n", sensor_info->bus_num, sensor_info->sensor_addr);

                        setting_size = sizeof(sc230ai_2lane_10bit_1920x1080_30fps_shdr_setting) / sizeof(uint32_t) / 2;
                        ret = vin_write_array(sensor_info->bus_num, sensor_info->sensor_addr, 2,
                                                    setting_size, sc230ai_2lane_10bit_1920x1080_30fps_shdr_setting);
                        if (ret < 0) {
                                vin_err("%d : init %s fail\n", __LINE__, sensor_info->sensor_name);
                                return -HB_CAM_I2C_WRITE_FAIL;
                        }
                        /*mabey need to change*/
                        ret = sc230ai_common_data_init(sensor_info);
                        if (ret < 0) {
                                vin_err("%d : dol2 data init %s fail\n", __LINE__, sensor_info->sensor_name);
                                return -HB_CAM_INIT_FAIL;
                        }
                        break;
                default:
                        vin_err("%d not support mode %d\n", __LINE__, sensor_info->sensor_mode);
                        ret = -HB_CAM_INIT_FAIL;
                        break;
        }
        vin_info("sc230ai config success under %d mode\n", sensor_info->sensor_mode);

        // Default 30fps
        // Switch frame rate based on application configuration
        //switch fps should be setted by user program, API: <hbn_camera_change_fps> !
	usleep(100 * 1000);  //100ms
	ret = sensor_dynamic_switch_fps(sensor_info, sensor_info->fps);
	if (ret < 0) {
		vin_err("sc230ai dynamic switch fps fail, ret = %d \n", ret);
		ret = HB_CAM_DYNAMIC_SWITCH_FPS_FAIL;
	}
        return ret;
}

int sensor_start(sensor_info_t *sensor_info)
{
        int ret = RET_OK;
        int setting_size = 0;

        switch(sensor_info->sensor_mode) {
                case NORMAL_M:
                case SLAVE_M:
                        setting_size = sizeof(sc230ai_stream_on_setting)/sizeof(uint32_t)/2;
                        vin_info("%s start normal / slave linear mode\n", sensor_info->sensor_name);
                        ret = vin_write_array(sensor_info->bus_num, sensor_info->sensor_addr, 2,
                                        setting_size, sc230ai_stream_on_setting);
                        if(ret < 0) {
                                vin_err("start %s fail\n", sensor_info->sensor_name);
                                return -HB_CAM_I2C_WRITE_FAIL;
                        }
                        break;
                case DOL2_M:
                        setting_size = sizeof(sc230ai_stream_on_setting)/sizeof(uint32_t)/2;
                        vin_info("%s start dol2 mode\n", sensor_info->sensor_name);
                        ret = vin_write_array(sensor_info->bus_num, sensor_info->sensor_addr, 2,
                                        setting_size, sc230ai_stream_on_setting);
                        if(ret < 0) {
                                vin_err("start %s fail\n", sensor_info->sensor_name);
                                return -HB_CAM_I2C_WRITE_FAIL;
                        }
                        break;
                default:
                        vin_err("%d not support mode %d\n", __LINE__, sensor_info->sensor_mode);
                        ret = -HB_CAM_START_FAIL;
                        break;
        }
        return ret;
}

int sensor_stop(sensor_info_t *sensor_info)
{
        int ret = RET_OK;
        int setting_size = 0;

        setting_size =
                sizeof(sc230ai_stream_off_setting) / sizeof(uint32_t) / 2;
        vin_info("%s sensor stop\n", sensor_info->sensor_name);
        ret = vin_write_array(sensor_info->bus_num,
                        sensor_info->sensor_addr, 2,
                        setting_size, sc230ai_stream_off_setting);
        if (ret < 0) {
                vin_err("start %s fail\n", sensor_info->sensor_name);
                return -HB_CAM_I2C_WRITE_FAIL;
        }

        return ret;
}

int sensor_deinit(sensor_info_t *sensor_info)
{
        int ret = RET_OK;

        ret = sensor_poweroff(sensor_info);
        if (ret < 0) {
                vin_err("%d : deinit %s fail\n", __LINE__, sensor_info->sensor_name);
                return ret; //-HB_CAM_SENSOR_POWEROFF_FAIL
        }
        return ret;
}

int sc230ai_common_data_init(sensor_info_t *sensor_info)
{
        int ret = RET_OK;
        uint32_t  open_cnt = 0;
        sensor_turning_data_t turning_data;
        uint32_t *stream_on = turning_data.stream_ctrl.stream_on;
        uint32_t *stream_off = turning_data.stream_ctrl.stream_off;

        uint16_t VTS_HIGH;
        uint16_t VTS_LOW;
        uint32_t VTS_VALUE;

        memset(&turning_data, 0, sizeof(sensor_turning_data_t));

        // common data
        turning_data.bus_num = sensor_info->bus_num;
        turning_data.bus_type = sensor_info->bus_type;
        turning_data.port = sensor_info->port;
        turning_data.reg_width = sensor_info->reg_width;
        turning_data.mode = sensor_info->sensor_mode;
        if (sensor_info->sensor_mode == SLAVE_M)
                turning_data.mode = NORMAL_M;
        turning_data.sensor_addr = sensor_info->sensor_addr;
        strncpy(turning_data.sensor_name, sensor_info->sensor_name,
                sizeof(turning_data.sensor_name));

        turning_data.sensor_data.active_width = 1920;
        turning_data.sensor_data.active_height = 1080;

        //read vts = frame_length = HMAX, default value is 0x0465 = 1125
        VTS_HIGH = hb_vin_i2c_read_reg16_data8(sensor_info->bus_num, sensor_info->sensor_addr, 0x320e);
        VTS_HIGH = VTS_HIGH & 0x7f; //0x320e[0:6]
        VTS_LOW = hb_vin_i2c_read_reg16_data8(sensor_info->bus_num, sensor_info->sensor_addr, 0x320f);
        VTS_VALUE = (VTS_HIGH << 8 | VTS_LOW);
#ifdef AE_DBG
        printf("%s read VTS_HIGH = 0x%x, VTS_LOW = 0x%x, VTS = 0x%x \n",
                __FUNCTION__, VTS_HIGH, VTS_LOW, VTS_VALUE);
#endif
        turning_data.sensor_data.lines_per_second = 33750;//vts * fps, should be fixed = 1125 * 30
        //lines_per_second/fps = vts
        // from customer, max 10ms
        // 1000ms -lines_per_second - 33750
        // 10ms - 337, 33ms - 1125
        turning_data.sensor_data.exposure_time_max = 1012; //from customer, max 10ms
        turning_data.sensor_data.exposure_time_min = 1;
        turning_data.sensor_data.exposure_time_long_max = 2 * VTS_VALUE - 8;  //2*frame_length - 8  //linear not use
        turning_data.sensor_data.analog_gain_max = 251; //we use again + dig fine gain
        turning_data.sensor_data.digital_gain_max = 0;
        turning_data.sensor_data.analog_gain_init = 64;
        turning_data.sensor_data.digital_gain_init = 0;
        turning_data.sensor_data.exposure_time_init = 377;
	// turning_data.sensor_data.user_define_delay = 1;
	// turning_data.sensor_data.gain_delay_frame = 3;
	// turning_data.sensor_data.line_delay_frame = 3;
	// turning_data.sensor_data.ae_again_mode = 0;

        if (sensor_info->sensor_mode == DOL2_M)
        {
            turning_data.dol2.line_p[0].ratio = 1 << 8;
            turning_data.dol2.line_p[0].offset = 0;
            turning_data.dol2.line_p[0].max = 66;
            turning_data.dol2.line_p[1].ratio = 1 << 8;
            turning_data.dol2.line_p[1].offset = 0;
            turning_data.dol2.line_p[1].max = 2176;
        }

        //sensor bit && bayer
        sensor_data_bayer_fill(&turning_data.sensor_data, 10, (uint32_t)BAYER_START_B, (uint32_t)BAYER_PATTERN_RGGB);
        // sensor exposure_max_bit, maybe not used ?  //FIXME
        sensor_data_bits_fill(&turning_data.sensor_data, 12);

        /* line and gain userspace control donnot need set those params */
#if 0
        turning_data.sensor_data.turning_type = 6;  //FIXME should be tuning_type ???
        turning_data.sensor_data.conversion = 1;
        turning_data.normal.line_p.ratio = 1 << 8;
        turning_data.normal.line_p.offset = 0;
        turning_data.normal.line_p.max = 968;
#endif
        //some stress test case, we need kernel stream_ctrl.
        turning_data.stream_ctrl.data_length = 1;

        if(sizeof(turning_data.stream_ctrl.stream_on) >= sizeof(sc230ai_stream_on_setting)) {
                memcpy(stream_on, sc230ai_stream_on_setting, sizeof(sc230ai_stream_on_setting));
        } else {
                vin_err("Number of registers on stream over 10\n");
                return -RET_ERROR;
        }
        if(sizeof(turning_data.stream_ctrl.stream_off) >= sizeof(sc230ai_stream_off_setting)) {
                memcpy(stream_off, sc230ai_stream_off_setting, sizeof(sc230ai_stream_off_setting));
        } else {
                vin_err("Number of registers on stream over 10\n");
                return -RET_ERROR;
        }

        // sync gain lut to kernel driver.
        turning_data.normal.again_lut = malloc(256 * sizeof(uint32_t));
        if (turning_data.normal.again_lut != NULL) {
                memset(turning_data.normal.again_lut, 0xff, 256 * sizeof(uint32_t));
                memcpy(turning_data.normal.again_lut, sc230ai_gain_lut,
                        sizeof(sc230ai_gain_lut));
        }

        ret = ioctl(sensor_info->sen_devfd, SENSOR_TURNING_PARAM, &turning_data);

        if (turning_data.normal.again_lut) {
                free(turning_data.normal.again_lut);
                turning_data.normal.again_lut = NULL;
        }

        if (ret < 0) {
                vin_err("%s sync gain lut ioctl fail %d\n", sensor_info->sensor_name, ret);
                return -RET_ERROR;
        }

        return ret;
}


/* Helper function: Set gain register */
static void set_gain_registers(hal_control_info_t *info, uint16_t again_reg, uint16_t dgain_reg, uint16_t dfine_reg,
        char again_val, char dgain_val, char dfine_val)
{
        vin_i2c_write8(info->bus_num, 16, info->sensor_addr, again_reg, again_val);
        vin_i2c_write8(info->bus_num, 16, info->sensor_addr, dgain_reg, dgain_val);
        vin_i2c_write8(info->bus_num, 16, info->sensor_addr, dfine_reg, dfine_val);
}

/* Helper Function: Exposure Time Register Setup */
static void set_exposure_registers(hal_control_info_t *info, uint16_t exp_line0, uint16_t exp_line1, uint16_t exp_line2,
           uint32_t sline)
{
        char temp0, temp1, temp2;

        if (sline > 2022) {
        sline = 2022;
        }

        temp0 = (sline >> 12) & 0x0F;
        vin_i2c_write8(info->bus_num, 16, info->sensor_addr, exp_line0, temp0);
        temp1 = (sline >> 4) & 0xFF;
        vin_i2c_write8(info->bus_num, 16, info->sensor_addr, exp_line1, temp1);
        temp2 = (sline & 0x0F) << 4;
        vin_i2c_write8(info->bus_num, 16, info->sensor_addr, exp_line2, temp2);

#ifdef AE_DBG
        printf("write sline = %d, 0x%04x = 0x%x, 0x%04x = 0x%x, 0x%04x = 0x%x \n",
        sline, exp_line0, temp0, exp_line1, temp1, exp_line2, temp2);
#endif
}

/* input value:
 * again, dgain should be lut index
 * gain_num, linear mode: 1; dol2 mode: 2
 */
 static int sensor_aexp_gain_control(hal_control_info_t *info, uint32_t mode, uint32_t *again, uint32_t *dgain, uint32_t gain_num)
 {
 #ifdef AE_DBG
     printf("test %s, mode = %d gain_num = %d again[0] = %d, dgain[0] = %d\n",
            __FUNCTION__, mode, gain_num, again[0], dgain[0]);
 #endif

     const uint16_t AGAIN = 0x3e09;
     const uint16_t DGAIN = 0x3e06;
     const uint16_t DFINE_GAIN = 0x3e07;
     char again_reg_value = 0;
     char dgain_reg_value = 0, d_fine_gain_reg_value = 0;
     int gain_index = 0;

     // Calculate Linear/HDR Long Gain Index
     if (again[0] >= sizeof(sc230ai_gain_lut)/sizeof(uint32_t))
         gain_index = sizeof(sc230ai_gain_lut)/sizeof(uint32_t) - 1;
     else
         gain_index = again[0];

     // Calculate Gain Value
     again_reg_value = (sc230ai_gain_lut[gain_index] >> 16) & 0x000000FF;
     dgain_reg_value = (sc230ai_gain_lut[gain_index] >> 8) & 0x000000FF;
     d_fine_gain_reg_value = sc230ai_gain_lut[gain_index] & 0x000000FF;

 #ifdef AE_DBG
     printf("%s, gain_index: %d, 0x3e09 = 0x%x dgain: 0x3e06 = 0x%x dig fine gain: 0x3e07 = 0x%x\n",
            __FUNCTION__, gain_index, again_reg_value, dgain_reg_value, d_fine_gain_reg_value);
 #endif

     // Configure registers based on the mode.
     if (mode == NORMAL_M || mode == SLAVE_M) {
         set_gain_registers(info, AGAIN, DGAIN, DFINE_GAIN,
                           again_reg_value, dgain_reg_value, d_fine_gain_reg_value);
     } else if(mode == DOL2_M) {
        int gain_short_index = 0;
        const uint16_t AGAIN_HDR_SHORT = 0x3e13;
        const uint16_t DGAIN_HDR_SHORT = 0x3e10;
        const uint16_t DFINE_GAIN_HDR_SHORT = 0x3e11;
        char again_short_reg_value = 0;
        char dgain_short_reg_value = 0, d_fine_gain_short_reg_value = 0;

        // Calculate HDR Short Gain Index
        if (again[1] >= sizeof(sc230ai_gain_lut)/sizeof(uint32_t))
            gain_short_index = sizeof(sc230ai_gain_lut)/sizeof(uint32_t) - 1;
        else
            gain_short_index = again[1];

 #ifdef AE_DBG
        printf("%s, gain_short_index: %d, 0x3e13 = 0x%x dgain: 0x3e10 = 0x%x dig fine gain: 0x3e11 = 0x%x\n",
                __FUNCTION__, gain_short_index, again_reg_value, dgain_reg_value, again[1]);
 #endif

        // Calculate Gain Value
        again_short_reg_value = (sc230ai_gain_lut[gain_short_index] >> 16) & 0x000000FF;
        dgain_short_reg_value = (sc230ai_gain_lut[gain_short_index] >> 8) & 0x000000FF;
        d_fine_gain_short_reg_value = sc230ai_gain_lut[gain_short_index] & 0x000000FF;
        // Set Long Exposure Gain,
        set_gain_registers(info, AGAIN, DGAIN, DFINE_GAIN,
            again_reg_value, dgain_reg_value, d_fine_gain_reg_value);

        // Set Short Exposure Gain
        set_gain_registers(info, AGAIN_HDR_SHORT, DGAIN_HDR_SHORT, DFINE_GAIN_HDR_SHORT,
            again_short_reg_value, dgain_short_reg_value, d_fine_gain_short_reg_value);
     } else {
         vin_err(" unsupport mode %d\n", mode);
     }

     return 0;
}

/* input value:
 * line: exposure time value
 * line_num: linear mode: 1; dol2 mode: 2
 * */
 static int sensor_aexp_line_control(hal_control_info_t *info, uint32_t mode, uint32_t *line, uint32_t line_num)
 {
 #ifdef AE_DBG
     printf("line mode %d, --line %d , line_num:%d \n", mode, line[0], line_num);
 #endif

     const uint16_t EXP_LINE0 = 0x3e00;
     const uint16_t EXP_LINE1 = 0x3e01;
     const uint16_t EXP_LINE2 = 0x3e02;

     if (mode == NORMAL_M || mode == SLAVE_M) {
         /*
         * NOTICE: sensor exposure half line, so sline = 2 * line(from isp)
         * exposure line max, from customer:
         * exposure_time_max = 10ms, line = 337, result = 337 * 2 = 674
         * form spec:
         * exposure_time_max = 2 * VTS - 8, 10fps, result = 11250 * 2 - 8
         * so, we should limit sline = 674
         */
         uint32_t sline = 2 * line[0];
         set_exposure_registers(info, EXP_LINE0, EXP_LINE1, EXP_LINE2, sline);
     } else if(mode == DOL2_M) {
         const uint16_t EXP_HDR_SHORT_LINE0 = 0x3e22;
         const uint16_t EXP_HDR_SHORT_LINE1 = 0x3e04;
         const uint16_t EXP_HDR_SHORT_LINE2 = 0x3e05;

         /*
         * NOTICE: sensor exposure half line, so sline = 2 * line(from isp)
         * exposure line max, from customer:
         * exposure_time_max = 10ms, line = 337, result = 337 * 2 = 674
         * form spec:
         * exposure_time_max = 2 * VTS - 8, 10fps, result = 11250 * 2 - 8
         * so, we should limit sline = 674
         */
         uint32_t sline = 2 * line[0];
         uint32_t sline_short = 2 * line[1];

         // 设置长曝光时间
         set_exposure_registers(info, EXP_LINE0, EXP_LINE1, EXP_LINE2, sline);

         // 设置短曝光时间
         set_exposure_registers(info, EXP_HDR_SHORT_LINE0, EXP_HDR_SHORT_LINE1, EXP_HDR_SHORT_LINE2, sline_short);
     } else {
         vin_err(" unsupport mode %d\n", mode);
     }

     return 0;
}

static int sensor_userspace_control(uint32_t port, uint32_t *enable)
{
        vin_info("enable userspace gain control and line control\n");
        *enable = HAL_GAIN_CONTROL | HAL_LINE_CONTROL;
        // *enable = 0;
        return 0;
}

static int32_t sensor_update_fps_notify_driver(sensor_info_t *sensor_info)
{
        int32_t ret = RET_OK;

        switch(sensor_info->sensor_mode) {
                case (uint32_t)NORMAL_M:
                case (uint32_t)SLAVE_M:
                        ret = sc230ai_common_data_init(sensor_info);
                        if (ret < 0) {
                                vin_err("update fps sc230ai_common_data_init fail\n");
                                return ret;
                        }
                        break;
                case (uint32_t)DOL2_M:
                        /*mabey change*/
                        ret = sc230ai_common_data_init(sensor_info);
                        if (ret < 0) {
                                vin_err("update fps sc230ai_common_data_init fail\n");
                                return ret;
                        }
                        break;
                default:
                        vin_err("update fps not support %d mode \n", sensor_info->sensor_mode);
                        break;
        }

        return ret;
}

/* input value:
 * fps: set fps
 *
 * we can use this function to dynamic switch fps in our program.
 * int32_t hbn_camera_change_fps(camera_handle_t cam_fd, int32_t fps)
 */
static int32_t sensor_dynamic_switch_fps(sensor_info_t *sensor_info, uint32_t fps)
{
        int32_t ret = RET_OK;
        const uint16_t SC230AI_VTS_LOW = 0x320f;
        const uint16_t SC230AI_VTS_HIGH = 0x320e;
        int32_t vts;

        vin_info("%s %s %dfps \n", __FUNCTION__, sensor_info->sensor_name, fps);

        if (fps < 1 || sensor_info->fps > 30) {
                vin_err("%s %s %dfps not support\n", __FUNCTION__, sensor_info->sensor_name, fps);
                return -RET_ERROR;
        }

        switch (sensor_info->sensor_mode) {
                case NORMAL_M:
                case SLAVE_M:
                        //NOTICE:
                        //vts = frame_length = lines_per_second / fps
                        vts = 33750 / fps;
                        break;
                case DOL2_M:
                default:
                        vin_err("%s not support mode %d \n", __FUNCTION__, sensor_info->sensor_mode);
                        return -RET_ERROR;
        }

#ifdef AE_DBG
        printf("%s set fps = %d, vts = 0x%x \n", __FUNCTION__, fps, vts);
#endif
        ret = hb_vin_i2c_write_reg16_data8(sensor_info->bus_num, sensor_info->sensor_addr,
                        SC230AI_VTS_HIGH, ((vts >> 8) & 0x7f)); //0x320e[0:6] 0x0d for 10fps
        ret |= hb_vin_i2c_write_reg16_data8(sensor_info->bus_num, sensor_info->sensor_addr,
                        SC230AI_VTS_LOW, (vts & 0xff) - 2); // 减小2行加点裕度 0x2d for 10fps
        if (ret < 0) {
                vin_err("%s %s write vts=0x%x fail \n", __FUNCTION__, sensor_info->sensor_name, vts);
                return -HB_CAM_I2C_WRITE_FAIL;
        }

        sensor_info->fps = fps;
        sensor_update_fps_notify_driver(sensor_info);
        vin_info("%s dynamic switch to %dfps success \n", sensor_info->sensor_name, fps);
        return RET_OK;
}

#ifdef CAMERA_FRAMEWORK_HBN
SENSOR_MODULE_F(sc230ai, CAM_MODULE_FLAG_A16D8);
sensor_module_t sc230ai = {
        .module = SENSOR_MNAME(sc230ai),
#else
sensor_module_t sc230ai = {
        .module = "sc230ai",
#endif
        .init = sensor_init,
        .start = sensor_start,
        .stop = sensor_stop,
        .deinit = sensor_deinit,
        .power_on = sensor_poweron,
        .power_off = sensor_poweroff,
        .aexp_gain_control = sensor_aexp_gain_control,
        .aexp_line_control = sensor_aexp_line_control,
        .dynamic_switch_fps = sensor_dynamic_switch_fps,
        .userspace_control = sensor_userspace_control,
};
