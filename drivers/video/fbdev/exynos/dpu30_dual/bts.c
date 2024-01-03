 /*
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * BTS file for Samsung EXYNOS DPU driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "decon.h"
#include "dpp.h"
#include "format.h"

#include <soc/samsung/bts.h>
#include <media/v4l2-subdev.h>
#if defined(CONFIG_CAL_IF)
#include <soc/samsung/cal-if.h>
#endif
#include <dt-bindings/soc/samsung/exynos9830-devfreq.h>
#include <soc/samsung/exynos-devfreq.h>

#define DISP_FACTOR		100UL
#define LCD_REFRESH_RATE	63UL
#define MULTI_FACTOR 		(1UL << 10)
/* bus utilization 70% : same value with INT_UTIL */
#define BUS_UTIL		70

#define DPP_SCALE_NONE		0
#define DPP_SCALE_UP		1
#define DPP_SCALE_DOWN		2

#define ACLK_100MHZ_PERIOD	10000UL
#define ACLK_MHZ_INC_STEP	50UL	/* 50Mhz */
#define FRAME_TIME_NSEC		1000000000UL	/* 1sec */
#define TOTAL_BUS_LATENCY	3000UL	/* 3us: BUS(1) + PWT(1) + Requst(1) */

/* tuning parameters for rotation */
#define ROTATION_FACTOR_BPP	32UL
#define ROTATION_FACTOR_SCUP	1332UL	/* 1.3x */
#define ROTATION_FACTOR_SCDN	1434UL	/* 1.4x */
#define RESOL_QHDP_21_TO_9	3440*1440UL	/* for MIF min-lock */


/* unit : usec x 1000 -> 5592 (5.592us) for WQHD+ case */
static inline u32 dpu_bts_get_one_line_time(struct exynos_panel_info *lcd_info)
{
	u32 tot_v;
	int tmp;

	tot_v = lcd_info->yres + lcd_info->vfp + lcd_info->vsa + lcd_info->vbp;
	tmp = (FRAME_TIME_NSEC + lcd_info->fps - 1) / lcd_info->fps;

	return (tmp / tot_v);
}

/* lmc : line memory count (usually 4) */
static inline u32 dpu_bts_afbc_latency(u32 src_w, u32 ppc, u32 lmc)
{
	return ((src_w * lmc) / ppc);
}

/*
 * line memory max size : 4096
 * lmc : line memory count (usually 4)
 */
static inline u32 dpu_bts_scale_latency(u32 is_s, u32 src_w, u32 dst_w,
		u32 ppc, u32 lmc)
{
	u32 lat_scale = 0;
	u32 line_w;

	/*
	 * line_w : reflecting scale-ratio
	 * INC for scale-down & DEC for scale-up
	 */
	if (is_s == DPP_SCALE_DOWN)
		line_w = src_w * (src_w * 1000UL) / dst_w;
	else
		line_w = src_w * 1000UL;
	lat_scale = (line_w * lmc) / (ppc * 1000UL);

	return lat_scale;
}

/*
 * src_h : height of original input source image
 * cpl : cycles per line
 */
static inline u32 dpu_bts_rotate_latency(u32 src_h, u32 cpl)
{
	return (src_h * cpl * 12 / 10);
}

/*
 * [DSC]
 * Line memory is necessary like followings.
 *  1EA : 2-line for 2-slice, 1-line for 1-slice
 *  2EA : 3.5-line for 4-slice (DSCC 0.5-line + DSC 3-line)
 *        2.5-line for 2-slice (DSCC 0.5-line + DSC 2-line)
 *
 * [DECON] none
 * When 1H is filled at OUT_FIFO, it immediately transfers to DSIM.
 */
static inline u32 dpu_bts_dsc_latency(u32 slice_num, u32 dsc_cnt, u32 dst_w)
{
	u32 lat_dsc = dst_w;

	switch (slice_num) {
	case 1:
		/* DSC: 1EA */
		lat_dsc = dst_w * 1;
		break;
	case 2:
		if (dsc_cnt == 1)
			lat_dsc = dst_w * 2;
		else
			lat_dsc = (dst_w * 25) / 10;
		break;
	case 4:
		/* DSC: 2EA */
		lat_dsc = (dst_w * 35) / 10;
		break;
	default:
		break;
	}

	return lat_dsc;
}

/*
 * unit : cycles
 * rotate and afbc are incompatible
 */
static u32 dpu_bts_get_initial_latency(bool is_r, u32 is_s, bool is_c,
		struct exynos_panel_info *lcd_info, u32 src_w, u32 dst_w,
		u32 ppc, u32 cpl, u32 lmc)
{
	u32 lat_cycle = 0;
	u32 tmp;

	if (lcd_info->dsc.en) {
		lat_cycle = dpu_bts_dsc_latency(lcd_info->dsc.slice_num,
				lcd_info->dsc.cnt, dst_w);
		DPU_DEBUG_BTS("\tDSC_lat_cycle(%d)\n", lat_cycle);
	}

	/* src_w : rotation reflected value */
	if (is_r) {
		tmp = dpu_bts_rotate_latency(src_w, cpl);
		DPU_DEBUG_BTS("\tR_lat_cycle(%d)\n", tmp);
		lat_cycle += tmp;
	}
	if (is_s) {
		tmp = dpu_bts_scale_latency(is_s, src_w, dst_w, ppc, lmc);
		DPU_DEBUG_BTS("\tS_lat_cycle(%d)\n", tmp);
		lat_cycle += tmp;
	}
	if (is_c) {
		tmp = dpu_bts_afbc_latency(src_w, ppc, lmc);
		DPU_DEBUG_BTS("\tC_lat_cycle(%d)\n", tmp);
		lat_cycle += tmp;
	}

	return lat_cycle;
}

/*
 * unit : nsec x 1000
 * reference aclk : 100MHz (-> 10ns x 1000)
 */
static inline u32 dpu_bts_get_aclk_period_time(u32 aclk_mhz)
{
	return ((ACLK_100MHZ_PERIOD * 100) / aclk_mhz);
}

/* find min-ACLK to meet latency */
static u32 dpu_bts_find_latency_meet_aclk(u32 lat_cycle, u32 line_time,
		u32 criteria_v, u32 aclk_disp,
		bool is_yuv10, bool is_rotate, u32 rot_factor)
{
	u32 aclk_mhz = aclk_disp / 1000UL;
	u32 aclk_period, lat_time;
	u32 lat_time_max;

	DPU_DEBUG_BTS("\t(rot_factor = %d) (is_yuv10 = %d)\n",
			rot_factor, is_yuv10);

	/* lat_time_max: usec x 1000 */
	lat_time_max = line_time * criteria_v;

	/* find min-ACLK to able to cover initial latency */
	while (1) {
		/* aclk_period: nsec x 1000 */
		aclk_period = dpu_bts_get_aclk_period_time(aclk_mhz);
		lat_time = (lat_cycle * aclk_period) / 1000UL;
		lat_time = lat_time << is_yuv10;
		lat_time += TOTAL_BUS_LATENCY;
		if (is_rotate)
			lat_time = (lat_time * rot_factor) / MULTI_FACTOR;

		DPU_DEBUG_BTS("\tloop: (aclk_period = %d) (lat_time = %d)\n",
			aclk_period, lat_time);
		if (lat_time < lat_time_max)
			break;

		aclk_mhz += ACLK_MHZ_INC_STEP;
	}

	DPU_DEBUG_BTS("\t(lat_time = %d) (lat_time_max = %d)\n",
		lat_time, lat_time_max);

	return (aclk_mhz * 1000UL);
}

/* return : kHz value based on 1-pixel processing pipe-line */
u32 dpu_bts_get_aclk(u32 xres, u32 yres, u32 fps)
{
	u32 aclk;
	u64 tmp;

	/* aclk = vclk_1pix * ( 1.1 + (48+20)/WIDTH ) : x1000 */
	tmp = 1100 + (48000 + 20000) / xres;
	aclk = xres * yres * fps * tmp / 1000;

	/* convert to kHz unit */
	return (aclk / 1000);
}

u64 dpu_bts_calc_aclk_disp(struct decon_device *decon,
		struct decon_win_config *config, u64 resol_clock)
{
	u64 s_ratio_h, s_ratio_v;
	u64 aclk_disp;
	u64 ppc;
	struct decon_frame *src = &config->src;
	struct decon_frame *dst = &config->dst;
	const struct dpu_fmt *fmt_info = dpu_find_fmt_info(config->format);
	u32 src_w, src_h;
	bool is_rotate = is_rotation(config) ? true : false;
	bool is_comp = is_afbc(config) ? true : false;
	u32 is_scale;
	u32 lat_cycle, line_time;
	u32 cycle_per_line, line_mem_cnt;
	u32 criteria_v, tot_v;
	u32 rot_factor = ROTATION_FACTOR_SCUP;

	if (is_rotate) {
		src_w = src->h;
		src_h = src->w;
	} else {
		src_w = src->w;
		src_h = src->h;
	}

	s_ratio_h = (src_w <= dst->w) ? MULTI_FACTOR : MULTI_FACTOR * (u64)src_w / (u64)dst->w;
	s_ratio_v = (src_h <= dst->h) ? MULTI_FACTOR : MULTI_FACTOR * (u64)src_h / (u64)dst->h;

	/* case for using dsc encoder 1ea at decon0 or decon1 */
	if ((decon->id != 2) && (decon->lcd_info->dsc.cnt == 1))
		ppc = ((decon->bts.ppc / 2UL) >= 1UL) ? (decon->bts.ppc / 2UL) : 1UL;
	else
		ppc = decon->bts.ppc;

	aclk_disp = resol_clock * s_ratio_h * s_ratio_v * DISP_FACTOR  / 100UL
		/ ppc * (MULTI_FACTOR * (u64)dst->w / (u64)decon->lcd_info->xres)
		/ (MULTI_FACTOR * MULTI_FACTOR * MULTI_FACTOR);

	if (aclk_disp < (resol_clock / ppc))
		aclk_disp = resol_clock / ppc;

	DPU_DEBUG_BTS("BEFORE latency calc: aclk_disp = %d\n", (u32)aclk_disp);

	/* scaling latency: width only */
	if (src_w < dst->w)
		is_scale = DPP_SCALE_UP;
	else if (src_w > dst->w) {
		is_scale = DPP_SCALE_DOWN;
		rot_factor = ROTATION_FACTOR_SCDN;
	} else
		is_scale = DPP_SCALE_NONE;

	/* to check initial latency */
	cycle_per_line = decon->bts.cycle_per_line;
	line_mem_cnt = decon->bts.line_mem_cnt;
	lat_cycle = dpu_bts_get_initial_latency(is_rotate, is_scale, is_comp,
			decon->lcd_info, src_w, dst->w, (u32)ppc, cycle_per_line,
			line_mem_cnt);
	line_time = dpu_bts_get_one_line_time(decon->lcd_info);

	if (decon->lcd_info->mode == DECON_VIDEO_MODE)
		criteria_v = decon->lcd_info->vbp;
	else {
		/* command mode margin : apply 20% of v-blank time */
		tot_v = decon->lcd_info->vfp + decon->lcd_info->vsa
			+ decon->lcd_info->vbp;
		criteria_v = tot_v - (tot_v * 20 / 100);
	}

	aclk_disp = dpu_bts_find_latency_meet_aclk(lat_cycle, line_time,
			criteria_v, aclk_disp, IS_YUV10(fmt_info), is_rotate,
			rot_factor);

	DPU_DEBUG_BTS("\t[R:%d C:%d S:%d] (lat_cycle=%d) (line_time=%d)\n",
		is_rotate, is_comp, is_scale, lat_cycle, line_time);
	DPU_DEBUG_BTS("AFTER latency calc: aclk_disp = %d\n", (u32)aclk_disp);

	return aclk_disp;
}

static void dpu_bts_sum_all_decon_bw(struct decon_device *decon, u32 ch_bw[])
{
	int i, j;

	if (decon->id < 0 || decon->id >= decon->dt.decon_cnt) {
		decon_warn("[%s] undefined decon id(%d)!\n", __func__, decon->id);
		return;
	}

	for (i = 0; i < BTS_DPU_MAX; ++i)
		decon->bts.ch_bw[decon->id][i] = ch_bw[i];

	for (i = 0; i < decon->dt.decon_cnt; ++i) {
		if (decon->id == i)
			continue;

		for (j = 0; j < BTS_DPU_MAX; ++j)
			ch_bw[j] += decon->bts.ch_bw[i][j];
	}
}

static void dpu_bts_find_max_disp_freq(struct decon_device *decon,
		struct decon_reg_data *regs)
{
	int i, j;
	u32 disp_ch_bw[BTS_DPU_MAX];
	u32 max_disp_ch_bw;
	u32 disp_op_freq = 0, freq = 0;
	u64 resol_clock;
	u64 op_fps = LCD_REFRESH_RATE;
	struct decon_win_config *config = regs->dpp_config;

	memset(disp_ch_bw, 0, sizeof(disp_ch_bw));

	for (i = 0; i < BTS_DPP_MAX; ++i)
		for (j = 0; j < BTS_DPU_MAX; ++j)
			if (decon->bts.bw[i].ch_num == j)
				disp_ch_bw[j] += decon->bts.bw[i].val;

	/* must be considered other decon's bw */
	dpu_bts_sum_all_decon_bw(decon, disp_ch_bw);

	for (i = 0; i < BTS_DPU_MAX; ++i)
		if (disp_ch_bw[i])
			DPU_DEBUG_BTS("\tCH%d = %d\n", i, disp_ch_bw[i]);

	max_disp_ch_bw = disp_ch_bw[0];
	for (i = 1; i < BTS_DPU_MAX; ++i)
		if (max_disp_ch_bw < disp_ch_bw[i])
			max_disp_ch_bw = disp_ch_bw[i];

	decon->bts.peak = max_disp_ch_bw;
	decon->bts.max_disp_freq = max_disp_ch_bw * 100 / (16 * BUS_UTIL) + 1;

	op_fps = decon->lcd_info->fps;

	resol_clock = dpu_bts_get_aclk(decon->lcd_info->xres,
			decon->lcd_info->yres, op_fps);

	DPU_DEBUG_BTS("\tDECON%d : resol clock = %d Khz @%d fps\n",
		decon->id, decon->bts.resol_clk, op_fps);
	
	for (i = 0; i < decon->dt.max_win; ++i) {
		if ((config[i].state != DECON_WIN_STATE_BUFFER) &&
				(config[i].state != DECON_WIN_STATE_COLOR))
			continue;

		freq = dpu_bts_calc_aclk_disp(decon, &config[i], resol_clock);
		if (disp_op_freq < freq)
			disp_op_freq = freq;
	}

	DPU_DEBUG_BTS("\tDISP bus freq(%d), operating freq(%d)\n",
			decon->bts.max_disp_freq, disp_op_freq);

	if (decon->bts.max_disp_freq < disp_op_freq)
		decon->bts.max_disp_freq = disp_op_freq;

	DPU_DEBUG_BTS("\tMAX DISP CH FREQ = %d\n", decon->bts.max_disp_freq);
}

static void dpu_bts_share_bw_info(int id)
{
	int i, j;
	struct decon_device *decon[3];
	int decon_cnt;

	decon_cnt = get_decon_drvdata(0)->dt.decon_cnt;

	for (i = 0; i < MAX_DECON_CNT; i++)
		decon[i] = NULL;

	for (i = 0; i < decon_cnt; i++)
		decon[i] = get_decon_drvdata(i);

	for (i = 0; i < decon_cnt; ++i) {
		if (id == i || decon[i] == NULL)
			continue;

		for (j = 0; j < BTS_DPU_MAX; ++j)
			decon[i]->bts.ch_bw[id][j] = decon[id]->bts.ch_bw[id][j];
	}
}

static void dpu_bts_calc_dpp_bw(struct bts_decon_info *bts_info, int idx)
{
	struct bts_dpp_info *dpp = &bts_info->dpp[idx];
	unsigned int dst_w, dst_h;

	dst_w = dpp->dst.x2 - dpp->dst.x1;
	dst_h = dpp->dst.y2 - dpp->dst.y1;

	dpp->bw = ((u64)dpp->src_h * dpp->src_w * dpp->bpp * bts_info->vclk) /
		(8 * dst_h * bts_info->lcd_w);

	DPU_DEBUG_BTS("\tDPP%d bandwidth = %d\n", idx, dpp->bw);
}

void dpu_bts_calc_bw(struct decon_device *decon, struct decon_reg_data *regs)
{
	struct decon_win_config *config = regs->dpp_config;
	struct bts_decon_info bts_info;
	const struct dpu_fmt *fmt_info;
	enum dpp_rotate rot;
	int idx, i;
	u32 total_bw = 0;

	if (!decon->bts.enabled)
		return;

	DPU_DEBUG_BTS("\n");
	DPU_DEBUG_BTS("%s + : DECON%d\n", __func__, decon->id);

	memset(&bts_info, 0, sizeof(struct bts_decon_info));

	bts_info.vclk = decon->bts.resol_clk;
	bts_info.lcd_w = decon->lcd_info->xres;
	bts_info.lcd_h = decon->lcd_info->yres;

	for (i = 0; i < decon->dt.max_win; ++i) {
		if (config[i].state == DECON_WIN_STATE_BUFFER) {
			idx = config[i].channel; /* ch */
			/*
			 * TODO: Array index of bts_info structure uses dma type.
			 * This array index will be changed to DPP channel number
			 * in the future.
			 */
			bts_info.dpp[idx].used = true;
		} else {
			continue;
		}

		fmt_info = dpu_find_fmt_info(config[i].format);
		bts_info.dpp[idx].bpp = fmt_info->bpp + fmt_info->padding;
		bts_info.dpp[idx].src_w = config[i].src.w;
		bts_info.dpp[idx].src_h = config[i].src.h;
		bts_info.dpp[idx].dst.x1 = config[i].dst.x;
		bts_info.dpp[idx].dst.x2 = config[i].dst.x + config[i].dst.w;
		bts_info.dpp[idx].dst.y1 = config[i].dst.y;
		bts_info.dpp[idx].dst.y2 = config[i].dst.y + config[i].dst.h;
		rot = config[i].dpp_parm.rot;
		bts_info.dpp[idx].rotation = (rot > DPP_ROT_180) ? true : false;

		/*
		 * [ GUIDE : #if 0 ]
		 * Need to apply twice instead of x1.25 as many bandwidth
		 * of 8-bit YUV if it is a 8P2 format rotation.
		 *
		 * [ APPLY : #else ]
		 * In case of rotation, MIF & INT and DISP clock frequencies
		 * are important factors related to the issue of underrun.
		 * So, relatively high frequency is required to avoid underrun.
		 * By using 32 instead of 12/15/24 as bpp(bit-per-pixel) value,
		 * MIF & INT frequency can be increased because
		 * those are decided by the bandwidth.
		 * ROTATION_FACTOR_BPP(= ARGB8888 value) is a tunable value.
		 */
		if (bts_info.dpp[idx].rotation) {
			#if 0
			if (fmt == DECON_PIXEL_FORMAT_NV12M_S10B ||
				fmt == DECON_PIXEL_FORMAT_NV21M_S10B)
					bts_info.dpp[idx].bpp = 24;
			#else
			bts_info.dpp[idx].bpp = ROTATION_FACTOR_BPP;
			#endif
		}

		DPU_DEBUG_BTS("\tDPP%d : bpp(%d) src w(%d) h(%d) rot(%d) fmt(%s)\n",
				idx, bts_info.dpp[idx].bpp,
				bts_info.dpp[idx].src_w, bts_info.dpp[idx].src_h,
				bts_info.dpp[idx].rotation,
				fmt_info->name);
				
		DPU_DEBUG_BTS("\t\t\t\tdst x(%d) right(%d) y(%d) bottom(%d)\n",
				bts_info.dpp[idx].dst.x1,
				bts_info.dpp[idx].dst.x2,
				bts_info.dpp[idx].dst.y1,
				bts_info.dpp[idx].dst.y2);

		dpu_bts_calc_dpp_bw(&bts_info, idx);
		total_bw += bts_info.dpp[idx].bw;
	}

	decon->bts.total_bw = total_bw;
	memcpy(&decon->bts.bts_info, &bts_info, sizeof(struct bts_decon_info));

	for (i = 0; i < BTS_DPP_MAX; ++i)
		decon->bts.bw[i].val = bts_info.dpp[i].bw;

	DPU_DEBUG_BTS("\tDECON%d total bandwidth = %d\n", decon->id,
			decon->bts.total_bw);

	dpu_bts_find_max_disp_freq(decon, regs);

	/* update bw for other decons */
	dpu_bts_share_bw_info(decon->id);

	DPU_DEBUG_BTS("%s -\n", __func__);
}

void dpu_bts_update_bw(struct decon_device *decon, struct decon_reg_data *regs,
		u32 is_after)
{
	struct bts_bw bw = { 0, };
#if defined(CONFIG_EXYNOS_DISPLAYPORT)
	struct displayport_device *displayport = get_displayport_drvdata();
	videoformat cur = V640X480P60;
	__u64 pixelclock = 0;
	u32 sst_id = SST1;

	if (decon->dt.out_type == DECON_OUT_DP) {
		sst_id = displayport_get_sst_id_with_decon_id(decon->id);
		cur = displayport->sst[sst_id]->cur_video;
		pixelclock = supported_videos[cur].dv_timings.bt.pixelclock;
	}
#endif

	DPU_DEBUG_BTS("%s +\n", __func__);

	if (!decon->bts.enabled)
		return;

	/* update peak & read bandwidth per DPU port */
	bw.peak = decon->bts.peak;
	bw.read = decon->bts.total_bw;
	DPU_DEBUG_BTS("\tpeak = %d, read = %d\n", bw.peak, bw.read);

	if (bw.read == 0)
		bw.peak = 0;

	if (is_after) { /* after DECON h/w configuration */
		if (decon->bts.total_bw <= decon->bts.prev_total_bw)
			bts_update_bw(decon->bts.bw_idx, bw);

#if defined(CONFIG_EXYNOS_DISPLAYPORT)
		if ((displayport->sst[sst_id]->state == DISPLAYPORT_STATE_ON)
			&& (pixelclock >= 533000000)) /* 4K DP case */
			return;
#endif

		if (decon->bts.max_disp_freq <= decon->bts.prev_max_disp_freq)
			pm_qos_update_request(&decon->bts.disp_qos,
					decon->bts.max_disp_freq);

		decon->bts.prev_total_bw = decon->bts.total_bw;
		decon->bts.prev_max_disp_freq = decon->bts.max_disp_freq;
	} else {
		if (decon->bts.total_bw > decon->bts.prev_total_bw)
			bts_update_bw(decon->bts.bw_idx, bw);

#if defined(CONFIG_EXYNOS_DISPLAYPORT)
		if ((displayport->sst[sst_id]->state == DISPLAYPORT_STATE_ON)
			&& (pixelclock >= 533000000)) /* 4K DP case */
			return;
#endif

		if (decon->bts.max_disp_freq > decon->bts.prev_max_disp_freq)
			pm_qos_update_request(&decon->bts.disp_qos,
					decon->bts.max_disp_freq);
	}

	DPU_DEBUG_BTS("%s -\n", __func__);
}

void dpu_bts_acquire_bw(struct decon_device *decon)
{
#if defined(CONFIG_DECON_BTS_LEGACY) && defined(CONFIG_EXYNOS_DISPLAYPORT)
	struct displayport_device *displayport = get_displayport_drvdata();
	videoformat cur = V640X480P60;
	__u64 pixelclock = 0;
	u32 sst_id = SST1;
#endif
	struct decon_win_config config;
	u64 resol_clock;
	u32 aclk_freq = 0;

#if defined(CONFIG_DECON_BTS_LEGACY) && defined(CONFIG_EXYNOS_DISPLAYPORT)
	if (decon->dt.out_type == DECON_OUT_DP) {
		sst_id = displayport_get_sst_id_with_decon_id(decon->id);
		cur = displayport->sst[sst_id]->cur_video;
		pixelclock = supported_videos[cur].dv_timings.bt.pixelclock;
	}
#endif

	DPU_DEBUG_BTS("%s +\n", __func__);

	if (!decon->bts.enabled)
		return;

	if (decon->dt.out_type == DECON_OUT_DSI) {
		memset(&config, 0, sizeof(struct decon_win_config));
		config.src.w = config.dst.w = decon->lcd_info->xres;
		config.src.h = config.dst.h = decon->lcd_info->yres;
		resol_clock = decon->lcd_info->xres * decon->lcd_info->yres *
			decon->lcd_info->fps * 11 / 10 / 1000 + 1;

		aclk_freq = dpu_bts_calc_aclk_disp(decon, &config, resol_clock);
		DPU_DEBUG_BTS("Initial calculated disp freq(%lu) @%d fps\n",
			aclk_freq, decon->lcd_info->fps);
		
		/*
		 * If current disp freq is higher than calculated freq,
		 * it must not be set. if not, underrun can occur.
		 */
		if (exynos_devfreq_get_domain_freq(DEVFREQ_DISP) < aclk_freq)
			pm_qos_update_request(&decon->bts.disp_qos, aclk_freq);

		DPU_DEBUG_BTS("Get initial disp freq(%lu)\n",
				exynos_devfreq_get_domain_freq(DEVFREQ_DISP));

		return;
	}

#if defined(CONFIG_DECON_BTS_LEGACY) && defined(CONFIG_EXYNOS_DISPLAYPORT)
	if (decon->dt.out_type != DECON_OUT_DP)
		return;

	if (pixelclock >= 533000000) {
		if (pm_qos_request_active(&decon->bts.mif_qos))
			pm_qos_update_request(&decon->bts.mif_qos, 1794 * 1000);
		else
			DPU_ERR_BTS("%s mif qos setting error\n", __func__);

		if (pm_qos_request_active(&decon->bts.int_qos))
			pm_qos_update_request(&decon->bts.int_qos, 534 * 1000);
		else
			DPU_ERR_BTS("%s int qos setting error\n", __func__);

		if (pm_qos_request_active(&decon->bts.disp_qos))
			pm_qos_update_request(&decon->bts.disp_qos, 400 * 1000);
		else
			DPU_ERR_BTS("%s int qos setting error\n", __func__);

		bts_add_scenario(decon->bts.scen_idx[DPU_BS_DP_DEFAULT]);
	} else if (pixelclock > 148500000) { /* pixelclock < 533000000 ? */
		if (pm_qos_request_active(&decon->bts.mif_qos))
			pm_qos_update_request(&decon->bts.mif_qos, 1352 * 1000);
		else
			DPU_ERR_BTS("%s mif qos setting error\n", __func__);
	} else { /* pixelclock <= 148500000 ? */
		if (pm_qos_request_active(&decon->bts.mif_qos))
			pm_qos_update_request(&decon->bts.mif_qos, 845 * 1000);
		else
			DPU_ERR_BTS("%s mif qos setting error\n", __func__);
	}

	DPU_DEBUG_BTS("%s: decon%d, pixelclock(%u)\n",
			__func__, decon->id, pixelclock);
#endif
}

void dpu_bts_release_bw(struct decon_device *decon)
{
	struct bts_bw bw = { 0, };
	DPU_DEBUG_BTS("%s +\n", __func__);

	if (!decon->bts.enabled)
		return;

	if (decon->dt.out_type == DECON_OUT_DSI) {
		bts_update_bw(decon->bts.bw_idx, bw);
		decon->bts.prev_total_bw = 0;
		pm_qos_update_request(&decon->bts.disp_qos, 0);
		decon->bts.prev_max_disp_freq = 0;
	} else if (decon->dt.out_type == DECON_OUT_DP) {
#if defined(CONFIG_DECON_BTS_LEGACY) && defined(CONFIG_EXYNOS_DISPLAYPORT)
		if (pm_qos_request_active(&decon->bts.mif_qos))
			pm_qos_update_request(&decon->bts.mif_qos, 0);
		else
			DPU_ERR_BTS("%s mif qos setting error\n", __func__);

		if (pm_qos_request_active(&decon->bts.int_qos))
			pm_qos_update_request(&decon->bts.int_qos, 0);
		else
			DPU_ERR_BTS("%s int qos setting error\n", __func__);

		if (pm_qos_request_active(&decon->bts.disp_qos))
			pm_qos_update_request(&decon->bts.disp_qos, 0);
		else
			DPU_ERR_BTS("%s int qos setting error\n", __func__);

		bts_del_scenario(decon->bts.scen_idx[DPU_BS_DP_DEFAULT]);
#endif
	}

	DPU_DEBUG_BTS("%s -\n", __func__);
}

void dpu_bts_init(struct decon_device *decon)
{
	int i;
	struct v4l2_subdev *sd = NULL;
	const char *scen_name[DPU_BS_MAX] = {
		"default",
		"mfc_uhd",
		"mfc_uhd_10bit",
		"dp_default",
		/* add scenario & update index of [decon_cal.h] */
	};

	DPU_DEBUG_BTS("%s +\n", __func__);

	decon->bts.enabled = false;

	if (!IS_ENABLED(CONFIG_EXYNOS_BTS)) {
		DPU_ERR_BTS("decon%d bts feature is disabled\n", decon->id);
		return;
	}

	if (decon->id == 1)
		decon->bts.bw_idx = bts_get_bwindex("DECON1");
	else if (decon->id == 2)
		decon->bts.bw_idx = bts_get_bwindex("DECON2");
	else
		decon->bts.bw_idx = bts_get_bwindex("DECON0");

	/*
	 * Get scenario index from BTS driver
	 * Don't try to get index value of "default" scenario
	 */
	for (i = 1; i < DPU_BS_MAX; i++) {
		if (scen_name[i] != NULL)
			decon->bts.scen_idx[i] =
				bts_get_scenindex(scen_name[i]);
	}

	for (i = 0; i < BTS_DPU_MAX; i++)
		decon->bts.ch_bw[decon->id][i] = 0;

	DPU_DEBUG_BTS("BTS_BW_TYPE(%d) -\n", decon->bts.bw_idx);

	if (decon->dt.out_type == DECON_OUT_DP) {
		/*
		* Decon2-DP : various resolutions are available
		* therefore, set max resolution clock at init phase to avoid underrun
		*/
		decon->bts.resol_clk = (u32)((u64)4096 * 2160 * 60 * 11
				/ 10 / 1000 + 1);
	} else {		
		/*
		+		 * Resol clock(KHZ) =
		+		 *	lcd width x lcd height x fps(refresh rate) x
		+		 *	1.1(10% margin) / 1000(for KHZ) + 1(for raising to a unit)
		*/
		decon->bts.resol_clk = (u32)((u64)decon->lcd_info->xres *
				(u64)decon->lcd_info->yres *
				decon->lcd_info->fps * 11 / 10 / 1000 + 1);
				
	}

	DPU_DEBUG_BTS("[Init: D%d] resol clock = %d Khz @%d fps\n",
		decon->id, decon->bts.resol_clk, decon->lcd_info->fps);

	pm_qos_add_request(&decon->bts.mif_qos, PM_QOS_BUS_THROUGHPUT, 0);
	pm_qos_add_request(&decon->bts.int_qos, PM_QOS_DEVICE_THROUGHPUT, 0);
	pm_qos_add_request(&decon->bts.disp_qos, PM_QOS_DISPLAY_THROUGHPUT, 0);
	decon->bts.scen_updated = 0;

	for (i = 0; i < BTS_DPP_MAX; ++i) {
		sd = decon->dpp_sd[i];
		v4l2_subdev_call(sd, core, ioctl, DPP_GET_PORT_NUM,
				&decon->bts.bw[i].ch_num);
		DPU_INFO_BTS(" CH(%d) Port(%d)\n", i,
				decon->bts.bw[i].ch_num);
	}

	decon->bts.enabled = true;

	DPU_INFO_BTS("decon%d bts feature is enabled\n", decon->id);
}

void dpu_bts_deinit(struct decon_device *decon)
{
	if (!decon->bts.enabled)
		return;

	DPU_DEBUG_BTS("%s +\n", __func__);
	pm_qos_remove_request(&decon->bts.disp_qos);
	pm_qos_remove_request(&decon->bts.int_qos);
	pm_qos_remove_request(&decon->bts.mif_qos);
	DPU_DEBUG_BTS("%s -\n", __func__);
}

struct decon_bts_ops decon_bts_control = {
	.bts_init		= dpu_bts_init,
	.bts_calc_bw		= dpu_bts_calc_bw,
	.bts_update_bw		= dpu_bts_update_bw,
	.bts_acquire_bw		= dpu_bts_acquire_bw,
	.bts_release_bw		= dpu_bts_release_bw,
	.bts_deinit		= dpu_bts_deinit,
};
