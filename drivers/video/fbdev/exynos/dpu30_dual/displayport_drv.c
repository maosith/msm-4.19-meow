/*
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung SoC DisplayPort driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/pm_runtime.h>
#include <linux/of_gpio.h>
#include <linux/device.h>
#include <linux/module.h>
#include <video/mipi_display.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-dv-timings.h>
#if defined(CONFIG_CPU_IDLE)
#include <soc/samsung/exynos-powermode.h>
#endif
#if defined(CONFIG_SND_SOC_SAMSUNG_DISPLAYPORT)
#include <sound/samsung/dp_ado.h>
#endif
#include <linux/smc.h>
#include <linux/exynos_iovmm.h>

#if defined(CONFIG_PHY_EXYNOS_USBDRD)
#include "../../../drivers/phy/samsung/phy-exynos-usbdrd.h"
#endif
#include "displayport.h"
#include "displayport_hdcp22_if.h"
#include "decon.h"

#define PIXELCLK_2160P30HZ 297000000 /* UHD 30hz */
#define PIXELCLK_1080P60HZ 148500000 /* FHD 60Hz */
#define PIXELCLK_1080P30HZ 74250000 /* FHD 30Hz */

int displayport_log_level = 6;
static u64 reduced_resolution;
struct displayport_debug_param g_displayport_debug_param;

extern enum hdcp22_auth_def hdcp22_auth_state;
struct displayport_device *displayport_drvdata;
EXPORT_SYMBOL(displayport_drvdata);

static int displayport_runtime_suspend(struct device *dev);
static int displayport_runtime_resume(struct device *dev);
static enum displayport_state displayport_check_sst_on(struct displayport_device *displayport);

void displayport_hdcp22_enable(u32 en);

void displayport_dump_registers(struct displayport_device *displayport)
{
	displayport_info("=== DisplayPort SFR DUMP ===\n");

	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			displayport->res.link_regs, 0xC0, false);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			displayport->res.link_regs + 0x100, 0x0C, false);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			displayport->res.link_regs + 0x200, 0x08, false);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			displayport->res.link_regs + 0x2000, 0x64, false);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			displayport->res.link_regs + 0x5000, 0x104, false);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			displayport->res.link_regs + 0x5400, 0x46C, false);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			displayport->res.link_regs + 0x6000, 0x104, false);
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 32, 4,
			displayport->res.link_regs + 0x6400, 0x46C, false);
}

static int displayport_remove(struct platform_device *pdev)
{
	struct displayport_device *displayport = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
#if defined(CONFIG_EXTCON)
	devm_extcon_dev_unregister(displayport->dev, displayport->extcon_displayport);
#else
	displayport_info("Not compiled EXTCON driver\n");
#endif
	mutex_destroy(&displayport->cmd_lock);
	mutex_destroy(&displayport->hpd_lock);
	mutex_destroy(&displayport->aux_lock);
	mutex_destroy(&displayport->training_lock);
	mutex_destroy(&displayport->hdcp2_lock);
	destroy_workqueue(displayport->dp_wq);
	destroy_workqueue(displayport->hdcp2_wq);
	displayport_info("displayport driver removed\n");

	return 0;
}

u32 displayport_get_decon_id(u32 sst_id)
{
	u32 decon_id = SST1_DECON_ID;

	switch (sst_id) {
	case SST1:
		decon_id = SST1_DECON_ID;
		break;
	case SST2:
		decon_id = SST2_DECON_ID;
		break;
	default:
		decon_id = SST1_DECON_ID;
	}

	return decon_id;
}

u32 displayport_get_sst_id_with_decon_id(u32 decon_id)
{
	u32 i = 0;
	int ret_val = SST1;
	struct displayport_device *displayport = get_displayport_drvdata();

	for (i = SST1; i < MAX_SST_CNT; i++) {
		if (decon_id == displayport->sst[i]->decon_id) {
			displayport_dbg("SST%d connect DECON%d\n", i + 1, decon_id);
			ret_val = displayport->sst[i]->id;
			break;
		}
	}

	return ret_val;
}

int displayport_get_sst_id_with_port_number(u8 port_number, u8 get_sst_id_type)
{
	int i = 0;
	int ret_val = 0;
	struct displayport_device *displayport = get_displayport_drvdata();

	if (get_sst_id_type == FIND_SST_ID) {
		for (i = SST1; i < MAX_SST_CNT; i++) {
			if (port_number == displayport->sst[i]->vc_config->port_num) {
				displayport_dbg("SST%d's port number = %d\n", i + 1,
						displayport->sst[i]->vc_config->port_num);
				break;
			}
		}
	} else if (get_sst_id_type == ALLOC_SST_ID) {
		for (i = SST1; i < MAX_SST_CNT; i++) {
			if (displayport->sst[i]->hpd_state == HPD_UNPLUG) {
				displayport_dbg("SST%d's port number = %d\n", i + 1, port_number);
				displayport->sst[i]->vc_config->port_num = port_number;
				break;
			}
		}
	}

	if (i >= MAX_SST_CNT) {
		displayport_dbg("Can't get SST ID with port number\n");
		ret_val = -EINVAL;
	} else
		ret_val = displayport->sst[i]->id;

	return ret_val;
}

void displayport_clr_vc_config(u32 ch,
		struct displayport_device *displayport)
{
	displayport->sst[ch]->vc_config->port_num = 0;
	displayport->sst[ch]->vc_config->timeslot = 0;
	displayport->sst[ch]->vc_config->timeslot_start_no = 0;
	displayport->sst[ch]->vc_config->pbn = 0;
	displayport->sst[ch]->vc_config->xvalue = 0;
	displayport->sst[ch]->vc_config->yvalue = 0;
	displayport->sst[ch]->vc_config->mode_enable = 0;
}

int displayport_calc_vc_config(u32 ch,
		struct displayport_device *displayport)
{
	int ret = 0;
	u64 pixelbandwidth = 0;
	u32 bitsperpixel = 24;
	u64 linkbandwidth = 0;
	u64 calc_temp = 0;
	u32 lane_cnt = displayport_reg_get_lane_count();

	displayport->sst[ch]->vc_config->mode_enable = DPTX_MST_FEC_DSC_DISABLE;

	switch (displayport->sst[ch]->bpc) {
	case BPC_8:
		bitsperpixel = 24;
		break;
	case BPC_10:
		bitsperpixel = 30;
		break;
	default:
		bitsperpixel = 18;
		break;
	}

	pixelbandwidth = (displayport_reg_get_video_clk(ch) / 1000) * (bitsperpixel / 8);
	displayport->sst[ch]->vc_config->pbn =
			(((pixelbandwidth * (64000 / 54)) / 1000) * 1006 / 1000) / 1000 + 1;
	displayport_info("CH%d PBN = %d", ch + 1, displayport->sst[ch]->vc_config->pbn);

	linkbandwidth = displayport_reg_get_ls_clk() * lane_cnt;
	displayport->sst[ch]->vc_config->timeslot =
			(pixelbandwidth / (linkbandwidth / 1000000)) * 64 / 1000 + 1;
	displayport_info("CH%d TimeSlot = %d", ch + 1, displayport->sst[ch]->vc_config->timeslot);

	calc_temp = ((displayport->sst[ch]->vc_config->pbn * 54 * 1000)
					/ (linkbandwidth / 1000000)) * lane_cnt;
	displayport->sst[ch]->vc_config->xvalue = calc_temp / 1000;
	displayport->sst[ch]->vc_config->yvalue = calc_temp - (displayport->sst[ch]->vc_config->xvalue * 1000);
	displayport_info("CH%d StreamSymbolTimeSlotsPerMTP = %d.%d", ch + 1,
			displayport->sst[ch]->vc_config->xvalue, displayport->sst[ch]->vc_config->yvalue);

	return ret;
}

static u64 displayport_find_edid_max_pixelclock(void)
{
	int i;

	for (i = supported_videos_pre_cnt - 1; i > 0; i--) {
		if (supported_videos[i].edid_support_match) {
			displayport_info("max video_format : %s\n",
				supported_videos[i].name);
			break;
		}
	}

	return supported_videos[i].dv_timings.bt.pixelclock;
}

static int displayport_check_edid_max_clock(u32 sst_id,
		struct displayport_device *displayport, videoformat video_format, enum bit_depth bpc)
{
	int ret_val = true;
	u64 calc_pixel_clock = 0;

	switch (bpc) {
	case BPC_8:
		calc_pixel_clock = supported_videos[video_format].dv_timings.bt.pixelclock;
		break;
	case BPC_10:
		calc_pixel_clock = supported_videos[video_format].dv_timings.bt.pixelclock * 125 / 100;
		break;
	default:
		calc_pixel_clock = supported_videos[video_format].dv_timings.bt.pixelclock;
		break;
	}

	if (displayport->sst[sst_id]->rx_edid_data.max_support_clk != 0) {
		if (calc_pixel_clock > displayport->sst[sst_id]->rx_edid_data.max_support_clk * MHZ) {
			displayport_info("RX support Max TMDS Clock = %d, but pixel clock = %llu\n",
					displayport->sst[sst_id]->rx_edid_data.max_support_clk * MHZ, calc_pixel_clock);
			ret_val = false;
		}
	} else
		displayport_info("Can't check RX support Max TMDS Clock\n");

	return ret_val;
}

static int displayport_check_link_rate_pixel_clock(u8 link_rate,
		u8 lane_cnt, u64 pixel_clock, enum bit_depth bpc)
{
	u64 calc_pixel_clock = 0;
	u64 pixel_clock_with_bpc = 0;
	int ret_val = false;

	switch (link_rate) {
	case LINK_RATE_1_62Gbps:
		calc_pixel_clock = RBR_PIXEL_CLOCK_PER_LANE * lane_cnt;
		break;
	case LINK_RATE_2_7Gbps:
		calc_pixel_clock = HBR_PIXEL_CLOCK_PER_LANE * lane_cnt;
		break;
	case LINK_RATE_5_4Gbps:
		calc_pixel_clock = HBR2_PIXEL_CLOCK_PER_LANE * lane_cnt;
		break;
	case LINK_RATE_8_1Gbps:
		calc_pixel_clock = HBR3_PIXEL_CLOCK_PER_LANE * lane_cnt;
		break;
	default:
		calc_pixel_clock = HBR2_PIXEL_CLOCK_PER_LANE * lane_cnt;
		break;
	}

	switch (bpc) {
	case BPC_8:
		pixel_clock_with_bpc = pixel_clock;
		break;
	case BPC_10:
		pixel_clock_with_bpc = pixel_clock * 125 / 100;
		break;
	default:
		pixel_clock_with_bpc = pixel_clock;
		break;
	}

	if (calc_pixel_clock >= pixel_clock_with_bpc)
		ret_val = true;

	if (ret_val == false)
		displayport_info("link rate: 0x%x, lane cnt: %d, pixel_clock_with_bpc = %llu, calc_pixel_clock = %llu\n",
				link_rate, lane_cnt, pixel_clock_with_bpc, calc_pixel_clock);

	return ret_val;
}

static int displayport_check_pixel_clock_for_hdr(u32 sst_id,
		struct displayport_device *displayport,	videoformat video_format)
{
	int ret_val = false;

	if (displayport->sst[sst_id]->rx_edid_data.hdr_support) {
		ret_val = displayport_check_edid_max_clock(sst_id, displayport, video_format, BPC_10);

		if (ret_val == true)
			ret_val = displayport_check_link_rate_pixel_clock(displayport_reg_phy_get_link_bw(),
							displayport_reg_get_lane_count(),
							supported_videos[video_format].dv_timings.bt.pixelclock,
							BPC_10);
	}

	return ret_val;
}

static int displayport_get_min_link_rate(u8 rx_link_rate,
		u8 lane_cnt, enum bit_depth bpc)
{
	int i = 0;
	int link_rate[MAX_LINK_RATE_NUM] = {LINK_RATE_1_62Gbps,
			LINK_RATE_2_7Gbps, LINK_RATE_5_4Gbps , LINK_RATE_8_1Gbps};
	u64 max_pclk = 0;
	u8 min_link_rate = 0;

	if (rx_link_rate == LINK_RATE_1_62Gbps)
		return rx_link_rate;

	if (lane_cnt > 4)
		return LINK_RATE_5_4Gbps;

	max_pclk = displayport_find_edid_max_pixelclock();
	for (i = 0; i < MAX_LINK_RATE_NUM; i++) {
		if (displayport_check_link_rate_pixel_clock(link_rate[i],
				lane_cnt, max_pclk, bpc) == true)
			break;
	}

	if (i > MAX_LINK_RATE_NUM)
		min_link_rate = LINK_RATE_5_4Gbps;
	else
		min_link_rate = link_rate[i] > rx_link_rate ? rx_link_rate : link_rate[i];

	displayport_info("set link late: 0x%x, lane cnt:%d\n", min_link_rate, lane_cnt);

	return min_link_rate;
}

void displayport_get_voltage_and_pre_emphasis_max_reach(u8 *drive_current, u8 *pre_emphasis, u8 *max_reach_value)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (drive_current[i] >= MAX_REACHED_CNT) {
			max_reach_value[i] &= ~(1 << MAX_SWING_REACHED_BIT_POS);
			max_reach_value[i] |= (1 << MAX_SWING_REACHED_BIT_POS);
		} else
			max_reach_value[i] &= ~(1 << MAX_SWING_REACHED_BIT_POS);

		if (pre_emphasis[i] >= MAX_REACHED_CNT) {
			max_reach_value[i] &= ~(1 << MAX_PRE_EMPHASIS_REACHED_BIT_POS);
			max_reach_value[i] |= (1 << MAX_PRE_EMPHASIS_REACHED_BIT_POS);
		} else
			max_reach_value[i] &= ~(1 << MAX_PRE_EMPHASIS_REACHED_BIT_POS);
	}
}

static int displayport_full_link_training(u32 sst_id)
{
	u8 link_rate;
	u8 lane_cnt;
	u8 training_aux_rd_interval;
	u8 pre_emphasis[MAX_LANE_CNT];
	u8 drive_current[MAX_LANE_CNT];
	u8 voltage_swing_lane[MAX_LANE_CNT];
	u8 pre_emphasis_lane[MAX_LANE_CNT];
	u8 max_reach_value[MAX_LANE_CNT];
	int training_retry_no, eq_training_retry_no, i;
	int total_retry_cnt = 0;
	u8 val[DPCD_BUF_SIZE] = {0,};
	u8 eq_val[DPCD_BUF_SIZE] = {0,};
	u8 lane_cr_done;
	u8 lane_channel_eq_done;
	u8 lane_symbol_locked_done;
	u8 interlane_align_done;
	u8 enhanced_frame_cap;
	int ret = 0;
	int tps3_supported = 0;
	enum bit_depth bpc = BPC_8;
	struct displayport_device *displayport = get_displayport_drvdata();
	struct decon_device *decon = get_decon_drvdata(DEFAULT_DECON_ID);

	displayport_reg_dpcd_read_burst(DPCD_ADD_REVISION_NUMBER, DPCD_BUF_SIZE, val);
	displayport_info("Full Link Training Start + : %02x %02x\n", val[1], val[2]);

	if (!displayport->hpd_current_state) {
		displayport_info("hpd is low in full link training\n");
		return 0;
	}

	link_rate = val[1];
	lane_cnt = val[2] & MAX_LANE_COUNT;
	tps3_supported = val[2] & TPS3_SUPPORTED;
	enhanced_frame_cap = val[2] & ENHANCED_FRAME_CAP;

	if (!displayport->auto_test_mode &&
			!(supported_videos[displayport->sst[sst_id]->best_video].pro_audio_support &&
			edid_support_pro_audio())) {
		if (displayport->sst[sst_id]->rx_edid_data.hdr_support)
			bpc = BPC_10;

		if (displayport->mst_cap == 1) {
			enhanced_frame_cap = 0;
			displayport_info("enhanced_frame_cap disable for MST mode\n");
		} else {
			link_rate = displayport_get_min_link_rate(link_rate, lane_cnt, bpc);
			displayport->auto_test_mode = 0;
		}
	}

	if (g_displayport_debug_param.param_used) {
		link_rate = g_displayport_debug_param.link_rate;
		lane_cnt = g_displayport_debug_param.lane_cnt;
	}

	displayport_reg_dpcd_read(DPCD_ADD_TRAINING_AUX_RD_INTERVAL, 1, val);
	training_aux_rd_interval = val[0];

Reduce_Link_Rate_Retry:
	displayport_info("Reduce_Link_Rate_Retry(0x%X)\n", link_rate);

	if (!displayport->hpd_current_state) {
		displayport_info("hpd is low in link rate retry\n");
		return 0;
	}

	for (i = 0; i < 4; i++) {
		pre_emphasis[i] = 0;
		drive_current[i] = 0;
		max_reach_value[i] = 0;
	}

	training_retry_no = 0;

	if (decon->state != DECON_STATE_ON
		|| displayport_reg_phy_get_link_bw() != link_rate
		|| displayport_reg_get_lane_count() != lane_cnt) {

		displayport_reg_phy_reset(1);
		displayport_reg_phy_init_setting();

		displayport_reg_phy_set_link_bw(link_rate);
		displayport_info("link_rate = %x\n", link_rate);

		displayport_reg_phy_mode_setting();

		displayport_reg_set_lane_count(lane_cnt);
		displayport_info("lane_cnt = %x\n", lane_cnt);

		if (enhanced_frame_cap)
			displayport_write_mask(SST1_MAIN_CONTROL + 0x1000 * sst_id,
					1, ENHANCED_MODE);

		/* wait for 60us */
		udelay(60);

		displayport_reg_phy_reset(0);
	} else
		displayport_info("skip phy_reset in link training\n");

	val[0] = link_rate;
	val[1] = lane_cnt;

	if (enhanced_frame_cap)
		val[1] |= ENHANCED_FRAME_CAP;

	displayport_reg_dpcd_write_burst(DPCD_ADD_LINK_BW_SET, 2, val);

	displayport_reg_wait_phy_pll_lock();

	displayport_reg_set_training_pattern(TRAINING_PATTERN_1);

	val[0] = 0x21;	/* SCRAMBLING_DISABLE, TRAINING_PATTERN_1 */
	displayport_reg_dpcd_write(DPCD_ADD_TRANING_PATTERN_SET, 1, val);

Voltage_Swing_Retry:
	displayport_dbg("Voltage_Swing_Retry\n");

	if (!displayport->hpd_current_state) {
		displayport_info("hpd is low in swing retry\n");
		return 0;
	}

	displayport_reg_set_voltage_and_pre_emphasis((u8 *)drive_current, (u8 *)pre_emphasis);
	displayport_get_voltage_and_pre_emphasis_max_reach((u8 *)drive_current,
			(u8 *)pre_emphasis, (u8 *)max_reach_value);

	val[0] = (pre_emphasis[0]<<3) | drive_current[0] | max_reach_value[0];
	val[1] = (pre_emphasis[1]<<3) | drive_current[1] | max_reach_value[1];
	val[2] = (pre_emphasis[2]<<3) | drive_current[2] | max_reach_value[2];
	val[3] = (pre_emphasis[3]<<3) | drive_current[3] | max_reach_value[3];
	displayport_info("Voltage_Swing_Retry %02x %02x %02x %02x\n", val[0], val[1], val[2], val[3]);
	displayport_reg_dpcd_write_burst(DPCD_ADD_TRANING_LANE0_SET, 4, val);

	if (training_aux_rd_interval != 0)
		mdelay(training_aux_rd_interval * 4);
	else
		udelay(100);

	lane_cr_done = 0;

	displayport_reg_dpcd_read(DPCD_ADD_LANE0_1_STATUS, 2, val);
	lane_cr_done |= ((val[0] & LANE0_CR_DONE) >> 0);
	lane_cr_done |= ((val[0] & LANE1_CR_DONE) >> 3);
	lane_cr_done |= ((val[1] & LANE2_CR_DONE) << 2);
	lane_cr_done |= ((val[1] & LANE3_CR_DONE) >> 1);

	displayport_dbg("lane_cr_done = %x\n", lane_cr_done);

	if (lane_cnt == 0x04) {
		if (lane_cr_done == 0x0F) {
			displayport_dbg("lane_cr_done\n");
			goto EQ_Training_Start;
		} else if (drive_current[0] == MAX_REACHED_CNT
			&& drive_current[1] == MAX_REACHED_CNT
			&& drive_current[2] == MAX_REACHED_CNT
			&& drive_current[3] == MAX_REACHED_CNT) {
			displayport_dbg("MAX_REACHED_CNT\n");
			goto Check_Link_rate;
		}
	} else if (lane_cnt == 0x02) {
		if (lane_cr_done == 0x03) {
			displayport_dbg("lane_cr_done\n");
			goto EQ_Training_Start;
		} else if (drive_current[0] == MAX_REACHED_CNT
			&& drive_current[1] == MAX_REACHED_CNT) {
			displayport_dbg("MAX_REACHED_CNT\n");
			goto Check_Link_rate;
		}
	} else if (lane_cnt == 0x01) {
		if (lane_cr_done == 0x01) {
			displayport_dbg("lane_cr_done\n");
			goto EQ_Training_Start;
		} else if (drive_current[0] == MAX_REACHED_CNT) {
			displayport_dbg("MAX_REACHED_CNT\n");
			goto Check_Link_rate;
		}
	} else {
		val[0] = 0x00;	/* SCRAMBLING_ENABLE, NORMAL_DATA */
		displayport_reg_dpcd_write(DPCD_ADD_TRANING_PATTERN_SET, 1, val);
		displayport_err("Full Link Training Fail : Link Rate %02x, lane Count %02x -",
				link_rate, lane_cnt);
		return -EINVAL;
	}

	displayport_reg_dpcd_read_burst(DPCD_ADD_ADJUST_REQUEST_LANE0_1, 2, val);
	voltage_swing_lane[0] = (val[0] & VOLTAGE_SWING_LANE0);
	pre_emphasis_lane[0] = (val[0] & PRE_EMPHASIS_LANE0) >> 2;
	voltage_swing_lane[1] = (val[0] & VOLTAGE_SWING_LANE1) >> 4;
	pre_emphasis_lane[1] = (val[0] & PRE_EMPHASIS_LANE1) >> 6;

	voltage_swing_lane[2] = (val[1] & VOLTAGE_SWING_LANE2);
	pre_emphasis_lane[2] = (val[1] & PRE_EMPHASIS_LANE2) >> 2;
	voltage_swing_lane[3] = (val[1] & VOLTAGE_SWING_LANE3) >> 4;
	pre_emphasis_lane[3] = (val[1] & PRE_EMPHASIS_LANE3) >> 6;

	if (drive_current[0] == voltage_swing_lane[0] &&
			drive_current[1] == voltage_swing_lane[1] &&
			drive_current[2] == voltage_swing_lane[2] &&
			drive_current[3] == voltage_swing_lane[3]) {
		if (training_retry_no == 4)
			goto Check_Link_rate;
		else
			training_retry_no++;
	} else
		training_retry_no = 0;

	for (i = 0; i < 4; i++) {
		drive_current[i] = voltage_swing_lane[i];
		pre_emphasis[i] = pre_emphasis_lane[i];
		displayport_dbg("v drive_current[%d] = %x\n",
				i, drive_current[i]);
		displayport_dbg("v pre_emphasis[%d] = %x\n",
				i, pre_emphasis[i]);
	}

	total_retry_cnt++;

	if (total_retry_cnt >= MAX_RETRY_CNT) {
		displayport_err("total_retry_cnt = %d\n", total_retry_cnt);
		goto Check_Link_rate;
	}

	goto Voltage_Swing_Retry;

Check_Link_rate:
	displayport_info("Check_Link_rate\n");

	total_retry_cnt = 0;

	if (link_rate == LINK_RATE_8_1Gbps) {
		link_rate = LINK_RATE_5_4Gbps;
		goto Reduce_Link_Rate_Retry;
	} else if (link_rate == LINK_RATE_5_4Gbps) {
		link_rate = LINK_RATE_2_7Gbps;
		goto Reduce_Link_Rate_Retry;
	} else if (link_rate == LINK_RATE_2_7Gbps) {
		link_rate = LINK_RATE_1_62Gbps;
		goto Reduce_Link_Rate_Retry;
	} else if (link_rate == LINK_RATE_1_62Gbps) {
		val[0] = 0x00;	/* SCRAMBLING_ENABLE, NORMAL_DATA */
		displayport_reg_dpcd_write(DPCD_ADD_TRANING_PATTERN_SET, 1, val);
		displayport_err("Full Link Training Fail : Link_Rate Retry -");
		return -EINVAL;
	}

EQ_Training_Start:
	displayport_info("EQ_Training_Start\n");

	eq_training_retry_no = 0;
	for (i = 0; i < DPCD_BUF_SIZE; i++)
		eq_val[i] = 0;

	if (tps3_supported) {
		displayport_reg_set_training_pattern(TRAINING_PATTERN_3);

		val[0] = 0x23;	/* SCRAMBLING_DISABLE, TRAINING_PATTERN_3 */
		displayport_reg_dpcd_write(DPCD_ADD_TRANING_PATTERN_SET, 1, val);
	} else {
		displayport_reg_set_training_pattern(TRAINING_PATTERN_2);

		val[0] = 0x22;	/* SCRAMBLING_DISABLE, TRAINING_PATTERN_2 */
		displayport_reg_dpcd_write(DPCD_ADD_TRANING_PATTERN_SET, 1, val);
	}

EQ_Training_Retry:
	displayport_dbg("EQ_Training_Retry\n");

	if (!displayport->hpd_current_state) {
		displayport_info("hpd is low in eq training retry\n");
		return 0;
	}

	displayport_reg_set_voltage_and_pre_emphasis((u8 *)drive_current, (u8 *)pre_emphasis);
	displayport_get_voltage_and_pre_emphasis_max_reach((u8 *)drive_current,
			(u8 *)pre_emphasis, (u8 *)max_reach_value);

	val[0] = (pre_emphasis[0]<<3) | drive_current[0] | max_reach_value[0];
	val[1] = (pre_emphasis[1]<<3) | drive_current[1] | max_reach_value[1];
	val[2] = (pre_emphasis[2]<<3) | drive_current[2] | max_reach_value[2];
	val[3] = (pre_emphasis[3]<<3) | drive_current[3] | max_reach_value[3];
	displayport_info("EQ_Training_Retry %02x %02x %02x %02x\n", val[0], val[1], val[2], val[3]);
	displayport_reg_dpcd_write_burst(DPCD_ADD_TRANING_LANE0_SET, 4, val);

	for (i = 0; i < 4; i++)
		eq_val[i] = val[i];

	lane_cr_done = 0;
	lane_channel_eq_done = 0;
	lane_symbol_locked_done = 0;
	interlane_align_done = 0;

	if (training_aux_rd_interval != 0)
		mdelay(training_aux_rd_interval * 4);
	else
		udelay(100);

	displayport_reg_dpcd_read_burst(DPCD_ADD_LANE0_1_STATUS, 3, val);
	lane_cr_done |= ((val[0] & LANE0_CR_DONE) >> 0);
	lane_cr_done |= ((val[0] & LANE1_CR_DONE) >> 3);
	lane_channel_eq_done |= ((val[0] & LANE0_CHANNEL_EQ_DONE) >> 1);
	lane_channel_eq_done |= ((val[0] & LANE1_CHANNEL_EQ_DONE) >> 4);
	lane_symbol_locked_done |= ((val[0] & LANE0_SYMBOL_LOCKED) >> 2);
	lane_symbol_locked_done |= ((val[0] & LANE1_SYMBOL_LOCKED) >> 5);

	lane_cr_done |= ((val[1] & LANE2_CR_DONE) << 2);
	lane_cr_done |= ((val[1] & LANE3_CR_DONE) >> 1);
	lane_channel_eq_done |= ((val[1] & LANE2_CHANNEL_EQ_DONE) << 1);
	lane_channel_eq_done |= ((val[1] & LANE3_CHANNEL_EQ_DONE) >> 2);
	lane_symbol_locked_done |= ((val[1] & LANE2_SYMBOL_LOCKED) >> 0);
	lane_symbol_locked_done |= ((val[1] & LANE3_SYMBOL_LOCKED) >> 3);

	interlane_align_done |= (val[2] & INTERLANE_ALIGN_DONE);

	if (lane_cnt == 0x04) {
		if (lane_cr_done != 0x0F)
			goto Check_Link_rate;
	} else if (lane_cnt == 0x02) {
		if (lane_cr_done != 0x03)
			goto Check_Link_rate;
	} else {
		if (lane_cr_done != 0x01)
			goto Check_Link_rate;
	}

	displayport_info("lane_cr_done = %x\n", lane_cr_done);
	displayport_info("lane_channel_eq_done = %x\n", lane_channel_eq_done);
	displayport_info("lane_symbol_locked_done = %x\n", lane_symbol_locked_done);
	displayport_info("interlane_align_done = %x\n", interlane_align_done);

	if (lane_cnt == 0x04) {
		if ((lane_channel_eq_done == 0x0F) && (lane_symbol_locked_done == 0x0F)
				&& (interlane_align_done == 1)) {
			displayport_reg_set_training_pattern(NORAMAL_DATA);

			val[0] = 0x00;	/* SCRAMBLING_ENABLE, NORMAL_DATA */
			displayport_reg_dpcd_write(DPCD_ADD_TRANING_PATTERN_SET, 1, val);

			displayport_info("Full Link Training Finish - : %02x %02x\n", link_rate, lane_cnt);
			displayport_info("LANE_SET [%d] : %02x %02x %02x %02x\n",
					eq_training_retry_no, eq_val[0], eq_val[1], eq_val[2], eq_val[3]);
			return ret;
		}
	} else if (lane_cnt == 0x02) {
		if ((lane_channel_eq_done == 0x03) && (lane_symbol_locked_done == 0x03)
				&& (interlane_align_done == 1)) {
			displayport_reg_set_training_pattern(NORAMAL_DATA);

			val[0] = 0x00;	/* SCRAMBLING_ENABLE, NORMAL_DATA */
			displayport_reg_dpcd_write(DPCD_ADD_TRANING_PATTERN_SET, 1, val);

			displayport_info("Full Link Training Finish - : %02x %02x\n", link_rate, lane_cnt);
			displayport_info("LANE_SET [%d] : %02x %02x %02x %02x\n",
					eq_training_retry_no, eq_val[0], eq_val[1], eq_val[2], eq_val[3]);
			return ret;
		}
	} else {
		if ((lane_channel_eq_done == 0x01) && (lane_symbol_locked_done == 0x01)
				&& (interlane_align_done == 1)) {
			displayport_reg_set_training_pattern(NORAMAL_DATA);

			val[0] = 0x00;	/* SCRAMBLING_ENABLE, NORMAL_DATA */
			displayport_reg_dpcd_write(DPCD_ADD_TRANING_PATTERN_SET, 1, val);

			displayport_info("Full Link Training Finish - : %02x %02x\n", link_rate, lane_cnt);
			displayport_info("LANE_SET [%d] : %02x %02x %02x %02x\n",
					eq_training_retry_no, eq_val[0], eq_val[1], eq_val[2], eq_val[3]);
			return ret;
		}
	}

	if (training_retry_no == 4)
		goto Check_Link_rate;

	if (eq_training_retry_no >= 5) {
		val[0] = 0x00;	/* SCRAMBLING_ENABLE, NORMAL_DATA */
		displayport_reg_dpcd_write(DPCD_ADD_TRANING_PATTERN_SET, 1, val);
		displayport_err("Full Link Training Fail : EQ_training Retry -");
		return -EINVAL;
	}

	displayport_reg_dpcd_read_burst(DPCD_ADD_ADJUST_REQUEST_LANE0_1, 2, val);
	voltage_swing_lane[0] = (val[0] & VOLTAGE_SWING_LANE0);
	pre_emphasis_lane[0] = (val[0] & PRE_EMPHASIS_LANE0) >> 2;
	voltage_swing_lane[1] = (val[0] & VOLTAGE_SWING_LANE1) >> 4;
	pre_emphasis_lane[1] = (val[0] & PRE_EMPHASIS_LANE1) >> 6;

	voltage_swing_lane[2] = (val[1] & VOLTAGE_SWING_LANE2);
	pre_emphasis_lane[2] = (val[1] & PRE_EMPHASIS_LANE2) >> 2;
	voltage_swing_lane[3] = (val[1] & VOLTAGE_SWING_LANE3) >> 4;
	pre_emphasis_lane[3] = (val[1] & PRE_EMPHASIS_LANE3) >> 6;

	for (i = 0; i < 4; i++) {
		drive_current[i] = voltage_swing_lane[i];
		pre_emphasis[i] = pre_emphasis_lane[i];

		displayport_dbg("eq drive_current[%d] = %x\n", i, drive_current[i]);
		displayport_dbg("eq pre_emphasis[%d] = %x\n", i, pre_emphasis[i]);
	}

	eq_training_retry_no++;
	goto EQ_Training_Retry;
}

static int displayport_check_dfp_type(void)
{
	u8 val = 0;
	int port_type = 0;
	char *dfp[] = {"Displayport", "VGA", "HDMI", "Others"};

	displayport_reg_dpcd_read(DPCD_ADD_DOWN_STREAM_PORT_PRESENT, 1, &val);
	port_type = (val & BIT_DFP_TYPE) >> 1;
	displayport_info("DFP type: %s(0x%X)\n", dfp[port_type], val);

	return port_type;
}

static void displayport_link_sink_status_read(void)
{
	u8 val[DPCD_BUF_SIZE] = {0, };
	int ret = 0;
	int count = 100;

	ret = displayport_reg_dpcd_read_burst(DPCD_ADD_REVISION_NUMBER, DPCD_BUF_SIZE, val);
	while (ret != 0 && count > 0) {
		displayport_info("%s error %d\n", __func__, count--);
		ret = displayport_reg_dpcd_read_burst(DPCD_ADD_REVISION_NUMBER, DPCD_BUF_SIZE, val);
		msleep(10);
	}
	if (!ret) {
		displayport_info("Read DPCD REV NUM 0_5 %02x %02x %02x %02x %02x %02x\n",
			val[0], val[1], val[2], val[3], val[4], val[5]);
		displayport_info("Read DPCD REV NUM 6_B %02x %02x %02x %02x %02x %02x\n",
			val[6], val[7], val[8], val[9], val[10], val[11]);
	} else
		displayport_info("%s error\n", __func__);
}

static int displayport_read_branch_revision(struct displayport_device *displayport)
{
	int ret = 0;
	u8 val[4] = {0, };

	ret = displayport_reg_dpcd_read_burst(DPCD_BRANCH_HW_REVISION, 3, val);
	if (!ret) {
		displayport_info("Branch revision: HW(0x%X), SW(0x%X, 0x%X)\n",
			val[0], val[1], val[2]);
		displayport->dex_ver[0] = val[1];
		displayport->dex_ver[1] = val[2];
	}

	return ret;
}

static int displayport_link_status_read(u32 sst_id)
{
	u8 val[DPCP_LINK_SINK_STATUS_FIELD_LENGTH] = {0, };
	int count = 200;

	/* for Link CTS : Branch Device Detection*/
	displayport_link_sink_status_read();
	do {
		displayport_reg_dpcd_read(DPCD_ADD_SINK_COUNT, 1, val);
		if ((val[0] & (SINK_COUNT1 | SINK_COUNT2)) != 0)
			break;
		msleep(20);
	} while (--count > 0);

	displayport_reg_dpcd_read_burst(DPCD_ADD_DEVICE_SERVICE_IRQ_VECTOR,
			DPCP_LINK_SINK_STATUS_FIELD_LENGTH - 1, &val[1]);

	displayport_info("SST(%d) Read link status %02x %02x %02x %02x %02x %02x\n",
				sst_id, val[0], val[1], val[2], val[3], val[4], val[5]);

	if ((val[0] & val[1] & val[2] & val[3] & val[4] & val[5]) == 0xff)
		return -EINVAL;

	if (count == 0)
		return -EINVAL;

	if (count < 10 && count > 0)
		usleep_range(10000, 11000); /* need delay after SINK count is changed to 1 */

	if (val[1] == AUTOMATED_TEST_REQUEST) {
		u8 data = 0;

		displayport_reg_dpcd_read(DPCD_TEST_REQUEST, 1, &data);

		if ((data & TEST_EDID_READ) == TEST_EDID_READ) {
			val[0] = edid_read_checksum();
			displayport_info("TEST_EDID_CHECKSUM %02x\n", val[0]);

			displayport_reg_dpcd_write(DPCD_TEST_EDID_CHECKSUM, 1, val);

			val[0] = 0x04; /*TEST_EDID_CHECKSUM_WRITE*/
			displayport_reg_dpcd_write(DPCD_TEST_RESPONSE, 1, val);

		}
	}

	return 0;
}

static void displayport_find_proper_ratio_video_for_dex(struct displayport_device *displayport)
{
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);
	int i = displayport->sst[sst_id]->best_video;
	enum video_ratio_t best_ratio = supported_videos[i].ratio;

	displayport->dex_video_pick = 0;

	if (!displayport->dex_setting)
		return;

	if (best_ratio == RATIO_16_9 ||
			best_ratio == RATIO_16_10 ||
			best_ratio == RATIO_21_9) {
		displayport_dbg("dex support ratio\n");
	} else {
		displayport_info("not dex support ratio\n");
		return;
	}

	/* for test */
	if (reduced_resolution) {
		i = reduced_resolution;
	}

	/* find same ratio timing from best timing */
	for (;i >= V1600X900P59; i--) {
		if (supported_videos[i].edid_support_match &&
				supported_videos[i].dex_support &&
				supported_videos[i].ratio == best_ratio) {
			displayport->dex_video_pick = i;
			displayport_info("found dex support ratio: %s %d/%d\n",
					supported_videos[i].name, i, best_ratio);
			break;
		}
	}
	if (!displayport->dex_video_pick)
		displayport_info("not found dex support ratio\n");
}

static int displayport_link_training(u32 sst_id)
{
	u8 val;
	struct displayport_device *displayport = get_displayport_drvdata();
	int ret = 0;

	if (!displayport->hpd_current_state) {
		displayport_info("hpd is low in link training\n");
		return 0;
	}

	mutex_lock(&displayport->training_lock);

	if (displayport->mst_cap == 0) {
		ret = edid_update(sst_id, displayport);
		if (ret < 0)
			displayport_err("failed to update edid\n");

		/* find proper ratio resolution for DEX */
		displayport_find_proper_ratio_video_for_dex(displayport);
	}

	displayport_reg_dpcd_read(DPCD_ADD_MAX_DOWNSPREAD, 1, &val);
	displayport_dbg("DPCD_ADD_MAX_DOWNSPREAD = %x\n", val);

	ret = displayport_full_link_training(sst_id);

	mutex_unlock(&displayport->training_lock);

	return ret;
}

static void displayport_set_extcon_state(u32 sst_id,
		struct displayport_device *displayport, int state)
{
#if defined(CONFIG_EXTCON)
	u32 extcon_dp_id = EXTCON_DISP_DP;

	switch (sst_id) {
	case SST1:
		extcon_dp_id = EXTCON_DISP_DP;
		break;
	case SST2:
		extcon_dp_id = EXTCON_DISP_DP2;
		break;
	default:
		extcon_dp_id = EXTCON_DISP_DP;
	}

	if (state)
		extcon_set_state_sync(displayport->extcon_displayport, extcon_dp_id, 1);
	else
		extcon_set_state_sync(displayport->extcon_displayport, extcon_dp_id, 0);
#else
	displayport_info("Not compiled EXTCON driver\n");
#endif
	displayport_info("SST%d HPD status = %d\n", sst_id + 1, state);
}

static int displayport_get_extcon_state(u32 sst_id,
		struct displayport_device *displayport)
{
#if defined(CONFIG_EXTCON)
	u32 extcon_dp_id = EXTCON_DISP_DP;

	switch (sst_id) {
	case SST1:
		extcon_dp_id = EXTCON_DISP_DP;
		break;
	case SST2:
		extcon_dp_id = EXTCON_DISP_DP2;
		break;
	default:
		extcon_dp_id = EXTCON_DISP_DP;
	}

	return extcon_get_state(displayport->extcon_displayport, extcon_dp_id);
#else
	displayport_info("Not compiled EXTCON driver\n");

	return 0;
#endif
}

int displayport_check_mst(void)
{
	u8 val = 0;
	int mst_cap = 0;
	int ret;
	int count = 100;

	ret = displayport_reg_dpcd_read(DPCD_ADD_REVISION_NUMBER, 1, &val);
	while (ret != 0 && count > 0) {
		displayport_info("%s error %d\n", __func__, count--);
		ret = displayport_reg_dpcd_read(DPCD_ADD_REVISION_NUMBER, 1, &val);
		msleep(10);
	}
	displayport_info("DPCD_ADD_REVISION_NUMBER = %x\n", val);

	if (val >= DPCD_VER_1_2) {
		displayport_reg_dpcd_read(DPCD_ADD_MSTM_CAP, 1, &val);
		displayport_info("DPCD_ADD_MSTM_CAP = %x\n", val);

		if (val & MST_CAP)
			mst_cap = 1;
	}

	return mst_cap;
}

void displayport_mst_enable(void)
{
	u8 val = 0;

	val = DPCD_MST_EN | UP_REQ_EN | UPSTREAM_IS_SRC;
	displayport_reg_dpcd_write(DPCD_ADD_MSTM_CTRL, 1, &val);

	displayport_reg_set_mst_en(1);
}

int displayport_wait_state_change(u32 sst_id,
		struct displayport_device *displayport,	int max_wait_time,
		enum displayport_state state)
{
	int ret = 0;
	int wait_cnt = max_wait_time;

	displayport_info("SST%d wait_state_change start\n", sst_id + 1);
	displayport_info("max_wait_time = %dms, state = %d\n", max_wait_time, state);

	do {
		wait_cnt--;
		usleep_range(1000, 1030);
	} while ((state != displayport->sst[sst_id]->state) && (wait_cnt > 0));

	displayport_info("wait_state_change time = %dms, state = %d\n",
			max_wait_time - wait_cnt, state);

	if (wait_cnt <= 0)
		displayport_err("SST%d wait state timeout\n", sst_id + 1);

	ret = wait_cnt;

	displayport_info("SST%d wait_state_change end\n", sst_id + 1);

	return ret;
}

int displayport_wait_audio_off_change(u32 sst_id,
		struct displayport_device *displayport,	int max_wait_time)
{
	int ret = 0;
	int wait_cnt = max_wait_time;

	displayport_info("SST%d wait_audio_off_change start\n", sst_id + 1);
	displayport_info("max_wait_time = %dms\n", max_wait_time);

	do {
		wait_cnt--;
		usleep_range(1000, 1030);
	} while ((displayport->sst[sst_id]->audio_state != 0) && (wait_cnt > 0));

	displayport_info("wait_audio_off time = %dms\n", max_wait_time - wait_cnt);

	if (wait_cnt <= 0)
		displayport_err("SST%d wait audio off timeout\n", sst_id + 1);

	ret = wait_cnt;

	displayport_info("SST%d wait_audio_off_change end\n", sst_id + 1);

	return ret;
}

int displayport_wait_decon_run(u32 sst_id,
		struct displayport_device *displayport,	int max_wait_time)
{
	int ret = 0;
	int wait_cnt = max_wait_time;

	displayport_info("SST%d wait_decon_run start\n", sst_id + 1);
	displayport_info("max_wait_time = %dms\n", max_wait_time);

	do {
		wait_cnt--;
		usleep_range(1000, 1030);
	} while ((displayport->sst[sst_id]->decon_run != 0) && (wait_cnt > 0));

	displayport_info("wait_decon_run time = %dms\n", max_wait_time - wait_cnt);

	if (wait_cnt <= 0)
		displayport_err("SST%d wait_decon_run timeout\n", sst_id + 1);

	ret = wait_cnt;

	displayport_info("SST%d wait_decon_run end\n", sst_id + 1);

	return ret;
}

void displayport_on_by_hpd_high(u32 sst_id, struct displayport_device *displayport)
{
	int timeout = 0;

	if (displayport->sst[sst_id]->hpd_state) {
		displayport->sst[sst_id]->bpc = BPC_8;	/* default setting */
		displayport->sst[sst_id]->dyn_range = VESA_RANGE;
		displayport->sst[sst_id]->state = DISPLAYPORT_STATE_INIT;

		displayport_audio_init_config(sst_id);

		if (displayport->sst[sst_id]->bist_used) {
			displayport->cur_sst_id = sst_id;
			displayport_enable(displayport); /* for bist video enable */
		} else {
			if (displayport_get_extcon_state(sst_id, displayport) == 0) {
				if (displayport->sst[sst_id]->bist_used == 0) {
					displayport_set_extcon_state(sst_id, displayport, 1);

					timeout = displayport_wait_state_change(sst_id,
									displayport, 3000, DISPLAYPORT_STATE_ON);
				}
			}
		}
	}

#if defined(CONFIG_SND_SOC_SAMSUNG_DISPLAYPORT)
	displayport_wait_decon_run(sst_id, displayport, 3000);
	dp_ado_switch_set_state(edid_audio_informs());
#endif
}

void displayport_off_by_hpd_low(u32 sst_id, struct displayport_device *displayport)
{
	int timeout = 0;
	struct decon_device *decon;

#if defined(CONFIG_SND_SOC_SAMSUNG_DISPLAYPORT)
	dp_ado_switch_set_state(-1);
	displayport_info("audio info = -1\n");
	displayport_wait_audio_off_change(sst_id, displayport, 5000);
#endif

	if (displayport->sst[sst_id]->state == DISPLAYPORT_STATE_ON) {
		displayport->sst[sst_id]->hpd_state = HPD_UNPLUG;
		displayport->sst[sst_id]->cur_video = V640X480P60;
		displayport->sst[sst_id]->best_video = V640X480P60;
		displayport->sst[sst_id]->bpc = BPC_8;
		displayport->sst[sst_id]->dyn_range = VESA_RANGE;

		if (displayport->sst[sst_id]->bist_used == 0) {
			displayport_set_extcon_state(sst_id, displayport, 0);

			timeout = displayport_wait_state_change(sst_id,
							displayport, 3000, DISPLAYPORT_STATE_OFF);

			if (timeout <= 0) {
				displayport_err("disable timeout\n");

				decon = get_decon_drvdata(displayport_get_decon_id(sst_id));

				if (displayport->sst[sst_id]->state == DISPLAYPORT_STATE_INIT) {
					displayport_info("SST%d not enabled\n", sst_id + 1);
				} else if (decon->state == DECON_STATE_OFF &&
						displayport->sst[sst_id]->state == DISPLAYPORT_STATE_ON) {
					displayport_err("abnormal state: decon%d:%d, displayport SST%d:%d\n",
							decon->id, decon->state, sst_id + 1,
							displayport->sst[sst_id]->state);
					displayport_disable(displayport);
				}
			}
		} else {
			displayport->cur_sst_id = sst_id;
			displayport_disable(displayport); /* for bist video disable */
		}
	}
}

void displayport_hpd_changed(int state)
{
	int ret = 0;
	struct displayport_device *displayport = get_displayport_drvdata();
	int i = 0;
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	mutex_lock(&displayport->hpd_lock);
	if (displayport->hpd_current_state == state) {
		displayport_info("hpd same state skip %x\n", state);
		mutex_unlock(&displayport->hpd_lock);
		return;
	}

	displayport_info("displayport hpd changed %d\n", state);
	displayport->hpd_current_state = state;

	if (state) {
		pm_runtime_get_sync(displayport->dev);
		pm_stay_awake(displayport->dev);

		/* PHY power on */
		displayport_reg_sw_reset();
		displayport_reg_init(); /* for AUX ch read/write. */
		displayport_hdcp22_notify_state(DP_CONNECT);

		displayport->auto_test_mode = 0;
		displayport->sst[sst_id]->bist_used = 0;
		displayport->sst[sst_id]->best_video = EDID_DEFAULT_TIMINGS_IDX;
		displayport->mst_mode = 0;

		usleep_range(10000, 11000);

		displayport->mst_cap = displayport_check_mst();

		if (displayport->mst_cap) {
			displayport->mst_mode = 1;
			displayport_mst_enable();
			displayport_topology_clr_vc();
		} else
			displayport->sst[sst_id]->hpd_state = HPD_PLUG; /* default */

		/* for Link CTS : (4.2.2.3) EDID Read */
		if (displayport_link_status_read(sst_id)) {
			displayport_err("link_status_read fail\n");
			goto HPD_FAIL;
		}

		if (displayport_read_branch_revision(displayport))
			displayport_err("branch_revision_read fail\n");

		displayport_info("link training in hpd_changed\n");
		ret = displayport_link_training(sst_id);
		if (ret < 0) {
			displayport_dbg("link training fail\n");
			goto HPD_FAIL;
		}

		displayport->dfp_type = displayport_check_dfp_type();
		/* Enable it! if you want to prevent output according to type
		 * if (displayport->dfp_type != DFP_TYPE_DP &&
		 *		displayport->dfp_type != DFP_TYPE_HDMI) {
		 *	displayport_err("not supported DFT type\n");
		 *	goto HPD_FAIL;
		 * }
		 */

		if (displayport->mst_cap == 0) {
			displayport_info("displayport_on_by_hpd_high in hpd changed\n");
			displayport_on_by_hpd_high(sst_id, displayport);
		} else
			displayport_topology_make();
	} else {
		displayport_reg_print_audio_state(sst_id);
#if defined(CONFIG_EXYNOS_HDCP2)
		if (displayport->hdcp_ver == HDCP_VERSION_2_2) {
			hdcp_dplink_cancel_auth();
		}
#endif
		cancel_delayed_work_sync(&displayport->hpd_plug_work);
		cancel_delayed_work_sync(&displayport->hpd_unplug_work);
		cancel_delayed_work_sync(&displayport->hpd_irq_work);
		cancel_delayed_work_sync(&displayport->hdcp13_work);
		cancel_delayed_work_sync(&displayport->hdcp22_work);
		cancel_delayed_work_sync(&displayport->hdcp13_integrity_check_work);

		for (i = SST1; i < MAX_SST_CNT; i++)
			displayport_off_by_hpd_low(i, displayport);

		displayport_reg_deinit();
		displayport_reg_phy_disable();
		pm_runtime_put_sync(displayport->dev);
#if defined(CONFIG_CPU_IDLE)
		/* unblock to enter SICD mode */
		exynos_update_ip_idle_status(displayport->idle_ip_index, 1);
#endif
		pm_relax(displayport->dev);

		displayport->hdcp_ver = 0;
	}

	mutex_unlock(&displayport->hpd_lock);

	return;

HPD_FAIL:
	displayport_reg_deinit();
	displayport_reg_phy_disable();
	pm_relax(displayport->dev);
	displayport->hpd_current_state = HPD_UNPLUG;

	for (i = SST1; i < MAX_SST_CNT; i++) {
		displayport->sst[i]->state = DISPLAYPORT_STATE_INIT;
		displayport->sst[i]->hpd_state = HPD_UNPLUG;
		displayport->sst[i]->cur_video = V640X480P60;
		displayport->sst[i]->bpc = BPC_8;
		displayport->sst[i]->dyn_range = VESA_RANGE;
	}

	pm_runtime_put_sync(displayport->dev);
	mutex_unlock(&displayport->hpd_lock);

	return;
}

void displayport_set_reconnection(void)
{
	int ret;
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	/* PHY power on */
	displayport_reg_init(); /* for AUX ch read/write. */

	displayport_info("link training in reconnection\n");
	ret = displayport_link_training(sst_id);
	if (ret < 0) {
		displayport_dbg("link training fail\n");
		return;
	}
}

static void displayport_hpd_plug_work(struct work_struct *work)
{
	int i = 0;
	struct displayport_device *displayport = get_displayport_drvdata();

	for (i = SST1; i < MAX_SST_CNT; i++) {
		if (displayport->sst[i]->hpd_state == HPD_PLUG_WORK) {
			displayport_info("hpd_plug_work\n");
			displayport->sst[i]->hpd_state = HPD_PLUG;
			edid_update(i, displayport);
			displayport_topology_make();
			displayport_on_by_hpd_high(i, displayport);
		}
	}
}

static void displayport_hpd_unplug_work(struct work_struct *work)
{
	int i = 0;
	struct displayport_device *displayport = get_displayport_drvdata();

	for (i = SST1; i < MAX_SST_CNT; i++) {
		if (displayport->sst[i]->hpd_state == HPD_UNPLUG_WORK) {
			displayport_info("hpd_unplug_work\n");
			displayport_topology_delete_vc(i);
			displayport->sst[i]->hpd_state = HPD_UNPLUG;
			displayport_off_by_hpd_low(i, displayport);
		}
	}
}

static int displayport_check_dpcd_lane_status(u8 lane0_1_status,
		u8 lane2_3_status, u8 lane_align_status)
{
	u8 val[2] = {0,};
	u32 link_rate = displayport_reg_phy_get_link_bw();
	u32 lane_cnt = displayport_reg_get_lane_count();

	displayport_reg_dpcd_read(DPCD_ADD_LINK_BW_SET, 2, val);

	displayport_info("check lane %02x %02x %02x %02x\n", link_rate, lane_cnt,
			val[0], (val[1] & MAX_LANE_COUNT));

	if ((link_rate != val[0]) || (lane_cnt != (val[1] & MAX_LANE_COUNT))) {
		displayport_err("%s() link rate, lane_cnt error\n", __func__);
		return -EINVAL;
	}

	if ((lane_align_status & INTERLANE_ALIGN_DONE) != INTERLANE_ALIGN_DONE) {
		displayport_err("%s() interlane align error\n", __func__);
		return -EINVAL;
	}

	if (lane_cnt >= 1) {
		if ((lane0_1_status & (LANE0_CR_DONE | LANE0_CHANNEL_EQ_DONE | LANE0_SYMBOL_LOCKED))
				!= (LANE0_CR_DONE | LANE0_CHANNEL_EQ_DONE | LANE0_SYMBOL_LOCKED)) {
			displayport_err("%s() lane0 status error\n", __func__);
			return -EINVAL;
		}
	}

	if (lane_cnt >= 2) {
		if ((lane0_1_status & (LANE1_CR_DONE | LANE1_CHANNEL_EQ_DONE | LANE1_SYMBOL_LOCKED))
				!= (LANE1_CR_DONE | LANE1_CHANNEL_EQ_DONE | LANE1_SYMBOL_LOCKED)) {
			displayport_err("%s() lane1 status error\n", __func__);
			return -EINVAL;
		}
	}

	if (lane_cnt == 4) {
		if ((lane2_3_status & (LANE2_CR_DONE | LANE2_CHANNEL_EQ_DONE | LANE2_SYMBOL_LOCKED))
				!= (LANE2_CR_DONE | LANE2_CHANNEL_EQ_DONE | LANE2_SYMBOL_LOCKED)) {
			displayport_err("%s() lane2 stat error\n", __func__);
			return -EINVAL;
		}

		if ((lane2_3_status & (LANE3_CR_DONE | LANE3_CHANNEL_EQ_DONE | LANE3_SYMBOL_LOCKED))
				!= (LANE3_CR_DONE | LANE3_CHANNEL_EQ_DONE | LANE3_SYMBOL_LOCKED)) {
			displayport_err("%s() lane3 status error\n", __func__);
			return -EINVAL;
		}
	}

	return 0;
}

static int displayport_automated_test_set_lane_req(u8 *val)
{
	u8 drive_current[MAX_LANE_CNT];
	u8 pre_emphasis[MAX_LANE_CNT];
	u8 voltage_swing_lane[MAX_LANE_CNT];
	u8 pre_emphasis_lane[MAX_LANE_CNT];
	u8 max_reach_value[MAX_LANE_CNT];
	u8 val2[MAX_LANE_CNT];
	int i;

	voltage_swing_lane[0] = (val[0] & VOLTAGE_SWING_LANE0);
	pre_emphasis_lane[0] = (val[0] & PRE_EMPHASIS_LANE0) >> 2;
	voltage_swing_lane[1] = (val[0] & VOLTAGE_SWING_LANE1) >> 4;
	pre_emphasis_lane[1] = (val[0] & PRE_EMPHASIS_LANE1) >> 6;

	voltage_swing_lane[2] = (val[1] & VOLTAGE_SWING_LANE2);
	pre_emphasis_lane[2] = (val[1] & PRE_EMPHASIS_LANE2) >> 2;
	voltage_swing_lane[3] = (val[1] & VOLTAGE_SWING_LANE3) >> 4;
	pre_emphasis_lane[3] = (val[1] & PRE_EMPHASIS_LANE3) >> 6;

	for (i = 0; i < 4; i++) {
		drive_current[i] = voltage_swing_lane[i];
		pre_emphasis[i] = pre_emphasis_lane[i];

		displayport_info("AutoTest: swing[%d] = %x\n", i, drive_current[i]);
		displayport_info("AutoTest: pre_emphasis[%d] = %x\n", i, pre_emphasis[i]);
	}
	displayport_reg_set_voltage_and_pre_emphasis((u8 *)drive_current, (u8 *)pre_emphasis);
	displayport_get_voltage_and_pre_emphasis_max_reach((u8 *)drive_current,
			(u8 *)pre_emphasis, (u8 *)max_reach_value);

	val2[0] = (pre_emphasis[0]<<3) | drive_current[0] | max_reach_value[0];
	val2[1] = (pre_emphasis[1]<<3) | drive_current[1] | max_reach_value[1];
	val2[2] = (pre_emphasis[2]<<3) | drive_current[2] | max_reach_value[2];
	val2[3] = (pre_emphasis[3]<<3) | drive_current[3] | max_reach_value[3];
	displayport_info("AutoTest: set %02x %02x %02x %02x\n", val2[0], val2[1], val2[2], val2[3]);
	displayport_reg_dpcd_write_burst(DPCD_ADD_TRANING_LANE0_SET, 4, val2);

	return 0;
}

static void displayport_automated_test_pattern_set(u32 sst_id, struct displayport_device *displayport,
			u8 dpcd_test_pattern_type)
{
	switch (dpcd_test_pattern_type) {
	case DPCD_TEST_PATTERN_COLOR_RAMPS:
		displayport->sst[sst_id]->bist_type = CTS_COLOR_RAMP;
		break;
	case DPCD_TEST_PATTERN_BW_VERTICAL_LINES:
		displayport->sst[sst_id]->bist_type = CTS_BLACK_WHITE;
		break;
	case DPCD_TEST_PATTERN_COLOR_SQUARE:
		if (displayport->sst[sst_id]->dyn_range == CEA_RANGE)
			displayport->sst[sst_id]->bist_type = CTS_COLOR_SQUARE_CEA;
		else
			displayport->sst[sst_id]->bist_type = CTS_COLOR_SQUARE_VESA;
		break;
	default:
		displayport->sst[sst_id]->bist_type = COLOR_BAR;
		break;
	}
}

static int displayport_Automated_Test_Request(void)
{
	u8 data = 0;
	u8 val[DPCP_LINK_SINK_STATUS_FIELD_LENGTH] = {0, };
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);
	u8 dpcd_test_pattern_type = 0;

	displayport_reg_dpcd_read(DPCD_TEST_REQUEST, 1, &data);
	displayport_info("TEST_REQUEST %02x\n", data);

	displayport->auto_test_mode = 1;
	val[0] = 0x01; /*TEST_ACK*/
	displayport_reg_dpcd_write(DPCD_TEST_RESPONSE, 1, val);

	if ((data & TEST_LINK_TRAINING) == TEST_LINK_TRAINING) {
		videoformat cur_video = displayport->sst[sst_id]->cur_video;
		u8 bpc = (u8)displayport->sst[sst_id]->bpc;
		u8 bist_type = (u8)displayport->sst[sst_id]->bist_type;
		u8 dyn_range = (u8)displayport->sst[sst_id]->dyn_range;

		/* PHY power on */
		displayport_reg_init();

		displayport_reg_dpcd_read(DPCD_TEST_LINK_RATE, 1, val);
		displayport_info("TEST_LINK_RATE %02x\n", val[0]);
		g_displayport_debug_param.link_rate = (val[0]&TEST_LINK_RATE);

		displayport_reg_dpcd_read(DPCD_TEST_LANE_COUNT, 1, val);
		displayport_info("TEST_LANE_COUNT %02x\n", val[0]);
		g_displayport_debug_param.lane_cnt = (val[0]&TEST_LANE_COUNT);

		g_displayport_debug_param.param_used = 1;
		displayport_link_training(sst_id);
		g_displayport_debug_param.param_used = 0;

		displayport->sst[sst_id]->bist_used = 1;
		displayport->sst[sst_id]->bist_type = COLOR_BAR;
		displayport_reg_set_bist_video_configuration(sst_id,
				cur_video, bpc, bist_type, dyn_range);
		displayport_reg_start(sst_id);
	} else if ((data & TEST_VIDEO_PATTERN) == TEST_VIDEO_PATTERN) {

		if (displayport->sst[sst_id]->state == DISPLAYPORT_STATE_OFF) {
			pm_runtime_get_sync(displayport->dev);

			/* PHY power on */
			displayport_reg_init(); /* for AUX ch read/write. */

			g_displayport_debug_param.param_used = 1;
			displayport_link_training(sst_id);
			g_displayport_debug_param.param_used = 0;
		}
		displayport_reg_dpcd_read(DPCD_TEST_PATTERN, 1, val);
		displayport_info("TEST_PATTERN %02x\n", val[0]);
		dpcd_test_pattern_type = val[0];

		displayport_automated_test_pattern_set(sst_id, displayport, dpcd_test_pattern_type);

		displayport->sst[sst_id]->cur_video = displayport->sst[sst_id]->best_video;

		displayport_reg_set_bist_video_configuration(sst_id, displayport->sst[sst_id]->cur_video,
				displayport->sst[sst_id]->bpc, CTS_COLOR_RAMP, displayport->sst[sst_id]->dyn_range);
		displayport_reg_start(sst_id);

	} else if ((data & TEST_PHY_TEST_PATTERN) == TEST_PHY_TEST_PATTERN) {
		displayport_reg_stop(sst_id);
		msleep(120);
		displayport_reg_dpcd_read(DPCD_ADD_ADJUST_REQUEST_LANE0_1, 2, val);
		displayport_info("ADJUST_REQUEST_LANE0_1 %02x %02x\n", val[0], val[1]);

		/*set swing, preemp*/
		displayport_automated_test_set_lane_req(val);

		displayport_reg_dpcd_read(DCDP_ADD_PHY_TEST_PATTERN, 4, val);
		displayport_info("PHY_TEST_PATTERN %02x %02x %02x %02x\n", val[0], val[1], val[2], val[3]);

		switch (val[0]) {
		case DISABLE_PATTEN:
			displayport_reg_set_qual_pattern(DISABLE_PATTEN, ENABLE_SCRAM);
			break;
		case D10_2_PATTERN:
			displayport_reg_set_qual_pattern(D10_2_PATTERN, DISABLE_SCRAM);
			break;
		case SERP_PATTERN:
			displayport_reg_set_qual_pattern(SERP_PATTERN, ENABLE_SCRAM);
			break;
		case PRBS7:
			displayport_reg_set_qual_pattern(PRBS7, DISABLE_SCRAM);
			break;
		case CUSTOM_80BIT:
			displayport_reg_set_pattern_PLTPAT();
			displayport_reg_set_qual_pattern(CUSTOM_80BIT, DISABLE_SCRAM);
			break;
		case HBR2_COMPLIANCE:
			/*option 0*/
			/*displayport_reg_set_hbr2_scrambler_reset(252);*/

			/*option 1*/
			displayport_reg_set_hbr2_scrambler_reset(252*2);

			/*option 2*/
			/*displayport_reg_set_hbr2_scrambler_reset(252);*/
			/*displayport_reg_set_PN_Inverse_PHY_Lane(1);*/

			/*option 3*/
			/*displayport_reg_set_hbr2_scrambler_reset(252*2);*/
			/*displayport_reg_set_PN_Inverse_PHY_Lane(1);*/

			displayport_reg_set_qual_pattern(HBR2_COMPLIANCE, ENABLE_SCRAM);
			break;
		default:
			displayport_err("not supported link qual pattern");
			break;
		}
	} else if ((data & TEST_AUDIO_PATTERN) == TEST_AUDIO_PATTERN) {
		struct displayport_audio_config_data audio_config_data;

		displayport_reg_dpcd_read(DPCD_TEST_AUDIO_MODE, 1, val);
		displayport_info("TEST_AUDIO_MODE %02x %02x\n",
				(val[0] & TEST_AUDIO_SAMPLING_RATE),
				(val[0] & TEST_AUDIO_CHANNEL_COUNT) >> 4);

		displayport_reg_dpcd_read(DPCD_TEST_AUDIO_PATTERN_TYPE, 1, val);
		displayport_info("TEST_AUDIO_PATTERN_TYPE %02x\n", val[0]);

		msleep(300);

		audio_config_data.audio_enable = 1;
		audio_config_data.audio_fs =  (val[0] & TEST_AUDIO_SAMPLING_RATE);
		audio_config_data.audio_channel_cnt = (val[0] & TEST_AUDIO_CHANNEL_COUNT) >> 4;
		audio_config_data.audio_channel_cnt++;
		displayport_audio_bist_config(sst_id, audio_config_data);
	} else {
		displayport_err("Not Supported AUTOMATED_TEST_REQUEST\n");
		return -EINVAL;
	}
	return 0;
}

static void displayport_hpd_irq_work(struct work_struct *work)
{
	u8 val[DPCP_LINK_SINK_STATUS_FIELD_LENGTH] = {0, };
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	if (!displayport->hpd_current_state) {
		displayport_info("HPD IRQ work: hpd is low\n");
		return;
	}

	displayport_dbg("detect HPD_IRQ\n");

	if (displayport->hdcp_ver == HDCP_VERSION_2_2) {
		int ret = displayport_reg_dpcd_read_burst(DPCD_ADD_SINK_COUNT,
				DPCP_LINK_SINK_STATUS_FIELD_LENGTH, val);

		displayport_info("HPD IRQ work2 %02x %02x %02x %02x %02x %02x\n",
				val[0], val[1], val[2], val[3], val[4], val[5]);
		if (ret < 0 || (val[0] & val[1] & val[2] & val[3] & val[4] & val[5]) == 0xff) {
			displayport_info("dpcd_read error in HPD IRQ work\n");
			return;
		}

		if ((val[1] & CP_IRQ) == CP_IRQ) {
			displayport_info("hdcp22: detect CP_IRQ\n");
			ret = displayport_hdcp22_irq_handler();
			if (ret == 0)
				return;
		} else {
			displayport_info("hdcp22: detect hpd_irq!!!!\n");

			if ((val[1] & AUTOMATED_TEST_REQUEST) == AUTOMATED_TEST_REQUEST) {
				if (displayport_Automated_Test_Request() == 0)
					return;
			}

			if (displayport_check_dpcd_lane_status(val[2], val[3], val[4]) != 0) {
				displayport_info("link training in HPD IRQ work2\n");

#if defined(CONFIG_EXYNOS_HDCP2)
				hdcp_dplink_set_reauth();
#endif
				displayport_hdcp22_enable(0);

				displayport_link_training(sst_id);
				queue_delayed_work(displayport->hdcp2_wq,
						&displayport->hdcp22_work, msecs_to_jiffies(2000));
			}

			if ((val[1] & UP_REQ_MSG_RDY) == UP_REQ_MSG_RDY) {
				displayport_info("Detect UP_REQ_MSG_RDY IRQ\n");
				displayport_msg_rx(UP_REQ);
			}
		}
		return;
	}

	if (hdcp13_info.auth_state != HDCP13_STATE_AUTH_PROCESS) {
		int ret = displayport_reg_dpcd_read_burst(DPCD_ADD_SINK_COUNT,
				DPCP_LINK_SINK_STATUS_FIELD_LENGTH, val);

		displayport_info("HPD IRQ work1 %02x %02x %02x %02x %02x %02x\n",
				val[0], val[1], val[2], val[3], val[4], val[5]);
		if (ret < 0 || (val[0] & val[1] & val[2] & val[3] & val[4] & val[5]) == 0xff) {
			displayport_info("dpcd_read error in HPD IRQ work\n");
			return;
		}

		if ((val[1] & CP_IRQ) == CP_IRQ && displayport->hdcp_ver == HDCP_VERSION_1_3) {
			displayport_info("detect CP_IRQ\n");
			hdcp13_info.cp_irq_flag = 1;
			hdcp13_info.link_check = LINK_CHECK_NEED;
			hdcp13_link_integrity_check();

			if (hdcp13_info.auth_state == HDCP13_STATE_FAIL)
				queue_delayed_work(displayport->dp_wq,
						&displayport->hdcp13_work, msecs_to_jiffies(2000));
		}

		if ((val[1] & AUTOMATED_TEST_REQUEST) == AUTOMATED_TEST_REQUEST) {
			if (displayport_Automated_Test_Request() == 0)
				return;
		}

		if (!displayport->hpd_current_state) {
			displayport_info("HPD IRQ work: hpd is low\n");
			return;
		}

		if (displayport_check_dpcd_lane_status(val[2], val[3], val[4]) != 0) {
			displayport_info("link training in HPD IRQ work1\n");
			displayport_link_training(sst_id);

			hdcp13_info.auth_state = HDCP13_STATE_NOT_AUTHENTICATED;
			queue_delayed_work(displayport->dp_wq,
					&displayport->hdcp13_work, msecs_to_jiffies(2000));
		}

		if ((val[1] & UP_REQ_MSG_RDY) == UP_REQ_MSG_RDY) {
			displayport_info("Detect UP_REQ_MSG_RDY IRQ\n");
			displayport_msg_rx(UP_REQ);
		}
	} else {
		displayport_reg_dpcd_read(DPCD_ADD_DEVICE_SERVICE_IRQ_VECTOR, 1, val);

		if ((val[0] & CP_IRQ) == CP_IRQ) {
			displayport_info("detect CP_IRQ\n");
			hdcp13_info.cp_irq_flag = 1;
			displayport_reg_dpcd_read(ADDR_HDCP13_BSTATUS, 1, HDCP13_DPCD.HDCP13_BSTATUS);
		}

		if ((val[1] & UP_REQ_MSG_RDY) == UP_REQ_MSG_RDY) {
			displayport_info("Detect UP_REQ_MSG_RDY IRQ\n");
			displayport_msg_rx(UP_REQ);
		}
	}
}

static void displayport_hdcp13_integrity_check_work(struct work_struct *work)
{
	struct displayport_device *displayport = get_displayport_drvdata();

	if (displayport->hdcp_ver == HDCP_VERSION_1_3) {
		hdcp13_info.link_check = LINK_CHECK_NEED;
		hdcp13_link_integrity_check();
	}
}

static irqreturn_t displayport_irq_handler(int irq, void *dev_data)
{
	struct displayport_device *displayport = dev_data;
	struct decon_device *decon = NULL;
	int active;
	u32 irq_status_reg;
	int i = 0;

	spin_lock(&displayport->slock);

	active = pm_runtime_active(displayport->dev);
	if (!active) {
		displayport_info("displayport power(%d)\n", active);
		spin_unlock(&displayport->slock);
		return IRQ_HANDLED;
	}

	/* Common interrupt */
	irq_status_reg = displayport_reg_get_common_interrupt_and_clear();

#if !defined(CONFIG_USB_TYPEC_MANAGER_NOTIFIER)
	if (irq_status_reg & HPD_CHG)
		displayport_info("HPD_CHG detect\n");

	if (irq_status_reg & HPD_LOST) {
		displayport_info("HPD_LOST detect\n");
	}

	if (irq_status_reg & HPD_PLUG_INT) {
		displayport_info("HPD_PLUG detect\n");
	}

	if (irq_status_reg & HPD_IRQ_FLAG) {
		displayport_info("HPD IRQ detect\n");
	}
#endif

	if (irq_status_reg & HDCP_LINK_CHK_FAIL) {
		queue_delayed_work(displayport->dp_wq, &displayport->hdcp13_integrity_check_work, 0);
		displayport_info("HDCP_LINK_CHK detect\n");
	}

	if (irq_status_reg & HDCP_R0_CHECK_FLAG) {
		hdcp13_info.r0_read_flag = 1;
		displayport_info("R0_CHECK_FLAG detect\n");
	}

	for (i = SST1; i < MAX_SST_CNT; i++) {
		irq_status_reg = displayport_reg_get_sst_video_interrupt_and_clear(i);

		if ((irq_status_reg & MAPI_FIFO_UNDER_FLOW)
				&& displayport->sst[i]->decon_run == 1)
			displayport_info("SST%d VIDEO FIFO_UNDER_FLOW detect\n", i + 1);

		if (displayport->sst[i]->bist_used == 0) {
			decon = get_decon_drvdata(displayport_get_decon_id(i));

			if (irq_status_reg & VSYNC_DET) {
				/* VSYNC interrupt, accept it */
				decon->frame_cnt++;
				wake_up_interruptible_all(&decon->wait_vstatus);

				if (decon->dt.psr_mode == DECON_VIDEO_MODE) {
					decon->vsync.timestamp = ktime_get();
					wake_up_interruptible_all(&decon->vsync.wait);
				}
			}
		}

		irq_status_reg = displayport_reg_get_sst_audio_interrupt_and_clear(i);

		if (irq_status_reg & MASTER_AUDIO_BUFFER_EMPTY_INT) {
			displayport_dbg("SST%d AFIFO_UNDER detect\n", i + 1);
			displayport->sst[i]->audio_buf_empty_check = 1;
		}
	}

	spin_unlock(&displayport->slock);

	return IRQ_HANDLED;
}

static u8 displayport_get_vic(u32 sst_id)
{
	struct displayport_device *displayport = get_displayport_drvdata();

	return supported_videos[displayport->sst[sst_id]->cur_video].vic;
}

static int displayport_make_avi_infoframe_data(u32 sst_id,
		struct infoframe *avi_infoframe)
{
	int i;

	avi_infoframe->type_code = INFOFRAME_PACKET_TYPE_AVI;
	avi_infoframe->version_number = AVI_INFOFRAME_VERSION;
	avi_infoframe->length = AVI_INFOFRAME_LENGTH;

	for (i = 0; i < AVI_INFOFRAME_LENGTH; i++)
		avi_infoframe->data[i] = 0x00;

	avi_infoframe->data[0] |= ACTIVE_FORMAT_INFOMATION_PRESENT;
	avi_infoframe->data[1] |= ACITVE_PORTION_ASPECT_RATIO;
	avi_infoframe->data[3] = displayport_get_vic(sst_id);

	return 0;
}

static int displayport_make_audio_infoframe_data(struct infoframe *audio_infoframe,
		struct displayport_audio_config_data *audio_config_data)
{
	int i;

	audio_infoframe->type_code = INFOFRAME_PACKET_TYPE_AUDIO;
	audio_infoframe->version_number = AUDIO_INFOFRAME_VERSION;
	audio_infoframe->length = AUDIO_INFOFRAME_LENGTH;

	for (i = 0; i < AUDIO_INFOFRAME_LENGTH; i++)
		audio_infoframe->data[i] = 0x00;

	/* Data Byte 1, PCM type and audio channel count */
	audio_infoframe->data[0] = ((u8)audio_config_data->audio_channel_cnt - 1);

	/* Data Byte 4, how various speaker locations are allocated */
	audio_infoframe->data[3] = 0;

	displayport_info("audio_infoframe: type and ch_cnt %02x, SF and bit size %02x, ch_allocation %02x\n",
			audio_infoframe->data[0], audio_infoframe->data[1], audio_infoframe->data[3]);

	return 0;
}

static int displayport_make_hdr_infoframe_data
	(struct infoframe *hdr_infoframe, struct exynos_hdr_static_info *hdr_info)
{
	int i;

	hdr_infoframe->type_code = INFOFRAME_PACKET_TYPE_HDR;
	hdr_infoframe->version_number = HDR_INFOFRAME_VERSION;
	hdr_infoframe->length = HDR_INFOFRAME_LENGTH;

	for (i = 0; i < HDR_INFOFRAME_LENGTH; i++)
		hdr_infoframe->data[i] = 0x00;

	hdr_infoframe->data[HDR_INFOFRAME_EOTF_BYTE_NUM] = HDR_INFOFRAME_SMPTE_ST_2084;
	hdr_infoframe->data[HDR_INFOFRAME_METADATA_ID_BYTE_NUM]
		= STATIC_MATADATA_TYPE_1;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_X_0_LSB]
		= hdr_info->stype1.mr.x & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_X_0_MSB]
		= (hdr_info->stype1.mr.x & MSB_MASK) >> SHIFT_8BIT;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_Y_0_LSB]
		= hdr_info->stype1.mr.y & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_Y_0_MSB]
		= (hdr_info->stype1.mr.y & MSB_MASK) >> SHIFT_8BIT;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_X_1_LSB]
		= hdr_info->stype1.mg.x & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_X_1_MSB]
		= (hdr_info->stype1.mg.x & MSB_MASK) >> SHIFT_8BIT;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_Y_1_LSB]
		= hdr_info->stype1.mg.y & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_Y_1_MSB]
		= (hdr_info->stype1.mg.y & MSB_MASK) >> SHIFT_8BIT;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_X_2_LSB]
		= hdr_info->stype1.mb.x & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_X_2_MSB]
		= (hdr_info->stype1.mb.x & MSB_MASK) >> SHIFT_8BIT;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_Y_2_LSB]
		= hdr_info->stype1.mb.y & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_DISP_PRI_Y_2_MSB]
		= (hdr_info->stype1.mb.y & MSB_MASK) >> SHIFT_8BIT;
	hdr_infoframe->data[HDR_INFOFRAME_WHITE_POINT_X_LSB]
		= hdr_info->stype1.mw.x & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_WHITE_POINT_X_MSB]
		= (hdr_info->stype1.mw.x & MSB_MASK) >> SHIFT_8BIT;
	hdr_infoframe->data[HDR_INFOFRAME_WHITE_POINT_Y_LSB]
		= hdr_info->stype1.mw.y & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_WHITE_POINT_Y_MSB]
		= (hdr_info->stype1.mw.y & MSB_MASK) >> SHIFT_8BIT;
	hdr_infoframe->data[HDR_INFOFRAME_MAX_LUMI_LSB]
		= hdr_info->stype1.mmax_display_luminance & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_MAX_LUMI_MSB]
		= (hdr_info->stype1.mmax_display_luminance & MSB_MASK) >> SHIFT_8BIT;
	hdr_infoframe->data[HDR_INFOFRAME_MIN_LUMI_LSB]
		= hdr_info->stype1.mmin_display_luminance & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_MIN_LUMI_MSB]
		= (hdr_info->stype1.mmin_display_luminance & MSB_MASK) >> SHIFT_8BIT;
	hdr_infoframe->data[HDR_INFOFRAME_MAX_LIGHT_LEVEL_LSB]
		= hdr_info->stype1.mmax_content_light_level & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_MAX_LIGHT_LEVEL_MSB]
		= (hdr_info->stype1.mmax_content_light_level & MSB_MASK) >> SHIFT_8BIT;
	hdr_infoframe->data[HDR_INFOFRAME_MAX_AVERAGE_LEVEL_LSB]
		= hdr_info->stype1.mmax_frame_average_light_level & LSB_MASK;
	hdr_infoframe->data[HDR_INFOFRAME_MAX_AVERAGE_LEVEL_MSB]
		= (hdr_info->stype1.mmax_frame_average_light_level & MSB_MASK) >> SHIFT_8BIT;

	for (i = 0; i < HDR_INFOFRAME_LENGTH; i++) {
		displayport_dbg("hdr_infoframe->data[%d] = 0x%02x", i,
			hdr_infoframe->data[i]);
	}

	print_hex_dump(KERN_INFO, "HDR: ", DUMP_PREFIX_NONE, 32, 1,
			hdr_infoframe->data, HDR_INFOFRAME_LENGTH, false);

	return 0;
}

static int displayport_set_avi_infoframe(u32 sst_id)
{
	struct infoframe avi_infoframe;

	displayport_make_avi_infoframe_data(sst_id, &avi_infoframe);
	displayport_reg_set_avi_infoframe(sst_id, avi_infoframe);

	return 0;
}

static int displayport_make_spd_infoframe_data(struct infoframe *spd_infoframe)
{
	spd_infoframe->type_code = 0x83;
	spd_infoframe->version_number = 0x1;
	spd_infoframe->length = 25;

	strncpy(&spd_infoframe->data[0], "SEC.GED", 8);

	return 0;
}

static int displayport_set_spd_infoframe(u32 sst_id)
{
	struct infoframe spd_infoframe;

	memset(&spd_infoframe, 0, sizeof(spd_infoframe));
	displayport_make_spd_infoframe_data(&spd_infoframe);
	displayport_reg_set_spd_infoframe(sst_id, spd_infoframe);

	return 0;
}

static int displayport_set_audio_infoframe(u32 sst_id,
		struct displayport_audio_config_data *audio_config_data)
{
	struct infoframe audio_infoframe;

	displayport_make_audio_infoframe_data(&audio_infoframe, audio_config_data);
	displayport_reg_set_audio_infoframe(sst_id, audio_infoframe, audio_config_data->audio_enable);

	return 0;
}

static int displayport_set_hdr_infoframe(u32 sst_id,
		struct exynos_hdr_static_info *hdr_info)
{
	struct infoframe hdr_infoframe;

	if (hdr_info->mid >= 0) {
		displayport_dbg("SST%d displayport_set_hdr_infoframe 1\n", sst_id + 1);
		displayport_make_hdr_infoframe_data(&hdr_infoframe, hdr_info);
		displayport_reg_set_hdr_infoframe(sst_id, hdr_infoframe, 1);
	} else {
		displayport_dbg("SST%d displayport_set_hdr_infoframe 0\n", sst_id + 1);
		displayport_reg_set_hdr_infoframe(sst_id, hdr_infoframe, 0);
	}

	return 0;
}

int displayport_audio_config(u32 sst_id, struct displayport_audio_config_data *audio_config_data)
{
	struct displayport_device *displayport = get_displayport_drvdata();
	int ret = 0;

	displayport_info("SST%d audio config(%d ==> %d)\n",
			sst_id + 1, displayport->sst[sst_id]->audio_state, audio_config_data->audio_enable);
	
	if (displayport_check_sst_on(displayport) == DISPLAYPORT_STATE_OFF)
		return 0;

	if (audio_config_data->audio_enable == displayport->sst[sst_id]->audio_state)
		return 0;

	if (audio_config_data->audio_enable == AUDIO_ENABLE) {
		displayport_info("audio_enable:%d, ch:%d, fs:%d, bit:%d, packed:%d, word_len:%d\n",
				audio_config_data->audio_enable, audio_config_data->audio_channel_cnt,
				audio_config_data->audio_fs, audio_config_data->audio_bit,
				audio_config_data->audio_packed_mode, audio_config_data->audio_word_length);

		displayport_audio_enable(sst_id, audio_config_data);
		displayport_set_audio_infoframe(sst_id, audio_config_data);
		displayport->sst[sst_id]->audio_state = AUDIO_ENABLE;
	} else if (audio_config_data->audio_enable == AUDIO_DISABLE) {
		displayport_audio_disable(sst_id);
		displayport_set_audio_infoframe(sst_id, audio_config_data);
		displayport->sst[sst_id]->audio_state = AUDIO_DISABLE;
	} else if (audio_config_data->audio_enable == AUDIO_WAIT_BUF_FULL) {
		displayport_audio_wait_buf_full(sst_id);
		displayport->sst[sst_id]->audio_state = AUDIO_WAIT_BUF_FULL;
	} else if (audio_config_data->audio_enable == AUDIO_DMA_REQ_HIGH) {
		displayport_reg_set_dma_req_gen(sst_id, 1);
		displayport->sst[sst_id]->audio_state = AUDIO_DMA_REQ_HIGH;
	} else
		displayport_info("Not support audio_enable = %d\n", audio_config_data->audio_enable);

	return ret;
}
EXPORT_SYMBOL(displayport_audio_config);

void displayport_audio_bist_config(u32 sst_id,
		struct displayport_audio_config_data audio_config_data)
{
	displayport_audio_bist_enable(sst_id, audio_config_data);
	displayport_set_audio_infoframe(sst_id, &audio_config_data);
}

int displayport_dpcd_read_for_hdcp22(u32 address, u32 length, u8 *data)
{
	int ret;

	ret = displayport_reg_dpcd_read_burst(address, length, data);

	if (ret != 0)
		displayport_err("dpcd_read_for_hdcp22 fail: 0x%Xn", address);

	return ret;
}

int displayport_dpcd_write_for_hdcp22(u32 address, u32 length, u8 *data)
{
	int ret;

	ret = displayport_reg_dpcd_write_burst(address, length, data);

	if (ret != 0)
		displayport_err("dpcd_write_for_hdcp22 fail: 0x%X\n", address);

	return ret;
}

void displayport_hdcp22_enable(u32 en)
{
	struct decon_device *decon = get_decon_drvdata(DEFAULT_DECON_ID);

	/* wait 2 frames for hdcp encryption enable/disable */
	decon_wait_for_vsync(decon, VSYNC_TIMEOUT_MSEC);
	decon_wait_for_vsync(decon, VSYNC_TIMEOUT_MSEC);

	if (en) {
		displayport_reg_set_hdcp22_system_enable(1);
		displayport_reg_set_hdcp22_mode(1);
		displayport_reg_set_hdcp22_encryption_enable(1);
	} else {
		displayport_reg_set_hdcp22_system_enable(0);
		displayport_reg_set_hdcp22_mode(0);
		displayport_reg_set_hdcp22_encryption_enable(0);
	}
}

static void displayport_hdcp13_run(struct work_struct *work)
{
	struct displayport_device *displayport = get_displayport_drvdata();

	if (displayport->hdcp_ver != HDCP_VERSION_1_3 ||
			!displayport->hpd_current_state)
		return;

	displayport_dbg("[HDCP 1.3] run\n");
	hdcp13_run();
	if (hdcp13_info.auth_state == HDCP13_STATE_FAIL) {
		queue_delayed_work(displayport->dp_wq, &displayport->hdcp13_work,
				msecs_to_jiffies(2000));
	}
}

static void displayport_hdcp22_run(struct work_struct *work)
{
#if defined(CONFIG_EXYNOS_HDCP2)
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 ret;
	u8 val[2] = {0, };

	mutex_lock(&displayport->hdcp2_lock);
	if (IS_DISPLAYPORT_HPD_PLUG_STATE() == 0) {
		displayport_info("stop hdcp2 : HPD is low\n");
		goto exit_hdcp;
	}

	displayport_hdcp22_notify_state(DP_HDCP_READY);
	ret = displayport_hdcp22_authenticate();
	if (ret) {
#ifdef CONFIG_SEC_DISPLAYPORT_BIGDATA
		secdp_bigdata_inc_error_cnt(ERR_HDCP_AUTH);
#endif
		goto exit_hdcp;
	}

	if (IS_DISPLAYPORT_HPD_PLUG_STATE() == 0) {
		displayport_info("stop hdcp2 : HPD is low\n");
		goto exit_hdcp;
	}

	displayport_dpcd_read_for_hdcp22(DPCD_HDCP22_RX_INFO, 2, val);
	displayport_info("HDCP2.2 rx_info: 0:0x%X, 8:0x%X\n", val[1], val[0]);

exit_hdcp:
	mutex_unlock(&displayport->hdcp2_lock);
#else
	displayport_info("Not compiled EXYNOS_HDCP2 driver\n");
#endif
}

static int displayport_check_hdcp_version(void)
{
	int ret = 0;
	u8 val[DPCD_HDCP22_RX_CAPS_LENGTH];
	u32 rx_caps = 0;
	int i;

	hdcp13_dpcd_buffer();

	if (hdcp13_read_bcap() != 0)
		displayport_dbg("[HDCP 1.3] NONE HDCP CAPABLE\n");
#if defined(HDCP_SUPPORT)
	else
		ret = HDCP_VERSION_1_3;
#endif
	displayport_dpcd_read_for_hdcp22(DPCD_HDCP22_RX_CAPS, DPCD_HDCP22_RX_CAPS_LENGTH, val);

	for (i = 0; i < DPCD_HDCP22_RX_CAPS_LENGTH; i++)
		rx_caps |= (u32)val[i] << ((DPCD_HDCP22_RX_CAPS_LENGTH - (i + 1)) * 8);

	displayport_info("HDCP2.2 rx_caps = 0x%x\n", rx_caps);

	if ((((rx_caps & VERSION) >> DPCD_HDCP_VERSION_BIT_POSITION) == (HDCP_VERSION_2_2))
			&& ((rx_caps & HDCP_CAPABLE) != 0)) {
#if defined(HDCP_2_2)
		ret = HDCP_VERSION_2_2;
#endif
		displayport_dbg("displayport_rx supports hdcp2.2\n");
	}

	return ret;
}

static void hdcp_start(struct displayport_device *displayport)
{
	displayport->hdcp_ver = displayport_check_hdcp_version();
#if defined(HDCP_SUPPORT)
	if (displayport->hdcp_ver == HDCP_VERSION_2_2)
		queue_delayed_work(displayport->hdcp2_wq, &displayport->hdcp22_work,
				msecs_to_jiffies(3500));
	else if (displayport->hdcp_ver == HDCP_VERSION_1_3)
		queue_delayed_work(displayport->dp_wq, &displayport->hdcp13_work,
				msecs_to_jiffies(5500));
	else
		displayport_info("HDCP is not supported\n");
#endif
}

static enum displayport_state displayport_check_sst_on(struct displayport_device *displayport)
{
	int ret = DISPLAYPORT_STATE_OFF;
	int i = 0;

	for (i = SST1; i < MAX_SST_CNT; i++) {
		if (displayport->sst[i]->state == DISPLAYPORT_STATE_ON) {
			ret = DISPLAYPORT_STATE_ON;
			break;
		}
	}

	return ret;
}

int displayport_enable(struct displayport_device *displayport)
{
	int ret = 0;
	u32 sst_id = displayport->cur_sst_id;
	u8 bpc = (u8)displayport->sst[sst_id]->bpc;
	u8 bist_type = (u8)displayport->sst[sst_id]->bist_type;
	u8 dyn_range = (u8)displayport->sst[sst_id]->dyn_range;

	mutex_lock(&displayport->cmd_lock);

	if (!displayport->hpd_current_state) {
		displayport_err("%s() hpd is low\n", __func__);
		mutex_unlock(&displayport->cmd_lock);
		return 0;
	}

	if (displayport->sst[sst_id]->state == DISPLAYPORT_STATE_ON) {
		displayport_err("SST%d ignore enable, state:%d\n", sst_id + 1,
				displayport->sst[sst_id]->state);
		mutex_unlock(&displayport->cmd_lock);
		return 0;
	}

	if (forced_resolution >= 0)
		displayport->sst[sst_id]->cur_video = forced_resolution;
	else if (displayport->sst[sst_id]->bist_used)
		displayport->sst[sst_id]->cur_video = displayport->sst[sst_id]->best_video;

	if (displayport->mst_cap) {
		displayport_topology_set_vc(sst_id);
		usleep_range(25, 30);
	}

	if (displayport_check_sst_on(displayport) != DISPLAYPORT_STATE_ON) {
		displayport_info("first sst on\n");
#if defined(CONFIG_CPU_IDLE)
		/* block to enter SICD mode */
		exynos_update_ip_idle_status(displayport->idle_ip_index, 0);
#endif
		enable_irq(displayport->res.irq);
	}

	if (displayport->sst[sst_id]->bist_used) {
		displayport_reg_set_bist_video_configuration(sst_id,
				displayport->sst[sst_id]->cur_video, bpc, bist_type, dyn_range);
	} else {
		if (displayport->sst[sst_id]->bpc == BPC_6 && displayport->dfp_type != DFP_TYPE_DP) {
			bpc = BPC_8;
			displayport->sst[sst_id]->bpc = BPC_8;
		}

		displayport_reg_set_video_configuration(sst_id,
				displayport->sst[sst_id]->cur_video, bpc, dyn_range);
	}

	displayport_info("SST%d cur_video = %s in displayport_enable!!!\n", sst_id + 1,
			supported_videos[displayport->sst[sst_id]->cur_video].name);

	displayport_set_avi_infoframe(sst_id);
	displayport_set_spd_infoframe(sst_id);
#ifdef HDCP_SUPPORT
	displayport_reg_video_mute(0);
#endif
	displayport_reg_start(sst_id);

	if (displayport_check_sst_on(displayport) != DISPLAYPORT_STATE_ON)
		hdcp_start(displayport);

	displayport->sst[sst_id]->state = DISPLAYPORT_STATE_ON;

	mutex_unlock(&displayport->cmd_lock);

	return ret;
}

int displayport_disable(struct displayport_device *displayport)
{
	u32 sst_id = displayport->cur_sst_id;

	displayport_info("SST%d %s +, state: %d\n", sst_id + 1,
			__func__, displayport->sst[sst_id]->state);

	mutex_lock(&displayport->cmd_lock);

	if (displayport->sst[sst_id]->state != DISPLAYPORT_STATE_ON) {
		displayport_err("ignore disable, state:%d\n", displayport->sst[sst_id]->state);
		mutex_unlock(&displayport->cmd_lock);
		return 0;
	}

	displayport_reg_stop(sst_id);
	displayport_reg_set_video_bist_mode(sst_id, 0);

	if (displayport->mst_cap && displayport->hpd_current_state == HPD_PLUG) {
		displayport_mst_payload_table_delete(sst_id);
		displayport_clr_vc_config(sst_id, displayport);
		displayport_info("SST%d displayport_mst_payload_table_delete\n", sst_id + 1);
	}

	displayport->sst[sst_id]->state = DISPLAYPORT_STATE_OFF_CHECK;

	if (displayport_check_sst_on(displayport) == DISPLAYPORT_STATE_OFF) {
		disable_irq(displayport->res.irq);

		hdcp13_info.auth_state = HDCP13_STATE_NOT_AUTHENTICATED;
		displayport_info("all sst off");
	}

	displayport->sst[sst_id]->state = DISPLAYPORT_STATE_OFF;

	displayport_info("%s -\n", __func__);

	mutex_unlock(&displayport->cmd_lock);

	return 0;
}

bool displayport_match_timings(const struct v4l2_dv_timings *t1,
		const struct v4l2_dv_timings *t2,
		unsigned pclock_delta)
{
	if (t1->type != t2->type)
		return false;

	if (t1->bt.width == t2->bt.width &&
			t1->bt.height == t2->bt.height &&
			t1->bt.interlaced == t2->bt.interlaced &&
			t1->bt.polarities == t2->bt.polarities &&
			t1->bt.pixelclock >= t2->bt.pixelclock - pclock_delta &&
			t1->bt.pixelclock <= t2->bt.pixelclock + pclock_delta &&
			t1->bt.hfrontporch == t2->bt.hfrontporch &&
			t1->bt.vfrontporch == t2->bt.vfrontporch &&
			t1->bt.vsync == t2->bt.vsync &&
			t1->bt.vbackporch == t2->bt.vbackporch &&
			(!t1->bt.interlaced ||
			 (t1->bt.il_vfrontporch == t2->bt.il_vfrontporch &&
			  t1->bt.il_vsync == t2->bt.il_vsync &&
			  t1->bt.il_vbackporch == t2->bt.il_vbackporch)))
		return true;

	return false;
}

static int displayport_timing2conf(struct v4l2_dv_timings *timings)
{
	int i;

	for (i = 0; i < supported_videos_pre_cnt; i++) {
		if (displayport_match_timings(&supported_videos[i].dv_timings,
					timings, 0))
			return i;
	}

	return -EINVAL;
}

static void displayport_dv_timings_to_str(const struct v4l2_dv_timings *t, char *str, int size)
{
	const struct v4l2_bt_timings *bt = &t->bt;
	u32 htot, vtot;
	u32 fps;

	if (t->type != V4L2_DV_BT_656_1120)
		return;

	htot = V4L2_DV_BT_FRAME_WIDTH(bt);
	vtot = V4L2_DV_BT_FRAME_HEIGHT(bt);
	if (bt->interlaced)
		vtot /= 2;

	fps = (htot * vtot) > 0 ? div_u64((100 * (u64)bt->pixelclock),
			(htot * vtot)) : 0;

	snprintf(str, size - 1, "%ux%u%s%u.%u", bt->width, bt->height,
			bt->interlaced ? "i" : "p", fps / 100, fps % 100);
}

static int displayport_s_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	struct displayport_device *displayport = container_of(sd, struct displayport_device, sd);
	u32 sst_id = displayport->cur_sst_id;
	struct decon_device *decon = get_decon_drvdata(displayport_get_decon_id(sst_id));
	videoformat displayport_setting_videoformat = V640X480P60;
	int ret = 0;
	char timingstr[32] = {0x0, };

	displayport_dv_timings_to_str(timings, timingstr, sizeof(timingstr));
	displayport_info("set timing %s\n", timingstr);

	ret = displayport_timing2conf(timings);
	if (ret < 0) {
		displayport_err("displayport timings not supported\n");
		return -EINVAL;
	}
	displayport_setting_videoformat = ret;

	if (displayport->sst[sst_id]->bist_used == 0) {
		if (displayport_check_pixel_clock_for_hdr(sst_id, displayport,
				displayport_setting_videoformat) == true
				&& displayport_setting_videoformat >= V3840X2160P50
				&& displayport_setting_videoformat < V640X10P60SACRC) {
			displayport_info("sink support HDR\n");
			/* BPC_10 should be enabled when support HDR */
			displayport->sst[sst_id]->bpc = BPC_10;
		} else {
			displayport->sst[sst_id]->rx_edid_data.hdr_support = 0;
			displayport->sst[sst_id]->bpc = BPC_8;
		}

		/*fail safe mode (640x480) with 6 bpc*/
		if (displayport_setting_videoformat == V640X480P60)
			displayport->sst[sst_id]->bpc = BPC_6;
	}

	displayport->sst[sst_id]->cur_video = displayport_setting_videoformat;
	displayport->sst[sst_id]->cur_timings = *timings;

	decon_displayport_get_out_sd(decon);

	displayport_dbg("SST%d New cur_video = %s\n", sst_id + 1,
			supported_videos[displayport->sst[sst_id]->cur_video].name);

	return 0;
}

static int displayport_g_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_dv_timings *timings)
{
	struct displayport_device *displayport = container_of(sd, struct displayport_device, sd);

	*timings = displayport->sst[displayport->cur_sst_id]->cur_timings;

	displayport_dbg("displayport_g_dv_timings\n");

	return 0;
}

static int displayport_enum_dv_timings(struct v4l2_subdev *sd,
		struct v4l2_enum_dv_timings *timings)
{
	struct displayport_device *displayport = container_of(sd, struct displayport_device, sd);
	if (timings->index >= supported_videos_pre_cnt) {
		displayport_warn("request index %d is too big\n", timings->index);
		return -E2BIG;
	}

	if (!supported_videos[timings->index].edid_support_match) {
		displayport_dbg("not supported video_format : %s\n",
				supported_videos[timings->index].name);
		return -EINVAL;
	}

	/* V640X480P60 is always supported.
	 * it's to avoid that any one is not selected */
	if (timings->index == V640X480P60) {
		timings->timings = supported_videos[timings->index].dv_timings;
		return 0;
	}

	if (displayport->dex_setting) {
		if (displayport->dex_video_pick &&
				timings->index > displayport->dex_video_pick) {
			displayport_info("dex proper ratio video pick %d\n", displayport->dex_video_pick);
			return -E2BIG;
		}
		if (displayport->mst_mode && timings->index > DEX_MST_MAX_VIDEO) {
			displayport_dbg("not supported video_format : %s in dex mst mode\n",
					supported_videos[timings->index].name);
			return -EINVAL;
		}
		if (supported_videos[timings->index].dex_support == DEX_NOT_SUPPORT) {
			displayport_dbg("not supported video_format : %s in dex mode\n",
					supported_videos[timings->index].name);
			return -EINVAL;
		}
		if (supported_videos[timings->index].dex_support > displayport->dex_adapter_type) {
			displayport_info("%s not supported, adapter:%d, resolution:%d in dex mode\n",
					supported_videos[timings->index].name,
					displayport->dex_adapter_type,
					supported_videos[timings->index].dex_support);
			return -EINVAL;
		}
	}

	/* reduce the timing by lane count and link rate */
	if (displayport_check_link_rate_pixel_clock(displayport_reg_phy_get_link_bw(),
			displayport_reg_get_lane_count(),
			supported_videos[timings->index].dv_timings.bt.pixelclock,
			BPC_8) == false) {
		displayport_info("can't set index %d (over link rate)\n", timings->index);
		return -EINVAL;
	}

	if (reduced_resolution && reduced_resolution <
			supported_videos[timings->index].dv_timings.bt.pixelclock) {
		displayport_info("reduced_resolution: %llu, idx: %d\n",
				reduced_resolution, timings->index);
		return -E2BIG;
	}

	displayport_dbg("matched video_format : %s\n", supported_videos[timings->index].name);
	timings->timings = supported_videos[timings->index].dv_timings;

	return 0;
}

static int displayport_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct displayport_device *displayport = container_of(sd, struct displayport_device, sd);

	if (enable)
		return displayport_enable(displayport);
	else
		return displayport_disable(displayport);
}

int displayport_set_hdr_config(u32 sst_id, struct exynos_hdr_static_info *hdr_info)
{
	int ret = 0;

	displayport_set_hdr_infoframe(sst_id, hdr_info);

	return ret;
}

void displayport_get_hdr_support(u32 sst_id,
		struct displayport_device *displayport, int *hdr_support)
{
	*hdr_support = (int)displayport->sst[sst_id]->rx_edid_data.hdr_support;
}

bool is_displayport_not_running(void)
{
	int i = 0;

	struct displayport_device *displayport = get_displayport_drvdata();

	for (i = SST1; i < MAX_SST_CNT; i++) {
		if (displayport->sst[i]->state == DISPLAYPORT_STATE_ON)
			return false;
	}

	return true;
}

static long displayport_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct displayport_device *displayport = container_of(sd, struct displayport_device, sd);
	int ret = 0;
	struct v4l2_enum_dv_timings *enum_timings;
	struct exynos_hdr_static_info *hdr_info;
	int *hdr_support;

	switch (cmd) {
	case DISPLAYPORT_IOC_DUMP:
		displayport_dump_registers(displayport);
		break;

	case DISPLAYPORT_IOC_GET_ENUM_DV_TIMINGS:
		enum_timings = (struct v4l2_enum_dv_timings *)arg;

		ret = displayport_enum_dv_timings(sd, enum_timings);
		break;

	case DISPLAYPORT_IOC_SET_RECONNECTION:	/* for restart without hpd change */
		displayport_set_reconnection();
		break;

	case DISPLAYPORT_IOC_SET_HDR_METADATA:
		hdr_info = (struct exynos_hdr_static_info *)arg;
		/* set info frame for hdr contents */
		ret = displayport_set_hdr_config(displayport->cur_sst_id, hdr_info);
		if (ret)
			displayport_err("failed to configure hdr info\n");
		break;

	case DISPLAYPORT_IOC_GET_HDR_INFO:
		hdr_support = (int *)arg;

		displayport_get_hdr_support(displayport->cur_sst_id,
				displayport, hdr_support);
		break;

	case EXYNOS_DPU_GET_ACLK:
		return clk_get_rate(displayport->res.aclk);

	default:
		displayport_err("unsupported ioctl");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_subdev_core_ops displayport_sd_core_ops = {
	.ioctl = displayport_ioctl,
};

static const struct v4l2_subdev_video_ops displayport_sd_video_ops = {
	.s_dv_timings = displayport_s_dv_timings,
	.g_dv_timings = displayport_g_dv_timings,
	.s_stream = displayport_s_stream,
};

static const struct v4l2_subdev_ops displayport_subdev_ops = {
	.core = &displayport_sd_core_ops,
	.video = &displayport_sd_video_ops,
};

static void displayport_init_subdev(struct displayport_device *displayport)
{
	struct v4l2_subdev *sd = &displayport->sd;

	v4l2_subdev_init(sd, &displayport_subdev_ops);
	sd->owner = THIS_MODULE;
	snprintf(sd->name, sizeof(sd->name), "%s", "displayport-sd");
	v4l2_set_subdevdata(sd, displayport);
}

static int displayport_parse_dt(struct displayport_device *displayport, struct device *dev)
{
	struct device_node *np = dev->of_node;
	int ret;

	if (IS_ERR_OR_NULL(dev->of_node)) {
		displayport_err("no device tree information\n");
		return -EINVAL;
	}

	displayport->dev = dev;

	displayport->gpio_sw_oe = of_get_named_gpio(np, "dp,aux_sw_oe", 0);
	if (gpio_is_valid(displayport->gpio_sw_oe)) {
		ret = gpio_request(displayport->gpio_sw_oe, "dp_aux_sw_oe");
		if (ret)
			displayport_err("failed to get gpio dp_aux_sw_oe request\n");
		else
			gpio_direction_output(displayport->gpio_sw_oe, 1);
	} else
		displayport_err("failed to get gpio dp_aux_sw_oe validation\n");

	displayport->gpio_sw_sel = of_get_named_gpio(np, "dp,sbu_sw_sel", 0);
	if (gpio_is_valid(displayport->gpio_sw_sel)) {
		ret = gpio_request(displayport->gpio_sw_sel, "dp_sbu_sw_sel");
		if (ret)
			displayport_err("failed to get gpio dp_sbu_sw_sel request\n");
	} else
		displayport_err("failed to get gpio dp_sbu_sw_sel validation\n");

	displayport->gpio_usb_dir = of_get_named_gpio(np, "dp,usb_con_sel", 0);
	if (!gpio_is_valid(displayport->gpio_usb_dir))
		displayport_err("failed to get gpio dp_usb_con_sel validation\n");

	displayport_info("%s done %d, %d, %d \n", __func__,
		displayport->gpio_sw_oe, displayport->gpio_sw_sel, displayport->gpio_usb_dir);

	return 0;
}

static int displayport_init_resources(struct displayport_device *displayport, struct platform_device *pdev)
{
	struct resource *res;
	int ret = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		displayport_err("failed to get mem resource\n");
		return -ENOENT;
	}

	displayport_info("link_regs: start(0x%x), end(0x%x)\n", (u32)res->start, (u32)res->end);

	displayport->res.link_regs = devm_ioremap_resource(displayport->dev, res);
	if (IS_ERR(displayport->res.link_regs)) {
		displayport_err("failed to remap DisplayPort LINK SFR region\n");
		return -EINVAL;
	}

#if defined(CONFIG_PHY_EXYNOS_USBDRD)
	displayport->res.phy_regs = phy_exynos_usbdp_get_address();
	if (!displayport->res.phy_regs) {
		displayport_err("failed to get USBDP combo PHY SFR region\n");
		return -EINVAL;
	}
#endif

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		displayport_err("failed to get irq resource\n");
		return -ENOENT;
	}

	displayport->res.irq = res->start;
	ret = devm_request_irq(displayport->dev, res->start,
			displayport_irq_handler, 0, pdev->name, displayport);
	if (ret) {
		displayport_err("failed to install DisplayPort irq\n");
		return -EINVAL;
	}
	disable_irq(displayport->res.irq);

	displayport->res.aclk = devm_clk_get(displayport->dev, "aclk");
	if (IS_ERR_OR_NULL(displayport->res.aclk)) {
		displayport_err("failed to get aclk\n");
		return PTR_ERR(displayport->res.aclk);
	}

	return 0;
}

#if defined(CONFIG_USB_TYPEC_MANAGER_NOTIFIER)
static int displayport_aux_onoff(struct displayport_device *displayport, int
		onoff)
{
	int rc = 0;

	displayport_info("aux vdd onoff = %d\n", onoff);
	if (gpio_is_valid(displayport->gpio_sw_oe)) {
		if (onoff == 1)
			gpio_direction_output(displayport->gpio_sw_oe, 0);
		else
			gpio_direction_output(displayport->gpio_sw_oe, 1);
	} else {
		displayport_info("aux switch is not available\n");
		rc = -1;
	}
	return rc;
}

static void displayport_aux_sel(struct displayport_device *displayport)
{
	if (gpio_is_valid(displayport->gpio_usb_dir) &&
			gpio_is_valid(displayport->gpio_sw_sel)) {
		displayport->dp_sw_sel = !gpio_get_value(displayport->gpio_usb_dir);
		gpio_direction_output(displayport->gpio_sw_sel, !(displayport->dp_sw_sel));
		displayport_info("Get direction from ccic %d\n", displayport->dp_sw_sel);
	} else if (gpio_is_valid(displayport->gpio_usb_dir)) {
		/* redriver support case */
		displayport->dp_sw_sel = !gpio_get_value(displayport->gpio_usb_dir);
		displayport_info("Get Direction From CCIC %d\n", !displayport->dp_sw_sel);
	}
}

static void displayport_check_adapter_type(struct displayport_device *displayport)
{
	displayport->dex_adapter_type = DEX_FHD_SUPPORT;

	if (displayport->ven_id != 0x04e8)
		return;

	switch(displayport->prod_id) {
	case 0xa029: /* PAD */
	case 0xa020: /* Station */
	case 0xa02a:
	case 0xa02b:
	case 0xa02c:
	case 0xa02d:
	case 0xa02e:
	case 0xa02f:
	case 0xa030:
	case 0xa031:
	case 0xa032:
	case 0xa033:
		displayport->dex_adapter_type = DEX_WQHD_SUPPORT;
		break;
	};
}

static int displayport_usb_typec_notification_proceed(struct displayport_device *displayport,
				CC_NOTI_TYPEDEF *usb_typec_info)
{
	displayport_dbg("%s: action (%ld) dump(0x%01x, 0x%01x, 0x%02x, 0x%04x, 0x%04x, 0x%04x)\n",
			__func__, usb_typec_info->src, usb_typec_info->dest, usb_typec_info->id,
			usb_typec_info->sub1, usb_typec_info->sub2, usb_typec_info->sub3);

	switch (usb_typec_info->id) {
	case CCIC_NOTIFY_ID_DP_CONNECT:
		switch (usb_typec_info->sub1) {
		case CCIC_NOTIFY_DETACH:
			dp_logger_set_max_count(100);
			displayport_info("CCIC_NOTIFY_ID_DP_CONNECT, %x\n", usb_typec_info->sub1);
			displayport->ccic_notify_dp_conf = CCIC_NOTIFY_DP_PIN_UNKNOWN;
			displayport->ccic_link_conf = false;
			displayport->ccic_hpd = false;
			displayport_hdcp22_notify_state(DP_DISCONNECT);
			displayport_hpd_changed(0);
			displayport_aux_onoff(displayport, 0);
			break;
		case CCIC_NOTIFY_ATTACH:
			dp_logger_set_max_count(100);
			displayport_info("CCIC_NOTIFY_ID_DP_CONNECT, %x\n", usb_typec_info->sub1);
			displayport->ven_id = usb_typec_info->sub2;
			displayport->prod_id = usb_typec_info->sub3;
			displayport_check_adapter_type(displayport);
			displayport_info("VID:0x%llX, PID:0x%llX\n", displayport->ven_id, displayport->prod_id);
			displayport_aux_onoff(displayport, 1);
			break;
		default:
			break;
		}

		break;

	case CCIC_NOTIFY_ID_DP_LINK_CONF:
		displayport_info("CCIC_NOTIFY_ID_DP_LINK_CONF %x\n",
				usb_typec_info->sub1);
		displayport_aux_sel(displayport);
		switch (usb_typec_info->sub1) {
		case CCIC_NOTIFY_DP_PIN_UNKNOWN:
			displayport->ccic_notify_dp_conf = CCIC_NOTIFY_DP_PIN_UNKNOWN;
			break;
		case CCIC_NOTIFY_DP_PIN_A:
			displayport->ccic_notify_dp_conf = CCIC_NOTIFY_DP_PIN_A;
			break;
		case CCIC_NOTIFY_DP_PIN_B:
			displayport->dp_sw_sel = !displayport->dp_sw_sel;
			displayport->ccic_notify_dp_conf = CCIC_NOTIFY_DP_PIN_B;
			break;
		case CCIC_NOTIFY_DP_PIN_C:
			displayport->ccic_notify_dp_conf = CCIC_NOTIFY_DP_PIN_C;
			break;
		case CCIC_NOTIFY_DP_PIN_D:
			displayport->ccic_notify_dp_conf = CCIC_NOTIFY_DP_PIN_D;
			break;
		case CCIC_NOTIFY_DP_PIN_E:
			displayport->ccic_notify_dp_conf = CCIC_NOTIFY_DP_PIN_E;
			break;
		case CCIC_NOTIFY_DP_PIN_F:
			displayport->ccic_notify_dp_conf = CCIC_NOTIFY_DP_PIN_F;
			break;
		default:
			displayport->ccic_notify_dp_conf = CCIC_NOTIFY_DP_PIN_UNKNOWN;
			break;
		}

		if (displayport->ccic_notify_dp_conf) {
			displayport->ccic_link_conf = true;
			if (displayport->ccic_hpd)
				displayport_hpd_changed(1);
		}
		break;

	case CCIC_NOTIFY_ID_DP_HPD:
		displayport_info("CCIC_NOTIFY_ID_DP_HPD, %x, %x\n",
				usb_typec_info->sub1, usb_typec_info->sub2);
		switch (usb_typec_info->sub1) {
		case CCIC_NOTIFY_IRQ:
			break;
		case CCIC_NOTIFY_LOW:
			displayport->ccic_hpd = false;
			displayport_hdcp22_notify_state(DP_DISCONNECT);
			displayport_hpd_changed(0);
			break;
		case CCIC_NOTIFY_HIGH:
			if (displayport->hpd_current_state &&
					usb_typec_info->sub2 == CCIC_NOTIFY_IRQ) {
				queue_delayed_work(displayport->dp_wq, &displayport->hpd_irq_work, 0);
				return 0;
			} else {
				displayport->ccic_hpd = true;
				dp_logger_set_max_count(300);
				if (displayport->ccic_link_conf) {
					displayport_hpd_changed(1);
				}
			}
			break;
		default:
			break;
		}

		break;

	default:
		break;
	}

	return 0;
}

#if defined(CONFIG_USE_DISPLAYPORT_CCIC_EVENT_QUEUE)
/* TODO: need exception handle */
static void displayport_ccic_event_proceed_work(struct work_struct *work)
{
	struct displayport_device *displayport = get_displayport_drvdata();
	struct ccic_event *data, *next;

	list_for_each_entry_safe(data, next, &displayport->list_cc, list) {
		displayport_usb_typec_notification_proceed(displayport, &data->event);
		list_del(&data->list);
		kfree(data);
	}
}
#endif

static int usb_typec_displayport_notification(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct displayport_device *displayport = get_displayport_drvdata();
	CC_NOTI_TYPEDEF usb_typec_info = *(CC_NOTI_TYPEDEF *)data;

	if (usb_typec_info.dest != CCIC_NOTIFY_DEV_DP)
		return 0;

#if defined(CONFIG_USE_DISPLAYPORT_CCIC_EVENT_QUEUE)
	{
		struct ccic_event *cc_data;
		int ret;

		displayport_info("CCIC action(%ld) dump(0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x)\n",
			action, usb_typec_info.src, usb_typec_info.dest, usb_typec_info.id,
			usb_typec_info.sub1, usb_typec_info.sub2, usb_typec_info.sub3);

		cc_data = kzalloc(sizeof(struct ccic_event), GFP_KERNEL);
		if (cc_data < 0) {
			displayport_err("kzalloc error for ccic event\n");
			return 0;
		}

		memcpy(&cc_data->event, &usb_typec_info, sizeof(usb_typec_info));
		list_add_tail(&cc_data->list, &displayport->list_cc);
		ret = queue_delayed_work(displayport->dp_wq, &displayport->ccic_event_proceed_work, 0);
		displayport_info("CCIC event is queued (%d)\n", ret);
	}
#else
	displayport_dbg("CCIC action(%ld) dump(0x%x, 0x%x, 0x%x, 0x%x, 0x%x, 0x%x)\n",
			action, usb_typec_info.src, usb_typec_info.dest, usb_typec_info.id,
			usb_typec_info.sub1, usb_typec_info.sub2, usb_typec_info.sub3);

	displayport_usb_typec_notification_proceed(displayport, &usb_typec_info);
#endif
	return 0;
}

static void displayport_notifier_register_work(struct work_struct *work)
{
	struct displayport_device *displayport = get_displayport_drvdata();

	if (!displayport->notifier_registered) {
		displayport->notifier_registered = 1;
		displayport_info("notifier registered\n");
		manager_notifier_register(&displayport->dp_typec_nb,
			usb_typec_displayport_notification, MANAGER_NOTIFY_CCIC_DP);
	}
}
#endif

#ifdef DISPLAYPORT_TEST
static ssize_t displayport_link_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	/*struct displayport_device *displayport = get_displayport_drvdata();*/

	return snprintf(buf, PAGE_SIZE, "%s\n", __func__);
}

static ssize_t displayport_link_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	int mode = 0;
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	if (kstrtoint(buf, 10, &mode))
		return size;
	pr_info("%s mode=%d\n", __func__, mode);

	if (mode == 0) {
		displayport_hpd_changed(0);
		displayport_link_sink_status_read();
	} else if (mode == 8) {
		queue_delayed_work(displayport->dp_wq, &displayport->hpd_irq_work, 0);
	} else if (mode == 9) {
		displayport_hpd_changed(1);
	} else {
		u8 link_rate = mode/10;
		u8 lane_cnt = mode%10;

		if ((link_rate >= 1 && link_rate <= 3) &&
				(lane_cnt == 1 || lane_cnt == 2 || lane_cnt == 4)) {
			if (link_rate == 4)
				link_rate = LINK_RATE_8_1Gbps;
			else if (link_rate == 3)
				link_rate = LINK_RATE_5_4Gbps;
			else if (link_rate == 2)
				link_rate = LINK_RATE_2_7Gbps;
			else
				link_rate = LINK_RATE_1_62Gbps;

			pr_info("%s: %02x %02x\n", __func__, link_rate, lane_cnt);
			displayport_reg_init(); /* for AUX ch read/write. */

			displayport_link_status_read(sst_id);

			g_displayport_debug_param.param_used = 1;
			g_displayport_debug_param.link_rate = link_rate;
			g_displayport_debug_param.lane_cnt = lane_cnt;

			displayport_full_link_training(sst_id);

			g_displayport_debug_param.param_used = 0;

			displayport_set_extcon_state(sst_id, displayport, 1);
			displayport_wait_state_change(sst_id, displayport,
					1000, DISPLAYPORT_STATE_ON);
		} else {
			pr_err("%s: Not support command[%d]\n",
					__func__, mode);
		}
	}

	return size;
}

static CLASS_ATTR(link, 0664, displayport_link_show, displayport_link_store);

static ssize_t displayport_test_bpc_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	return snprintf(buf, PAGE_SIZE, "displayport bpc %d\n",
			(displayport->sst[sst_id]->bpc == BPC_6)?6:8);
}
static ssize_t displayport_test_bpc_store(struct class *dev,
		struct class_attribute *attr,
		const char *buf, size_t size)
{
	int mode = 0;
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	if (kstrtoint(buf, 10, &mode))
		return size;
	pr_info("%s mode=%d\n", __func__, mode);

	switch (mode) {
	case 6:
		displayport->sst[sst_id]->bpc = BPC_6;
		break;
	case 8:
		displayport->sst[sst_id]->bpc = BPC_8;
		break;
	default:
		pr_err("%s: Not support command[%d]\n",
				__func__, mode);
		break;
	}

	return size;
}

static CLASS_ATTR(bpc, 0664, displayport_test_bpc_show, displayport_test_bpc_store);

static ssize_t displayport_test_range_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	return sprintf(buf, "displayport range %s\n",
		(displayport->sst[sst_id]->dyn_range == VESA_RANGE)?"VESA_RANGE":"CEA_RANGE");
}
static ssize_t displayport_test_range_store(struct class *dev,
		struct class_attribute *attr,
		const char *buf, size_t size)
{
	int mode = 0;
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	if (kstrtoint(buf, 10, &mode))
		return size;
	pr_info("%s mode=%d\n", __func__, mode);

	switch (mode) {
	case 0:
		displayport->sst[sst_id]->dyn_range = VESA_RANGE;
		break;
	case 1:
		displayport->sst[sst_id]->dyn_range = CEA_RANGE;
		break;
	default:
		pr_err("%s: Not support command[%d]\n",
				__func__, mode);
		break;
	}

	return size;
}

static CLASS_ATTR(range, 0664, displayport_test_range_show, displayport_test_range_store);

static ssize_t displayport_test_edid_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	struct v4l2_dv_timings edid_preset;
	int i;
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	edid_preset = edid_preferred_preset();

	i = displayport_timing2conf(&edid_preset);
	if (i < 0) {
		i = displayport->sst[sst_id]->cur_video;
		pr_err("displayport timings not supported\n");
	}

	return snprintf(buf, PAGE_SIZE, "displayport preferred_preset = %d %d %d\n",
			videoformat_parameters[i].active_pixel,
			videoformat_parameters[i].active_line,
			videoformat_parameters[i].fps);
}

static ssize_t displayport_test_edid_store(struct class *dev,
		struct class_attribute *attr,
		const char *buf, size_t size)
{
	struct v4l2_dv_timings edid_preset;
	int i;
	int mode = 0;
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	if (kstrtoint(buf, 10, &mode))
		return size;
	pr_info("%s mode=%d\n", __func__, mode);

	edid_set_preferred_preset(mode);
	edid_preset = edid_preferred_preset();
	i = displayport_timing2conf(&edid_preset);
	if (i < 0) {
		i = displayport->sst[sst_id]->cur_video;
		pr_err("displayport timings not supported\n");
	}

	pr_info("displayport preferred_preset = %d %d %d\n",
			videoformat_parameters[i].active_pixel,
			videoformat_parameters[i].active_line,
			videoformat_parameters[i].fps);

	return size;
}
static CLASS_ATTR(edid, 0664, displayport_test_edid_show, displayport_test_edid_store);

static ssize_t displayport_test_bist_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	return snprintf(buf, PAGE_SIZE, "displayport bist used %d type %d\n",
			displayport->sst[sst_id]->bist_used,
			displayport->sst[sst_id]->bist_type);
}

static ssize_t displayport_test_bist_store(struct class *dev,
		struct class_attribute *attr,
		const char *buf, size_t size)
{
	int mode = 0;
	struct displayport_audio_config_data audio_config_data;
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	if (kstrtoint(buf, 10, &mode))
		return size;
	pr_info("%s mode=%d\n", __func__, mode);

	switch (mode) {
	case 0:
		displayport->sst[sst_id]->bist_used = 0;
		displayport->sst[sst_id]->bist_type = COLOR_BAR;
		break;
	case 1:
		displayport->sst[sst_id]->bist_used = 1;
		displayport->sst[sst_id]->bist_type = COLOR_BAR;
		break;
	case 2:
		displayport->sst[sst_id]->bist_used = 1;
		displayport->sst[sst_id]->bist_type = WGB_BAR;
		break;
	case 3:
		displayport->sst[sst_id]->bist_used = 1;
		displayport->sst[sst_id]->bist_type = MW_BAR;
		break;
	case 4:
		displayport->sst[sst_id]->bist_used = 1;
		displayport->sst[sst_id]->bist_type = CTS_COLOR_RAMP;
		break;
	case 5:
		displayport->sst[sst_id]->bist_used = 1;
		displayport->sst[sst_id]->bist_type = CTS_BLACK_WHITE;
		break;
	case 6:
		displayport->sst[sst_id]->bist_used = 1;
		displayport->sst[sst_id]->bist_type = CTS_COLOR_SQUARE_VESA;
		break;
	case 7:
		displayport->sst[sst_id]->bist_used = 1;
		displayport->sst[sst_id]->bist_type = CTS_COLOR_SQUARE_CEA;
		break;
	case 11:
	case 12:
		audio_config_data.audio_enable = 1;
		audio_config_data.audio_fs = FS_192KHZ;
		audio_config_data.audio_channel_cnt = mode-10;
		audio_config_data.audio_bit = 0;
		audio_config_data.audio_packed_mode = 0;
		audio_config_data.audio_word_length = 0;
		displayport_audio_bist_config(SST1, audio_config_data);
		break;
	default:
		pr_err("%s: Not support command[%d]\n",
				__func__, mode);
		break;
	}

	return size;
}
static CLASS_ATTR(bist, 0664, displayport_test_bist_show, displayport_test_bist_store);

static ssize_t displayport_test_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	struct displayport_device *displayport = get_displayport_drvdata();

	return snprintf(buf, PAGE_SIZE, "displayport gpio oe %d, sel %d, direction %d\n",
			gpio_get_value(displayport->gpio_sw_oe),
			gpio_get_value(displayport->gpio_sw_sel),
			gpio_get_value(displayport->gpio_usb_dir));
}
static ssize_t displayport_test_store(struct class *dev,
		struct class_attribute *attr,
		const char *buf, size_t size)
{
	/*	struct displayport_device *displayport = get_displayport_drvdata(); */

	return size;
}

static CLASS_ATTR(dp, 0664, displayport_test_show, displayport_test_store);

static ssize_t displayport_forced_resolution_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int ret = 0;
	int i;

	for (i = 0; i < supported_videos_pre_cnt; i++) {
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%c %2d : %s\n",
				forced_resolution == i ? '*':' ', i,
				supported_videos[i].name);
	}

	return ret;
}

static ssize_t displayport_forced_resolution_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	int val[4] = {0,};

	get_options(buf, 4, val);

	reduced_resolution = 0;

	if (val[1] < 0 || val[1] >= supported_videos_pre_cnt || val[0] < 1)
		forced_resolution = -1;
	else {
		struct displayport_device *displayport = get_displayport_drvdata();
		int hpd_stat = displayport->hpd_current_state;

		forced_resolution = val[1];
		if (hpd_stat) {
			displayport_hpd_changed(0);
			msleep(100);
			displayport_hpd_changed(1);
		}
	}

	return size;
}
static CLASS_ATTR(forced_resolution, 0664, displayport_forced_resolution_show,
		displayport_forced_resolution_store);

static ssize_t displayport_reduced_resolution_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int ret = 0;

	ret = scnprintf(buf, PAGE_SIZE, "%llu\n", reduced_resolution);

	return ret;
}

static ssize_t displayport_reduced_resolution_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	int val[4] = {0,};

	get_options(buf, 4, val);

	forced_resolution = -1;

	if (val[1] < 0 || val[1] >= supported_videos_pre_cnt || val[0] < 1)
		reduced_resolution = 0;
	else {
		switch (val[1]) {
		case 1:
			reduced_resolution = PIXELCLK_2160P30HZ;
			break;
		case 2:
			reduced_resolution = PIXELCLK_1080P60HZ;
			break;
		case 3:
			reduced_resolution = PIXELCLK_1080P30HZ;
			break;
		default:
			reduced_resolution = 0;
		};
	}

	return size;
}
static CLASS_ATTR(reduced_resolution, 0664, displayport_reduced_resolution_show,
		displayport_reduced_resolution_store);
#endif

static ssize_t dex_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	struct displayport_device *displayport = get_displayport_drvdata();
	int ret = 0;

	displayport_info("dex state: %d\n", displayport->dex_state);
	ret = scnprintf(buf, PAGE_SIZE, "%d\n", displayport->dex_state);
	if (displayport->dex_state == DEX_RECONNECTING)
		displayport->dex_state = DEX_ON;

	return ret;
}

static ssize_t dex_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	struct displayport_device *displayport = get_displayport_drvdata();
	int val[4] = {0,};
	int dex_run;
	int need_reconnect = 0;	
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);
	int cur_video = displayport->sst[sst_id]->cur_video;
	int best_video = displayport->sst[sst_id]->best_video;
	int mst_support = displayport->mst_cap;

	get_options(buf, 2, val);

	displayport->dex_setting = (val[1] & 0xF0) >> 4;
	dex_run = (val[1] & 0x0F);

	displayport_info("dex state:%d, setting:%d, run:%d, hpd:%d\n",
			displayport->dex_state, displayport->dex_setting,
			dex_run, displayport->hpd_current_state);

#if defined(CONFIG_USB_TYPEC_MANAGER_NOTIFIER)
	if (!displayport->notifier_registered) {
		displayport->notifier_registered = 1;
		displayport_info("notifier registered in dex\n");
		cancel_delayed_work_sync(&displayport->notifier_register_work);
		manager_notifier_register(&displayport->dp_typec_nb,
			usb_typec_displayport_notification, MANAGER_NOTIFY_CCIC_DP);
		goto dex_exit;
	}
#endif

	/* if current state is not new state, then reconnect */
	if (displayport->dex_state != dex_run &&
			displayport->dex_state != DEX_RECONNECTING)
		need_reconnect = 1;

	if (mst_support) {//TODO
		displayport_info("mst mode, just reconnect\n");
	} else if (displayport->dex_setting) {
		/* if current resolution is dex supported, then do not reconnect */
		if (supported_videos[cur_video].dex_support != DEX_NOT_SUPPORT &&
			supported_videos[cur_video].dex_support <= displayport->dex_adapter_type) {
			displayport_info("current video support dex %d\n", cur_video);
			need_reconnect = 0;
		}
	} else {
		/* if current resolution is best, then do not reconnect */
		if (cur_video == best_video) {
			displayport_info("current video is best %d\n", cur_video);
			need_reconnect = 0;
		}
	}

	displayport->dex_state = dex_run;

	/* reconnect if setting was mirroring(0) and dex is running(1), */
	if (displayport->hpd_current_state && need_reconnect) {
		displayport_info("reconnecting to %s mode\n", dex_run ? "dex":"mirroring");
		displayport->dex_state = DEX_RECONNECTING;
		displayport_hpd_changed(0);
		msleep(1000);
		if (displayport->dex_state != DEX_OFF)
			displayport_hpd_changed(1);
	}

dex_exit:
	displayport_info("dex exit: state:%d, setting:%d\n",
			displayport->dex_state, displayport->dex_setting);

	return size;
}
static CLASS_ATTR_RW(dex);

static ssize_t dex_ver_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int ret = 0;
	struct displayport_device *displayport = get_displayport_drvdata();

	ret = scnprintf(buf, PAGE_SIZE, "%02X%02X\n", displayport->dex_ver[0],
			displayport->dex_ver[1]);

	return ret;
}

static CLASS_ATTR_RO(dex_ver);

static ssize_t monitor_info_show(struct class *class,
		struct class_attribute *attr, char *buf)
{
	int ret = 0;
	struct displayport_device *displayport = get_displayport_drvdata();
	u32 sst_id = displayport_get_sst_id_with_decon_id(DEFAULT_DECON_ID);

	ret = scnprintf(buf, PAGE_SIZE, "%s,0x%x,0x%x\n",
			displayport->sst[sst_id]->rx_edid_data.edid_manufacturer,
			displayport->sst[sst_id]->rx_edid_data.edid_product,
			displayport->sst[sst_id]->rx_edid_data.edid_serial);
	displayport_info("manufacturer : %s product : %x serial num : %x\n",
			displayport->sst[sst_id]->rx_edid_data.edid_manufacturer,
			displayport->sst[sst_id]->rx_edid_data.edid_product,
			displayport->sst[sst_id]->rx_edid_data.edid_serial);

	return ret;
}

static CLASS_ATTR_RO(monitor_info);

static ssize_t dp_sbu_sw_sel_store(struct class *dev,
		struct class_attribute *attr, const char *buf, size_t size)
{
	struct displayport_device *displayport = get_displayport_drvdata();
	int val[10] = {0,};
	int aux_sw_sel, aux_sw_oe;

	get_options(buf, 10, val);

	aux_sw_sel = val[1];
	aux_sw_oe = val[2];
	displayport_info("sbu_sw_sel(%d), sbu_sw_oe(%d)\n", aux_sw_sel, aux_sw_oe);

	if ((aux_sw_sel == 0 || aux_sw_sel == 1) && (aux_sw_oe == 0 || aux_sw_oe == 1)) {
		if (gpio_is_valid(displayport->gpio_sw_sel))
			gpio_direction_output(displayport->gpio_sw_sel, aux_sw_sel);
		displayport_aux_onoff(displayport, !aux_sw_oe);
	} else
		displayport_err("invalid aux switch parameter\n");

	return size;
}
static CLASS_ATTR_WO(dp_sbu_sw_sel);

static ssize_t log_level_show(struct class *class,
		struct class_attribute *attr,
		char *buf)
{
	return snprintf(buf, PAGE_SIZE, "displayport log level %1d\n", displayport_log_level);
}
static ssize_t log_level_store(struct class *dev,
		struct class_attribute *attr,
		const char *buf, size_t size)
{
	int mode = 0;

	if (kstrtoint(buf, 10, &mode))
		return size;
	displayport_log_level = mode;
	displayport_err("log level = %d\n", displayport_log_level);

	return size;
}

static CLASS_ATTR_RW(log_level);

static int displayport_init_sst_info(struct displayport_device *displayport)
{
	int ret = 0;
	int i = 0;
	struct displayport_sst *sst = NULL;
	struct displayport_vc_config *vc_config = NULL;

	for (i = SST1; i < MAX_SST_CNT; i++) {
		if (displayport->sst[i] == NULL) {
			sst = kzalloc(sizeof(struct displayport_sst), GFP_KERNEL);
			if (!sst) {
				displayport_err("could not allocate sst[%d]\n", i);
				ret = -ENOMEM;
				goto err_sst_info;
			}

			displayport->sst[i] = sst;
			displayport->sst[i]->id = i;
			displayport->sst[i]->decon_id = displayport_get_decon_id(i);
			displayport->sst[i]->state = DISPLAYPORT_STATE_INIT;
			displayport->sst[i]->cur_video = V640X480P60;
			displayport->sst[i]->bpc = BPC_8;
			displayport->sst[i]->dyn_range = VESA_RANGE;
			displayport->sst[i]->bist_used = 0;
			displayport->sst[i]->bist_type = COLOR_BAR;
			displayport->sst[i]->audio_state = AUDIO_DISABLE;
			displayport->sst[i]->audio_buf_empty_check = 0;

			vc_config = kzalloc(sizeof(struct displayport_vc_config), GFP_KERNEL);
			if (!vc_config) {
				displayport_err("could not allocate sst[%d] vc_config\n", i);
				ret = -ENOMEM;
				goto err_sst_info;
			}

			displayport->sst[i]->vc_config = vc_config;
		}
	}

err_sst_info:
	return ret;
}

static int displayport_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device *dev = &pdev->dev;
	struct displayport_device *displayport = NULL;
	struct class *dp_class;

	dp_logger_init();
	dev_info(dev, "%s start\n", __func__);

	displayport = devm_kzalloc(dev, sizeof(struct displayport_device), GFP_KERNEL);
	if (!displayport) {
		displayport_err("failed to allocate displayport device.\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = displayport_init_sst_info(displayport);
	if (ret)
		goto err_dt;

	dma_set_mask(dev, DMA_BIT_MASK(36));

	ret = displayport_parse_dt(displayport, dev);
	if (ret)
		goto err_dt;

	displayport_drvdata = displayport;

	spin_lock_init(&displayport->slock);
	mutex_init(&displayport->cmd_lock);
	mutex_init(&displayport->hpd_lock);
	mutex_init(&displayport->aux_lock);
	mutex_init(&displayport->training_lock);
	mutex_init(&displayport->hdcp2_lock);

	ret = displayport_init_resources(displayport, pdev);
	if (ret)
		goto err_dt;

	displayport_init_subdev(displayport);
	platform_set_drvdata(pdev, displayport);

	displayport->dp_wq = create_singlethread_workqueue(dev_name(&pdev->dev));
	if (!displayport->dp_wq) {
		displayport_err("create wq failed.\n");
		goto err_dt;
	}

	displayport->hdcp2_wq = create_singlethread_workqueue(dev_name(&pdev->dev));
	if (!displayport->hdcp2_wq) {
		displayport_err("create hdcp2_wq failed.\n");
		goto err_dt;
	}

	INIT_DELAYED_WORK(&displayport->hpd_plug_work, displayport_hpd_plug_work);
	INIT_DELAYED_WORK(&displayport->hpd_unplug_work, displayport_hpd_unplug_work);
	INIT_DELAYED_WORK(&displayport->hpd_irq_work, displayport_hpd_irq_work);
	INIT_DELAYED_WORK(&displayport->hdcp13_work, displayport_hdcp13_run);
	INIT_DELAYED_WORK(&displayport->hdcp22_work, displayport_hdcp22_run);
	INIT_DELAYED_WORK(&displayport->hdcp13_integrity_check_work, displayport_hdcp13_integrity_check_work);

#if defined(CONFIG_USB_TYPEC_MANAGER_NOTIFIER)
#if defined(CONFIG_USE_DISPLAYPORT_CCIC_EVENT_QUEUE)
	INIT_LIST_HEAD(&displayport->list_cc);
	INIT_DELAYED_WORK(&displayport->ccic_event_proceed_work,
			displayport_ccic_event_proceed_work);
#endif
	INIT_DELAYED_WORK(&displayport->notifier_register_work,
			displayport_notifier_register_work);
	queue_delayed_work(displayport->dp_wq, &displayport->notifier_register_work,
			msecs_to_jiffies(30000));
#endif

#if defined(CONFIG_EXTCON)
	/* register the extcon device for HPD */
	displayport->extcon_displayport = devm_extcon_dev_allocate(displayport->dev, extcon_id);
	if (IS_ERR(displayport->extcon_displayport)) {
		displayport_err("displayport extcon dev_allocate failed.\n");
		goto err_dt;
	}

	ret = devm_extcon_dev_register(displayport->dev, displayport->extcon_displayport);
	if (ret) {
		displayport_err("hdmi extcon register failed.\n");
		goto err_dt;
	}
#else
	displayport_info("Not compiled EXTCON driver\n");
#endif

	pm_runtime_enable(dev);

#if defined(CONFIG_ION_EXYNOS)
	ret = iovmm_activate(dev);
	if (ret) {
		displayport_err("failed to activate iovmm\n");
		goto err_dt;
	}
	iovmm_set_fault_handler(dev, dpu_sysmmu_fault_handler, NULL);
#endif

#if defined(CONFIG_CPU_IDLE)
	displayport->idle_ip_index =
		exynos_get_idle_ip_index(dev_name(&pdev->dev));
	if (displayport->idle_ip_index < 0)
		displayport_warn("idle ip index is not provided for DP\n");
	exynos_update_ip_idle_status(displayport->idle_ip_index, 1);
#endif

	ret = device_init_wakeup(displayport->dev, true);
	if (ret) {
		dev_err(displayport->dev, "failed to init wakeup device\n");
		return -EINVAL;
	}

	dp_class = class_create(THIS_MODULE, "dp_sec");
	if (IS_ERR(dp_class))
		displayport_err("failed to creat dp_class\n");
	else {
#ifdef DISPLAYPORT_TEST
		ret = class_create_file(dp_class, &class_attr_link);
		if (ret)
			displayport_err("failed to create attr_link\n");
		ret = class_create_file(dp_class, &class_attr_bpc);
		if (ret)
			displayport_err("failed to create attr_bpc\n");
		ret = class_create_file(dp_class, &class_attr_range);
		if (ret)
			displayport_err("failed to create attr_range\n");
		ret = class_create_file(dp_class, &class_attr_edid);
		if (ret)
			displayport_err("failed to create attr_edid\n");
		ret = class_create_file(dp_class, &class_attr_bist);
#if defined(CONFIG_EXYNOS_HDCP2)
		ret = class_create_file(dp_class, &class_attr_dp_drm);
		if (ret)
			displayport_err("failed to create attr_dp_drm\n");
#endif
		if (ret)
			displayport_err("failed to create attr_bist\n");
		ret = class_create_file(dp_class, &class_attr_dp);
		if (ret)
			displayport_err("failed to create attr_test\n");
		ret = class_create_file(dp_class, &class_attr_forced_resolution);
		if (ret)
			displayport_err("failed to create attr_dp_forced_resolution\n");
		ret = class_create_file(dp_class, &class_attr_reduced_resolution);
		if (ret)
			displayport_err("failed to create attr_dp_reduced_resolution\n");
#endif

		ret = class_create_file(dp_class, &class_attr_dex);
		if (ret)
			displayport_err("failed to create attr_dp_dex\n");
		ret = class_create_file(dp_class, &class_attr_dex_ver);
		if (ret)
			displayport_err("failed to create attr_dp_dex_ver\n");
		ret = class_create_file(dp_class, &class_attr_monitor_info);
		if (ret)
			displayport_err("failed to create attr_dp_monitor_info\n");
		ret = class_create_file(dp_class, &class_attr_dp_sbu_sw_sel);
		if (ret)
			displayport_err("failed to create class_attr_dp_sbu_sw_sel\n");
		ret = class_create_file(dp_class, &class_attr_log_level);
		if (ret)
			displayport_err("failed to create class_attr_log_level\n");
	}

	g_displayport_debug_param.param_used = 0;
	g_displayport_debug_param.link_rate = LINK_RATE_2_7Gbps;
	g_displayport_debug_param.lane_cnt = 0x04;

	displayport->forced_bist = -1;

#if defined(CONFIG_EXYNOS_HDCP2)
	displayport->drm_start_state = DRM_OFF;
#endif

	displayport_info("displayport driver has been probed.\n");
	return 0;

err_dt:
	kfree(displayport);
err:
	return ret;
}

static void displayport_shutdown(struct platform_device *pdev)
{
#if 0
	struct displayport_device *displayport = platform_get_drvdata(pdev);
	int i = 0;

	/* DPU_EVENT_LOG(DPU_EVT_DP_SHUTDOWN, &displayport->sd, ktime_set(0, 0)); */
	for (i = SST1; i < MAX_SST_CNT; i++) {
		displayport_info("SST%d %s + state:%d\n", i + 1, __func__, displayport->sst[i]->state);
		displayport_disable(displayport);
	}

	displayport_info("%s -\n", __func__);
#else
	displayport_info("%s +-\n", __func__);
#endif
}

static int displayport_runtime_suspend(struct device *dev)
{
	struct displayport_device *displayport = dev_get_drvdata(dev);

	/* DPU_EVENT_LOG(DPU_EVT_DP_SUSPEND, &displayport->sd, ktime_set(0, 0)); */
	displayport_dbg("%s +\n", __func__);
	clk_disable_unprepare(displayport->res.aclk);
	displayport_dbg("%s -\n", __func__);
	return 0;
}

static int displayport_runtime_resume(struct device *dev)
{
	struct displayport_device *displayport = dev_get_drvdata(dev);

	/* DPU_EVENT_LOG(DPU_EVT_DP_RESUME, &displayport->sd, ktime_set(0, 0)); */
	displayport_dbg("%s: +\n", __func__);
	clk_prepare_enable(displayport->res.aclk);
	displayport_dbg("%s -\n", __func__);
	return 0;
}

static const struct of_device_id displayport_of_match[] = {
	{ .compatible = "samsung,exynos-displayport" },
	{},
};
MODULE_DEVICE_TABLE(of, displayport_of_match);

static const struct dev_pm_ops displayport_pm_ops = {
	.runtime_suspend	= displayport_runtime_suspend,
	.runtime_resume		= displayport_runtime_resume,
};

static struct platform_driver displayport_driver __refdata = {
	.probe			= displayport_probe,
	.remove			= displayport_remove,
	.shutdown		= displayport_shutdown,
	.driver = {
		.name		= DISPLAYPORT_MODULE_NAME,
		.owner		= THIS_MODULE,
		.pm		= &displayport_pm_ops,
		.of_match_table	= of_match_ptr(displayport_of_match),
		.suppress_bind_attrs = true,
	}
};

static int __init displayport_init(void)
{
	int ret = platform_driver_register(&displayport_driver);

	if (ret)
		pr_err("displayport driver register failed\n");

	return ret;
}
late_initcall(displayport_init);

static void __exit displayport_exit(void)
{
	platform_driver_unregister(&displayport_driver);
}

module_exit(displayport_exit);
MODULE_AUTHOR("Kwangje Kim <kj1.kim@samsung.com>");
MODULE_DESCRIPTION("Samusung EXYNOS DisplayPort driver");
MODULE_LICENSE("GPL");
