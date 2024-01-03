/*
 * linux/drivers/video/fbdev/exynos/panel/s6e3fc3/s6e3fc3_r8s_panel.h
 *
 * Header file for S6E3FC3 Dimming Driver
 *
 * Copyright (c) 2016 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __S6E3FC3_R8S_PANEL_H__
#define __S6E3FC3_R8S_PANEL_H__

#include "../panel.h"
#include "../panel_drv.h"
#include "s6e3fc3.h"
#include "s6e3fc3_dimming.h"
#ifdef CONFIG_EXYNOS_DECON_MDNIE_LITE
#include "s6e3fc3_r8s_panel_mdnie.h"
#endif
#include "s6e3fc3_r8s_panel_dimming.h"
#ifdef CONFIG_SUPPORT_AOD_BL
#include "s6e3fc3_r8s_panel_aod_dimming.h"
#endif

#ifdef CONFIG_DYNAMIC_FREQ
#include "s6e3fc3_r8s_df_tbl.h"
#endif
#include "s6e3fc3_r8s_resol.h"

#undef __pn_name__
#define __pn_name__	r8s

#undef __PN_NAME__
#define __PN_NAME__

/* ===================================================================================== */
/* ============================= [S6E3FC3 READ INFO TABLE] ============================= */
/* ===================================================================================== */
/* <READINFO TABLE> IS DEPENDENT ON LDI. IF YOU NEED, DEFINE PANEL's RESOURCE TABLE */


/* ===================================================================================== */
/* ============================= [S6E3FC3 RESOURCE TABLE] ============================== */
/* ===================================================================================== */
/* <RESOURCE TABLE> IS DEPENDENT ON LDI. IF YOU NEED, DEFINE PANEL's RESOURCE TABLE */


/* ===================================================================================== */
/* ============================== [S6E3FC3 MAPPING TABLE] ============================== */
/* ===================================================================================== */

static u8 r8s_brt_table[S6E3FC3_TOTAL_STEP][2] = {
	{0x00, 0x02}, {0x00, 0x02}, {0x00, 0x05}, {0x00, 0x05}, {0x00, 0x08}, {0x00, 0x08}, {0x00, 0x0B}, {0x00, 0x0B}, {0x00, 0x0E}, {0x00, 0x0E},
	{0x00, 0x11}, {0x00, 0x14}, {0x00, 0x18}, {0x00, 0x18}, {0x00, 0x1B}, {0x00, 0x1D}, {0x00, 0x1F}, {0x00, 0x22}, {0x00, 0x24}, {0x00, 0x27},
	{0x00, 0x29}, {0x00, 0x2C}, {0x00, 0x2E}, {0x00, 0x31}, {0x00, 0x34}, {0x00, 0x36}, {0x00, 0x39}, {0x00, 0x3C}, {0x00, 0x3F}, {0x00, 0x42},
	{0x00, 0x44}, {0x00, 0x47}, {0x00, 0x4A}, {0x00, 0x4D}, {0x00, 0x50}, {0x00, 0x53}, {0x00, 0x56}, {0x00, 0x59}, {0x00, 0x5C}, {0x00, 0x5F},
	{0x00, 0x62}, {0x00, 0x66}, {0x00, 0x69}, {0x00, 0x6C}, {0x00, 0x6F}, {0x00, 0x72}, {0x00, 0x76}, {0x00, 0x79}, {0x00, 0x7C}, {0x00, 0x80},
	{0x00, 0x83}, {0x00, 0x86}, {0x00, 0x8A}, {0x00, 0x8D}, {0x00, 0x91}, {0x00, 0x94}, {0x00, 0x97}, {0x00, 0x9B}, {0x00, 0x9E}, {0x00, 0xA2},
	{0x00, 0xA5}, {0x00, 0xA9}, {0x00, 0xAD}, {0x00, 0xB0}, {0x00, 0xB4}, {0x00, 0xB7}, {0x00, 0xBB}, {0x00, 0xBF}, {0x00, 0xC2}, {0x00, 0xC6},
	{0x00, 0xCA}, {0x00, 0xCD}, {0x00, 0xD1}, {0x00, 0xD5}, {0x00, 0xD9}, {0x00, 0xDC}, {0x00, 0xE0}, {0x00, 0xE4}, {0x00, 0xE8}, {0x00, 0xEC},
	{0x00, 0xF0}, {0x00, 0xF3}, {0x00, 0xF7}, {0x00, 0xFB}, {0x00, 0xFF}, {0x01, 0x03}, {0x01, 0x07}, {0x01, 0x0B}, {0x01, 0x0F}, {0x01, 0x13},
	{0x01, 0x17}, {0x01, 0x1B}, {0x01, 0x1F}, {0x01, 0x23}, {0x01, 0x27}, {0x01, 0x2B}, {0x01, 0x2F}, {0x01, 0x33}, {0x01, 0x37}, {0x01, 0x3B},
	{0x01, 0x3F}, {0x01, 0x44}, {0x01, 0x48}, {0x01, 0x4C}, {0x01, 0x50}, {0x01, 0x54}, {0x01, 0x58}, {0x01, 0x5D}, {0x01, 0x61}, {0x01, 0x65},
	{0x01, 0x69}, {0x01, 0x6E}, {0x01, 0x72}, {0x01, 0x76}, {0x01, 0x7A}, {0x01, 0x7F}, {0x01, 0x83}, {0x01, 0x87}, {0x01, 0x8C}, {0x01, 0x90},
	{0x01, 0x94}, {0x01, 0x99}, {0x01, 0x9D}, {0x01, 0xA2}, {0x01, 0xA6}, {0x01, 0xAA}, {0x01, 0xAF}, {0x01, 0xB3}, {0x01, 0xB8}, {0x01, 0xBC},
	{0x01, 0xC1}, {0x01, 0xC5}, {0x01, 0xC9}, {0x01, 0xCE}, {0x01, 0xD2}, {0x01, 0xD6}, {0x01, 0xDB}, {0x01, 0xDF}, {0x01, 0xE3}, {0x01, 0xE7},
	{0x01, 0xEC}, {0x01, 0xF0}, {0x01, 0xF4}, {0x01, 0xF9}, {0x01, 0xFD}, {0x02, 0x01}, {0x02, 0x06}, {0x02, 0x0A}, {0x02, 0x0F}, {0x02, 0x13},
	{0x02, 0x17}, {0x02, 0x1C}, {0x02, 0x20}, {0x02, 0x25}, {0x02, 0x29}, {0x02, 0x2E}, {0x02, 0x32}, {0x02, 0x36}, {0x02, 0x3B}, {0x02, 0x3F},
	{0x02, 0x44}, {0x02, 0x48}, {0x02, 0x4D}, {0x02, 0x51}, {0x02, 0x56}, {0x02, 0x5B}, {0x02, 0x5F}, {0x02, 0x64}, {0x02, 0x68}, {0x02, 0x6D},
	{0x02, 0x71}, {0x02, 0x76}, {0x02, 0x7B}, {0x02, 0x7F}, {0x02, 0x84}, {0x02, 0x88}, {0x02, 0x8D}, {0x02, 0x92}, {0x02, 0x96}, {0x02, 0x9B},
	{0x02, 0xA0}, {0x02, 0xA4}, {0x02, 0xA9}, {0x02, 0xAE}, {0x02, 0xB2}, {0x02, 0xB7}, {0x02, 0xBC}, {0x02, 0xC0}, {0x02, 0xC5}, {0x02, 0xCA},
	{0x02, 0xCE}, {0x02, 0xD3}, {0x02, 0xD8}, {0x02, 0xDD}, {0x02, 0xE1}, {0x02, 0xE6}, {0x02, 0xEB}, {0x02, 0xF0}, {0x02, 0xF5}, {0x02, 0xF9},
	{0x02, 0xFE}, {0x03, 0x03}, {0x03, 0x08}, {0x03, 0x0D}, {0x03, 0x12}, {0x03, 0x16}, {0x03, 0x1B}, {0x03, 0x20}, {0x03, 0x25}, {0x03, 0x2A},
	{0x03, 0x2F}, {0x03, 0x34}, {0x03, 0x38}, {0x03, 0x3D}, {0x03, 0x42}, {0x03, 0x47}, {0x03, 0x4C}, {0x03, 0x4F}, {0x03, 0x54}, {0x03, 0x58},
	{0x03, 0x5C}, {0x03, 0x60}, {0x03, 0x65}, {0x03, 0x69}, {0x03, 0x6E}, {0x03, 0x72}, {0x03, 0x77}, {0x03, 0x7C}, {0x03, 0x81}, {0x03, 0x87},
	{0x03, 0x8D}, {0x03, 0x91}, {0x03, 0x96}, {0x03, 0x9B}, {0x03, 0xA0}, {0x03, 0xA4}, {0x03, 0xA8}, {0x03, 0xAC}, {0x03, 0xB0}, {0x03, 0xB5},
	{0x03, 0xBA}, {0x03, 0xBF}, {0x03, 0xC3}, {0x03, 0xC8}, {0x03, 0xCD}, {0x03, 0xD2}, {0x03, 0xD6}, {0x03, 0xDC}, {0x03, 0xE1}, {0x03, 0xE7},
	{0x03, 0xEC}, {0x03, 0xF0}, {0x03, 0xF4}, {0x03, 0xF7}, {0x03, 0xFB}, {0x03, 0xFF}, {0x00, 0x02}, {0x00, 0x04}, {0x00, 0x06}, {0x00, 0x09},
	{0x00, 0x0C}, {0x00, 0x0E}, {0x00, 0x11}, {0x00, 0x13}, {0x00, 0x15}, {0x00, 0x18}, {0x00, 0x1B}, {0x00, 0x1D}, {0x00, 0x20}, {0x00, 0x22},
	{0x00, 0x24}, {0x00, 0x27}, {0x00, 0x2A}, {0x00, 0x2C}, {0x00, 0x2F}, {0x00, 0x31}, {0x00, 0x33}, {0x00, 0x36}, {0x00, 0x39}, {0x00, 0x3B},
	{0x00, 0x3E}, {0x00, 0x40}, {0x00, 0x42}, {0x00, 0x45}, {0x00, 0x48}, {0x00, 0x4A}, {0x00, 0x4D}, {0x00, 0x4F}, {0x00, 0x51}, {0x00, 0x54},
	{0x00, 0x57}, {0x00, 0x59}, {0x00, 0x5C}, {0x00, 0x5E}, {0x00, 0x60}, {0x00, 0x63}, {0x00, 0x66}, {0x00, 0x68}, {0x00, 0x6A}, {0x00, 0x6D},
	{0x00, 0x6F}, {0x00, 0x72}, {0x00, 0x75}, {0x00, 0x77}, {0x00, 0x79}, {0x00, 0x7C}, {0x00, 0x7E}, {0x00, 0x81}, {0x00, 0x84}, {0x00, 0x86},
	{0x00, 0x88}, {0x00, 0x8B}, {0x00, 0x8D}, {0x00, 0x90}, {0x00, 0x93}, {0x00, 0x95}, {0x00, 0x97}, {0x00, 0x9A}, {0x00, 0x9C}, {0x00, 0x9F},
	{0x00, 0xA2}, {0x00, 0xA4}, {0x00, 0xA6}, {0x00, 0xA9}, {0x00, 0xAB}, {0x00, 0xAE}, {0x00, 0xB1}, {0x00, 0xB3}, {0x00, 0xB5}, {0x00, 0xB8},
	{0x00, 0xBA}, {0x00, 0xBD}, {0x00, 0xC0}, {0x00, 0xC2}, {0x00, 0xC4}, {0x00, 0xC7}, {0x00, 0xC9}, {0x00, 0xCC}, {0x00, 0xCF}, {0x00, 0xD1},
	{0x00, 0xD3}, {0x00, 0xD6}, {0x00, 0xD8}, {0x00, 0xDB}, {0x00, 0xDE}, {0x00, 0xE0}, {0x00, 0xE2}, {0x00, 0xE5}, {0x00, 0xE7}, {0x00, 0xEA},
	{0x00, 0xED}, {0x00, 0xEF}, {0x00, 0xF1}, {0x00, 0xF4}, {0x00, 0xF6}, {0x00, 0xF9}, {0x00, 0xFC}, {0x00, 0xFE}, {0x01, 0x00}, {0x01, 0x03},
	{0x01, 0x05}, {0x01, 0x08}, {0x01, 0x0B}, {0x01, 0x0D}, {0x01, 0x0F}, {0x01, 0x12}, {0x01, 0x14}, {0x01, 0x17}, {0x01, 0x19}, {0x01, 0x1C},
	{0x01, 0x1E}, {0x01, 0x20}, {0x01, 0x23}, {0x01, 0x26}, {0x01, 0x28}, {0x01, 0x2B}, {0x01, 0x2D}, {0x01, 0x2F}, {0x01, 0x32}, {0x01, 0x35},
	{0x01, 0x37}, {0x01, 0x3A}, {0x01, 0x3C}, {0x01, 0x3E}, {0x01, 0x41}, {0x01, 0x44}, {0x01, 0x46}, {0x01, 0x49}, {0x01, 0x4B}, {0x01, 0x4D},
	{0x01, 0x50}, {0x01, 0x53}, {0x01, 0x55}, {0x01, 0x58}, {0x01, 0x5A}, {0x01, 0x5C}, {0x01, 0x5F}, {0x01, 0x62}, {0x01, 0x64}, {0x01, 0x67},
	{0x01, 0x69}, {0x01, 0x6B}, {0x01, 0x6E}, {0x01, 0x71}, {0x01, 0x73}, {0x01, 0x76}, {0x01, 0x78}, {0x01, 0x7A}, {0x01, 0x7D}, {0x01, 0x80},
	{0x01, 0x82}, {0x01, 0x85}, {0x01, 0x87}, {0x01, 0x89}, {0x01, 0x8C}, {0x01, 0x8F}, {0x01, 0x91}, {0x01, 0x94}, {0x01, 0x96}, {0x01, 0x98},
	{0x01, 0x9B}, {0x01, 0x9E}, {0x01, 0xA0}, {0x01, 0xA3}, {0x01, 0xA5}, {0x01, 0xA7}, {0x01, 0xAA}, {0x01, 0xAD}, {0x01, 0xAF}, {0x01, 0xB2},
	{0x01, 0xB4}, {0x01, 0xB6}, {0x01, 0xB9}, {0x01, 0xBC}, {0x01, 0xBE}, {0x01, 0xC1}, {0x01, 0xC3}, {0x01, 0xC5}, {0x01, 0xC8}, {0x01, 0xCB},
	{0x01, 0xCD}, {0x01, 0xD0}, {0x01, 0xD2}, {0x01, 0xD4}, {0x01, 0xD7}, {0x01, 0xDA}, {0x01, 0xDC}, {0x01, 0xDF}, {0x01, 0xE1}, {0x01, 0xE3},
	{0x01, 0xE6}, {0x01, 0xE9}, {0x01, 0xEB}, {0x01, 0xEE}, {0x01, 0xF0}, {0x01, 0xF2}, {0x01, 0xF5}, {0x01, 0xF8}, {0x01, 0xFA}, {0x01, 0xFD},
	{0x01, 0xFF}, {0x02, 0x01}, {0x02, 0x04}, {0x02, 0x07}, {0x02, 0x09}, {0x02, 0x0C}, {0x02, 0x0E}, {0x02, 0x10}, {0x02, 0x13}, {0x02, 0x16},
	{0x02, 0x18}, {0x02, 0x1B}, {0x02, 0x1D}, {0x02, 0x1F}, {0x02, 0x22}, {0x02, 0x25}, {0x02, 0x27}, {0x02, 0x2A}, {0x02, 0x2C}, {0x02, 0x2E},
	{0x02, 0x31}, {0x02, 0x34}, {0x02, 0x36}, {0x02, 0x39}, {0x02, 0x3B}, {0x02, 0x3D}, {0x02, 0x40},
};

static u8 r8s_elvss_table[S6E3FC3_TOTAL_STEP][1] = {
	/* OVER_ZERO */
	[0 ... 255] = { 0x16 },
	/* HBM */
	[256 ... 270] = { 0x26 },
	[271 ... 283] = { 0x25 },
	[284 ... 297] = { 0x24 },
	[298 ... 310] = { 0x23 },
	[311 ... 324] = { 0x22 },
	[325 ... 337] = { 0x21 },
	[338 ... 351] = { 0x20 },
	[352 ... 364] = { 0x1F },
	[365 ... 378] = { 0x1E },
	[379 ... 391] = { 0x1D },
	[392 ... 405] = { 0x1C },
	[406 ... 418] = { 0x1B },
	[419 ... 432] = { 0x1A },
	[433 ... 445] = { 0x19 },
	[446 ... 459] = { 0x18 },
	[460 ... 472] = { 0x17 },
	[473 ... 486] = { 0x16 },
};

static u8 r8s_hbm_transition_table[MAX_PANEL_HBM][SMOOTH_TRANS_MAX][1] = {
	/* HBM off */
	{
		/* Normal */
		{ 0x20 },
		/* Smooth */
		{ 0x28 },
	},
	/* HBM on */
	{
		/* Normal */
		{ 0xE0 },
		/* Smooth */
		{ 0xE0 }, /* R8 no smooth in HBM */
	}
};

static u8 r8s_acl_frame_avg_table[][1] = {
	{ 0x44 }, /* 16 Frame Avg */
	{ 0x55 }, /* 32 Frame Avg */
};

static u8 r8s_acl_start_point_table[][2] = {
	{ 0x00, 0xB0 }, /* 50 Percent */
	{ 0x40, 0x28 }, /* 60 Percent */
};

static u8 r8s_acl_dim_speed_table[][1] = {
	{ 0x00 }, /* 0x00 : ACL Dimming Off */
	{ 0x20 }, /* 0x20 : ACL Dimming 32 Frames */
};

static u8 r8s_acl_opr_table[ACL_OPR_MAX][1] = {
	{ 0x00 }, /* ACL OFF OPR */
	{ 0x01 }, /* ACL ON OPR_3 */
	{ 0x01 }, /* ACL ON OPR_6 */
	{ 0x01 }, /* ACL ON OPR_8 */
	{ 0x03 }, /* ACL ON OPR_12 */
	{ 0x03 }, /* ACL ON OPR_15 */
};

static u8 r8s_lpm_nit_table[4][1] = {
	/* LPM 2NIT: */
	{ 0x27 },
	/* LPM 10NIT */
	{ 0x26 },
	/* LPM 30NIT */
	{ 0x25  },
	/* LPM 60NIT */
	{ 0x24 },
};

static u8 r8s_lpm_on_table[2][1] = {
	[ALPM_MODE] = { 0x23 },
	[HLPM_MODE] = { 0x23 },
};

#ifdef CONFIG_SUPPORT_XTALK_MODE
static u8 r8s_vgh_table[][1] = {
	{ 0xC0 },	/* off 7.0 V */
	{ 0x60 },	/* on 6.2 V */
};
#endif

#if defined(CONFIG_SEC_FACTORY) && defined(CONFIG_SUPPORT_FAST_DISCHARGE)
static u8 r8s_fast_discharge_table[][1] = {
	{ 0x80 },	//fd off
	{ 0x40 },	//fd on
};
#endif


static u8 r8s_fps_table_1[][1] = {
	[S6E3FC3_VRR_FPS_120] = { 0x08 },
	[S6E3FC3_VRR_FPS_60] = { 0x00 },
};

static u8 r8s_dimming_speep_table_1[][1] = {
	[S6E3FC3_SMOOTH_DIMMING_OFF] = { 0x20 },
	[S6E3FC3_SMOOTH_DIMMING_ON] = { 0x60 },
};

#ifdef CONFIG_DYNAMIC_FREQ
/* waring dummy */
static u8 r8s_dyn_ffc_table_1[][4] = {
	/* A: 1110  B: 1114  C: 1124 */
	{
		/* 1110 */
		0x0D, 0x10, 0x80, 0x45
	},
	{
		/* 1114 */
		0x0D, 0x10, 0x80, 0x45
	},
	{
		/* 1124 */
		0x0D, 0x10, 0x80, 0x45
	},
};

static u8 r8s_dyn_ffc_table_2[][2] = {
	/*	A: 1110  B: 1114  C: 1124	*/
	{
		/* 1110 */
		0x53, 0xA0
	},
	{
		/* 1114 */
		0x53, 0x54
	},
	{
		/* 1124 */
		0x52, 0x96
	},
};
#endif

static struct maptbl r8s_maptbl[MAX_MAPTBL] = {
	[GAMMA_MODE2_MAPTBL] = DEFINE_2D_MAPTBL(r8s_brt_table, init_gamma_mode2_brt_table, getidx_gamma_mode2_brt_table, copy_common_maptbl),
	[HBM_ONOFF_MAPTBL] = DEFINE_3D_MAPTBL(r8s_hbm_transition_table, init_common_table, getidx_hbm_transition_table, copy_common_maptbl),

	[ACL_FRAME_AVG_MAPTBL] = DEFINE_2D_MAPTBL(r8s_acl_frame_avg_table, init_common_table, getidx_acl_onoff_table, copy_common_maptbl),
	[ACL_START_POINT_MAPTBL] = DEFINE_2D_MAPTBL(r8s_acl_start_point_table, init_common_table, getidx_hbm_onoff_table, copy_common_maptbl),
	[ACL_DIM_SPEED_MAPTBL] = DEFINE_2D_MAPTBL(r8s_acl_dim_speed_table, init_common_table, getidx_acl_dim_onoff_table, copy_common_maptbl),
	[ACL_OPR_MAPTBL] = DEFINE_2D_MAPTBL(r8s_acl_opr_table, init_common_table, getidx_acl_opr_table, copy_common_maptbl),

	[TSET_MAPTBL] = DEFINE_0D_MAPTBL(r8s_tset_table, init_common_table, NULL, copy_tset_maptbl),
	[LPM_NIT_MAPTBL] = DEFINE_2D_MAPTBL(r8s_lpm_nit_table, init_lpm_brt_table, getidx_lpm_brt_table, copy_common_maptbl),
#ifdef CONFIG_SUPPORT_XTALK_MODE
	[VGH_MAPTBL] = DEFINE_2D_MAPTBL(r8s_vgh_table, init_common_table, getidx_vgh_table, copy_common_maptbl),
#endif
#ifdef CONFIG_DYNAMIC_FREQ
	[DYN_FFC_1_MAPTBL] = DEFINE_2D_MAPTBL(r8s_dyn_ffc_table_1, init_common_table, getidx_dyn_ffc_table, copy_common_maptbl),
	[DYN_FFC_2_MAPTBL] = DEFINE_2D_MAPTBL(r8s_dyn_ffc_table_2, init_common_table, getidx_dyn_ffc_table, copy_common_maptbl),
#endif
#if defined(CONFIG_SEC_FACTORY) && defined(CONFIG_SUPPORT_FAST_DISCHARGE)
	[FAST_DISCHARGE_MAPTBL] = DEFINE_2D_MAPTBL(r8s_fast_discharge_table,
			init_common_table, getidx_fast_discharge_table, copy_common_maptbl),
#endif
	[FPS_MAPTBL_1] = DEFINE_2D_MAPTBL(r8s_fps_table_1, init_common_table, getidx_vrr_fps_table, copy_common_maptbl),
	[DIMMING_SPEED] = DEFINE_2D_MAPTBL(r8s_dimming_speep_table_1, init_common_table, getidx_smooth_transition_table, copy_common_maptbl),
};

/* ===================================================================================== */
/* ============================== [S6E3FC3 COMMAND TABLE] ============================== */
/* ===================================================================================== */
static u8 R8S_KEY1_ENABLE[] = { 0x9F, 0xA5, 0xA5 };
static u8 R8S_KEY2_ENABLE[] = { 0xF0, 0x5A, 0x5A };
static u8 R8S_KEY3_ENABLE[] = { 0xFC, 0x5A, 0x5A };

static u8 R8S_KEY1_DISABLE[] = { 0x9F, 0x5A, 0x5A };
static u8 R8S_KEY2_DISABLE[] = { 0xF0, 0xA5, 0xA5 };
static u8 R8S_KEY3_DISABLE[] = { 0xFC, 0xA5, 0xA5 };
static u8 R8S_SLEEP_OUT[] = { 0x11 };
static u8 R8S_SLEEP_IN[] = { 0x10 };
static u8 R8S_DISPLAY_OFF[] = { 0x28 };
static u8 R8S_DISPLAY_ON[] = { 0x29 };

static u8 R8S_MULTI_CMD_ENABLE[] = { 0x72, 0x2C, 0x21 };
static u8 R8S_MULTI_CMD_DISABLE[] = { 0x72, 0x2C, 0x01 };
static u8 R8S_MULTI_CMD_DUMMY[] = { 0x0A, 0x00 };

static u8 R8S_TE_ON[] = { 0x35, 0x00, 0x00 };
static u8 R8S_TE_OFFSET[] = { 0xB9, 0x01, 0x09, 0x58, 0x00, 0x0B};

static DEFINE_STATIC_PACKET(r8s_level1_key_enable, DSI_PKT_TYPE_WR, R8S_KEY1_ENABLE, 0);
static DEFINE_STATIC_PACKET(r8s_level2_key_enable, DSI_PKT_TYPE_WR, R8S_KEY2_ENABLE, 0);
static DEFINE_STATIC_PACKET(r8s_level3_key_enable, DSI_PKT_TYPE_WR, R8S_KEY3_ENABLE, 0);

static DEFINE_STATIC_PACKET(r8s_level1_key_disable, DSI_PKT_TYPE_WR, R8S_KEY1_DISABLE, 0);
static DEFINE_STATIC_PACKET(r8s_level2_key_disable, DSI_PKT_TYPE_WR, R8S_KEY2_DISABLE, 0);
static DEFINE_STATIC_PACKET(r8s_level3_key_disable, DSI_PKT_TYPE_WR, R8S_KEY3_DISABLE, 0);

static DEFINE_STATIC_PACKET(r8s_multi_cmd_enable, DSI_PKT_TYPE_WR, R8S_MULTI_CMD_ENABLE, 0);
static DEFINE_STATIC_PACKET(r8s_multi_cmd_disable, DSI_PKT_TYPE_WR, R8S_MULTI_CMD_DISABLE, 0);
static DEFINE_STATIC_PACKET(r8s_multi_cmd_dummy, DSI_PKT_TYPE_WR, R8S_MULTI_CMD_DUMMY, 0);

static DEFINE_STATIC_PACKET(r8s_sleep_out, DSI_PKT_TYPE_WR, R8S_SLEEP_OUT, 0);
static DEFINE_STATIC_PACKET(r8s_sleep_in, DSI_PKT_TYPE_WR, R8S_SLEEP_IN, 0);
static DEFINE_STATIC_PACKET(r8s_display_on, DSI_PKT_TYPE_WR, R8S_DISPLAY_ON, 0);
static DEFINE_STATIC_PACKET(r8s_display_off, DSI_PKT_TYPE_WR, R8S_DISPLAY_OFF, 0);

static DEFINE_STATIC_PACKET(r8s_te_offset, DSI_PKT_TYPE_WR, R8S_TE_OFFSET, 0);
static DEFINE_STATIC_PACKET(r8s_te_on, DSI_PKT_TYPE_WR, R8S_TE_ON, 0);

static u8 R8S_TSET_SET[] = {
	0xB5,
	0x00,
};
static DEFINE_PKTUI(r8s_tset_set, &r8s_maptbl[TSET_MAPTBL], 1);
static DEFINE_VARIABLE_PACKET(r8s_tset_set, DSI_PKT_TYPE_WR, R8S_TSET_SET, 0x00);


static u8 R8S_AOD_SETTING[] = { 0x91, 0x01, 0x01 };
static DEFINE_STATIC_PACKET(r8s_aod_setting, DSI_PKT_TYPE_WR, R8S_AOD_SETTING, 0x0);

static u8 R8S_VAINT_SETTING[] = { 0xF4, 0x28 };
static DEFINE_STATIC_PACKET(r8s_vaint_setting, DSI_PKT_TYPE_WR, R8S_VAINT_SETTING, 0x4C);

static u8 R8S_LPM_PORCH_0_ON[] = { 0xCB, 0x40, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x40 };
static DEFINE_STATIC_PACKET(r8s_lpm_porch_0_on, DSI_PKT_TYPE_WR, R8S_LPM_PORCH_0_ON, 0x188);

static u8 R8S_LPM_PORCH_1_ON[] = { 0xCB, 0x38, 0x00, 0x00, 0x00, 0x18, 0x03, 0x38 };
static DEFINE_STATIC_PACKET(r8s_lpm_porch_1_on, DSI_PKT_TYPE_WR, R8S_LPM_PORCH_1_ON, 0x1C0);

static u8 R8S_LPM_PORCH_2_ON[] = { 0xF2, 0x24, 0xA4};
static DEFINE_STATIC_PACKET(r8s_lpm_porch_2_on, DSI_PKT_TYPE_WR, R8S_LPM_PORCH_2_ON, 0x10);


static u8 R8S_SWIRE_NO_PULSE[] = { 0xB5, 0x00, 0x00, 0x00 };
static DEFINE_STATIC_PACKET(r8s_swire_no_pulse, DSI_PKT_TYPE_WR, R8S_SWIRE_NO_PULSE, 0x07);

static u8 R8S_LPM_PORCH_0_OFF[] = { 0xCB, 0x45, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x45 };
static DEFINE_STATIC_PACKET(r8s_lpm_porch_0_off, DSI_PKT_TYPE_WR, R8S_LPM_PORCH_0_OFF, 0x188);

static u8 R8S_LPM_PORCH_1_OFF[] = { 0xCB, 0x3D, 0x00, 0x00, 0x00, 0x18, 0x03, 0x3D };
static DEFINE_STATIC_PACKET(r8s_lpm_porch_1_off, DSI_PKT_TYPE_WR, R8S_LPM_PORCH_1_OFF, 0x1C0);

static u8 R8S_LPM_PORCH_2_OFF[] = { 0xF2, 0x26, 0xE4 };
static DEFINE_STATIC_PACKET(r8s_lpm_porch_2_off, DSI_PKT_TYPE_WR, R8S_LPM_PORCH_2_OFF, 0x10);

static u8 R8S_NORMAL_SETTING[] = { 0x91, 0x02, 0x01 };
static DEFINE_STATIC_PACKET(r8s_normal_setting, DSI_PKT_TYPE_WR, R8S_NORMAL_SETTING, 0x00);




static u8 R8S_LPM_NIT[] = { 0x53, 0x27 };
static DEFINE_PKTUI(r8s_lpm_nit, &r8s_maptbl[LPM_NIT_MAPTBL], 1);
static DEFINE_VARIABLE_PACKET(r8s_lpm_nit, DSI_PKT_TYPE_WR, R8S_LPM_NIT, 0x00);


#ifdef CONFIG_SUPPORT_MASK_LAYER
static DEFINE_PANEL_MDELAY(r8s_wait_1msec, 1);
static DEFINE_PANEL_MDELAY(r8s_wait_9msec, 9);
#endif
static DEFINE_PANEL_MDELAY(r8s_wait_10msec, 10);
static DEFINE_PANEL_MDELAY(r8s_wait_30msec, 30);
static DEFINE_PANEL_MDELAY(r8s_wait_17msec, 17);

static DEFINE_PANEL_MDELAY(r8s_wait_34msec, 34);

static DEFINE_PANEL_MDELAY(r8s_wait_sleep_out_90msec, 90);
static DEFINE_PANEL_MDELAY(r8s_wait_display_off, 20);

static DEFINE_PANEL_MDELAY(r8s_wait_sleep_in, 120);
static DEFINE_PANEL_UDELAY(r8s_wait_1usec, 1);

static DEFINE_PANEL_KEY(r8s_level1_key_enable, CMD_LEVEL_1, KEY_ENABLE, &PKTINFO(r8s_level1_key_enable));
static DEFINE_PANEL_KEY(r8s_level2_key_enable, CMD_LEVEL_2, KEY_ENABLE, &PKTINFO(r8s_level2_key_enable));
static DEFINE_PANEL_KEY(r8s_level3_key_enable, CMD_LEVEL_3, KEY_ENABLE, &PKTINFO(r8s_level3_key_enable));

static DEFINE_PANEL_KEY(r8s_level1_key_disable, CMD_LEVEL_1, KEY_DISABLE, &PKTINFO(r8s_level1_key_disable));
static DEFINE_PANEL_KEY(r8s_level2_key_disable, CMD_LEVEL_2, KEY_DISABLE, &PKTINFO(r8s_level2_key_disable));
static DEFINE_PANEL_KEY(r8s_level3_key_disable, CMD_LEVEL_3, KEY_DISABLE, &PKTINFO(r8s_level3_key_disable));


#ifdef CONFIG_SUPPORT_MASK_LAYER
static DEFINE_PANEL_VSYNC_DELAY(r8s_wait_1_vsync, 1);
static DEFINE_COND(r8s_cond_is_60hz, s6e3fc3_is_60hz);
static DEFINE_COND(r8s_cond_is_120hz, s6e3fc3_is_120hz);
#endif

static u8 R8S_HBM_TRANSITION[] = {
	0x53, 0x20
};
static DEFINE_PKTUI(r8s_hbm_transition, &r8s_maptbl[HBM_ONOFF_MAPTBL], 1);
static DEFINE_VARIABLE_PACKET(r8s_hbm_transition, DSI_PKT_TYPE_WR, R8S_HBM_TRANSITION, 0);

static u8 R8S_ACL_SET[] = {
	0x65,
	0x55, 0x00, 0xB0, 0x51, 0x66, 0x98, 0x15, 0x55,
	0x55, 0x55, 0x08, 0xF1, 0xC6, 0x48, 0x40, 0x00,
	0x20, 0x10, 0x09
};
static DECLARE_PKTUI(r8s_acl_set) = {
	{ .offset = 1, .maptbl = &r8s_maptbl[ACL_FRAME_AVG_MAPTBL] },
	{ .offset = 2, .maptbl = &r8s_maptbl[ACL_START_POINT_MAPTBL] },
	{ .offset = 17, .maptbl = &r8s_maptbl[ACL_DIM_SPEED_MAPTBL] },
};
static DEFINE_VARIABLE_PACKET(r8s_acl_set, DSI_PKT_TYPE_WR, R8S_ACL_SET, 0x3B3);

static u8 R8S_ACL[] = {
	0x55, 0x02
};
static DEFINE_PKTUI(r8s_acl_control, &r8s_maptbl[ACL_OPR_MAPTBL], 1);
static DEFINE_VARIABLE_PACKET(r8s_acl_control, DSI_PKT_TYPE_WR, R8S_ACL, 0);

static u8 R8S_WRDISBV[] = {
	0x51, 0x03, 0xFF
};
static DEFINE_PKTUI(r8s_wrdisbv, &r8s_maptbl[GAMMA_MODE2_MAPTBL], 1);
static DEFINE_VARIABLE_PACKET(r8s_wrdisbv, DSI_PKT_TYPE_WR, R8S_WRDISBV, 0);

static u8 R8S_CASET[] = { 0x2A, 0x00, 0x00, 0x04, 0x37 };
static u8 R8S_PASET[] = { 0x2B, 0x00, 0x00, 0x09, 0x5F };
static DEFINE_STATIC_PACKET(r8s_caset, DSI_PKT_TYPE_WR, R8S_CASET, 0);
static DEFINE_STATIC_PACKET(r8s_paset, DSI_PKT_TYPE_WR, R8S_PASET, 0);

#ifdef CONFIG_DYNAMIC_FREQ
static u8 R8S_FFC_SET_1[] = {
	0xC5,
	0x0D, 0x10, 0x80, 0x45
};
static DEFINE_PKTUI(r8s_ffc_set_1, &r8s_maptbl[DYN_FFC_1_MAPTBL], 1);
static DEFINE_VARIABLE_PACKET(r8s_ffc_set_1, DSI_PKT_TYPE_WR, R8S_FFC_SET_1, 0x2A);

static u8 R8S_FFC_SET_2[] = {
	0xC5,
	0x53, 0xC7
};
static DEFINE_PKTUI(r8s_ffc_set_2, &r8s_maptbl[DYN_FFC_2_MAPTBL], 1);
static DEFINE_VARIABLE_PACKET(r8s_ffc_set_2, DSI_PKT_TYPE_WR, R8S_FFC_SET_2, 0x2E);

static u8 R8S_FFC_OFF_1[] = {
	0xC5,
	0x0D, 0x0C
};
static DEFINE_STATIC_PACKET(r8s_ffc_off_1, DSI_PKT_TYPE_WR, R8S_FFC_OFF_1, 0x2A);

static u8 R8S_FFC_OFF_2[] = {
	0xFE,
	0xB0,
};
static DEFINE_STATIC_PACKET(r8s_ffc_off_2, DSI_PKT_TYPE_WR, R8S_FFC_OFF_2, 0);

static u8 R8S_FFC_OFF_3[] = {
	0xFE,
	0x30,
};
static DEFINE_STATIC_PACKET(r8s_ffc_off_3, DSI_PKT_TYPE_WR, R8S_FFC_OFF_3, 0);

#endif

#if defined(CONFIG_SEC_FACTORY) && defined(CONFIG_SUPPORT_FAST_DISCHARGE)
static u8 R8S_FAST_DISCHARGE[] = {
	0xB5,
	0x40, 0x40	/* FD enable */
};
static DEFINE_PKTUI(r8s_fast_discharge, &r8s_maptbl[FAST_DISCHARGE_MAPTBL], 1);
static DEFINE_VARIABLE_PACKET(r8s_fast_discharge, DSI_PKT_TYPE_WR, R8S_FAST_DISCHARGE, 0x0A);
#endif

static u8 R8S_PCD_SET_DET_LOW[] = {
	0xCC,
	0x5C, 0x51	/* 1st 0x5C: default high, 2nd 0x51 : Enable SW RESET */
};
static DEFINE_STATIC_PACKET(r8s_pcd_det_set, DSI_PKT_TYPE_WR, R8S_PCD_SET_DET_LOW, 0);

static u8 R8S_ERR_FG_ON[] = {
	0xE5, 0x15
};
static DEFINE_STATIC_PACKET(r8s_err_fg_on, DSI_PKT_TYPE_WR, R8S_ERR_FG_ON, 0);

static u8 R8S_ERR_FG_OFF[] = {
	0xE5, 0x05
};
static DEFINE_STATIC_PACKET(r8s_err_fg_off, DSI_PKT_TYPE_WR, R8S_ERR_FG_OFF, 0);

static u8 R8S_ERR_FG_SETTING[] = {
	0xED,
	0x04, 0x4C, 0x20
	/* Vlin1, ELVDD, Vlin3 Monitor On */
	/* Defalut LOW */
};
static DEFINE_STATIC_PACKET(r8s_err_fg_setting, DSI_PKT_TYPE_WR, R8S_ERR_FG_SETTING, 0);

static u8 R8S_VSYNC_SET[] = {
	0xF2,
	0x54
};
static DEFINE_STATIC_PACKET(r8s_vsync_set, DSI_PKT_TYPE_WR, R8S_VSYNC_SET, 0x04);

static u8 R8S_FREQ_SET[] = {
	0xF2,
	0x00
};
static DEFINE_STATIC_PACKET(r8s_freq_set, DSI_PKT_TYPE_WR, R8S_FREQ_SET, 0x27);

static u8 R8S_PORCH_SET[] = {
	0xF2,
	0x55
};
static DEFINE_STATIC_PACKET(r8s_porch_set, DSI_PKT_TYPE_WR, R8S_PORCH_SET, 0x2E);

#ifdef CONFIG_SUPPORT_XTALK_MODE
static u8 R8S_XTALK_MODE[] = { 0xD9, 0x60 };
static DEFINE_PKTUI(r8s_xtalk_mode, &r8s_maptbl[VGH_MAPTBL], 1);
static DEFINE_VARIABLE_PACKET(r8s_xtalk_mode, DSI_PKT_TYPE_WR, R8S_XTALK_MODE, 0x1C);
#endif

static u8 R8S_FPS_1[] = { 0x60, 0x00, 0x00};
static DEFINE_PKTUI(r8s_fps_1, &r8s_maptbl[FPS_MAPTBL_1], 1);
static DEFINE_VARIABLE_PACKET(r8s_fps_1, DSI_PKT_TYPE_WR, R8S_FPS_1, 0);

static u8 R8S_DIMMING_SPEED[] = { 0x63, 0x00};
static DEFINE_PKTUI(r8s_dimming_speed, &r8s_maptbl[DIMMING_SPEED], 1);
static DEFINE_VARIABLE_PACKET(r8s_dimming_speed, DSI_PKT_TYPE_WR, R8S_DIMMING_SPEED, 0x91);

static u8 R8S_PANEL_UPDATE[] = {
	0xF7,
	0x0F
};
static DEFINE_STATIC_PACKET(r8s_panel_update, DSI_PKT_TYPE_WR, R8S_PANEL_UPDATE, 0);

static u8 R8S_GLOBAL_PARAM_SETTING[] = {
	0xF2,
	0x00, 0x05, 0x0E, 0x58, 0x54, 0x01, 0x0C, 0x00,
	0x04, 0x27, 0x24, 0x2F, 0xB0, 0x0C, 0x09, 0x74,
	0x26, 0xE4, 0x0C, 0x00, 0x04, 0x10, 0x00, 0x10,
	0x26, 0xA8, 0x10, 0x00, 0x10, 0x10, 0x34, 0x10,
	0x00, 0x40, 0x30, 0xC8, 0x00, 0xC8, 0x00, 0x00,
	0xCE
};
static DEFINE_STATIC_PACKET(r8s_global_param_setting, DSI_PKT_TYPE_WR, R8S_GLOBAL_PARAM_SETTING, 0);

static u8 R8S_MIN_ROI_SETTING[] = {
	0xC2,
	0x1B, 0x41, 0xB0, 0x0E, 0x00, 0x3C, 0x5A, 0x00,
	0x00
};
static DEFINE_STATIC_PACKET(r8s_min_roi_setting, DSI_PKT_TYPE_WR, R8S_MIN_ROI_SETTING, 0);


static u8 R8S_FOD_ENTER_1[] = {
	0x63, 0xF1, 0x00
};
static DEFINE_STATIC_PACKET(r8s_fod_enter_1, DSI_PKT_TYPE_WR, R8S_FOD_ENTER_1, 0x203);

static u8 R8S_FOD_ENTER_2[] = {
	0xB5, 0x14
};
static DEFINE_STATIC_PACKET(r8s_fod_enter_2, DSI_PKT_TYPE_WR, R8S_FOD_ENTER_2, 0);

static u8 R8S_FOD_ENTER_3[] = {
	0x63, 0xF0, 0x07
};
static DEFINE_STATIC_PACKET(r8s_fod_enter_3, DSI_PKT_TYPE_WR, R8S_FOD_ENTER_3, 0x203);

static u8 R8S_FOD_EXIT_1[] = {
	0x63, 0xF1, 0x07
};
static DEFINE_STATIC_PACKET(r8s_fod_exit_1, DSI_PKT_TYPE_WR, R8S_FOD_EXIT_1, 0x203);

static u8 R8S_FOD_EXIT_2[] = {
	0xB5, 0x14
};
static DEFINE_STATIC_PACKET(r8s_fod_exit_2, DSI_PKT_TYPE_WR, R8S_FOD_EXIT_2, 0);

static u8 R8S_FOD_EXIT_3[] = {
	0x63, 0xF0, 0x00
};
static DEFINE_STATIC_PACKET(r8s_fod_exit_3, DSI_PKT_TYPE_WR, R8S_FOD_EXIT_3, 0x203);

static u8 R8S_DSC[] = { 0x01 };
static DEFINE_STATIC_PACKET(r8s_dsc, DSI_PKT_TYPE_COMP, R8S_DSC, 0);

static u8 R8S_PPS[] = {
	//1080x2400 Slice Info : 540x40
	0x11, 0x00, 0x00, 0x89, 0x30, 0x80, 0x09, 0x60,
	0x04, 0x38, 0x00, 0x28, 0x02, 0x1C, 0x02, 0x1C,
	0x02, 0x00, 0x02, 0x0E, 0x00, 0x20, 0x03, 0xDD,
	0x00, 0x07, 0x00, 0x0C, 0x02, 0x77, 0x02, 0x8B,
	0x18, 0x00, 0x10, 0xF0, 0x03, 0x0C, 0x20, 0x00,
	0x06, 0x0B, 0x0B, 0x33, 0x0E, 0x1C, 0x2A, 0x38,
	0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7B,
	0x7D, 0x7E, 0x01, 0x02, 0x01, 0x00, 0x09, 0x40,
	0x09, 0xBE, 0x19, 0xFC, 0x19, 0xFA, 0x19, 0xF8,
	0x1A, 0x38, 0x1A, 0x78, 0x1A, 0xB6, 0x2A, 0xF6,
	0x2B, 0x34, 0x2B, 0x74, 0x3B, 0x74, 0x6B, 0xF4,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static DEFINE_STATIC_PACKET(r8s_pps, DSI_PKT_TYPE_PPS, R8S_PPS, 0);

static u8 R8S_NORMAL_MODE[] = {
	0x53, 0x20

};
static DEFINE_STATIC_PACKET(r8s_normal_mode, DSI_PKT_TYPE_WR, R8S_NORMAL_MODE, 0);

static struct seqinfo SEQINFO(r8s_set_bl_param_seq);
static struct seqinfo SEQINFO(r8s_set_fps_param_seq);
#if defined(CONFIG_SEC_FACTORY)
static struct seqinfo SEQINFO(r8s_res_init_seq);
#endif

static void *r8s_init_cmdtbl[] = {
	&DLYINFO(r8s_wait_10msec),
	&PKTINFO(r8s_sleep_out),
	&DLYINFO(r8s_wait_30msec),

	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),

	/* common setting */
	&PKTINFO(r8s_global_param_setting),
	&PKTINFO(r8s_panel_update),

	&PKTINFO(r8s_dsc),
	&PKTINFO(r8s_pps),

	&PKTINFO(r8s_te_offset),
	&PKTINFO(r8s_te_on),

	&PKTINFO(r8s_caset),
	&PKTINFO(r8s_paset),

	&PKTINFO(r8s_min_roi_setting),

#ifdef CONFIG_DYNAMIC_FREQ
	&PKTINFO(r8s_ffc_set_1),
	&PKTINFO(r8s_ffc_set_2),
	&PKTINFO(r8s_panel_update),
#endif
#if defined(CONFIG_SEC_FACTORY) && defined(CONFIG_SUPPORT_FAST_DISCHARGE)
	&PKTINFO(r8s_fast_discharge), /* need to check */
#endif
	&PKTINFO(r8s_err_fg_on),
	&PKTINFO(r8s_err_fg_setting),

	&PKTINFO(r8s_vsync_set),
	&PKTINFO(r8s_pcd_det_set),

	&PKTINFO(r8s_freq_set),
	&PKTINFO(r8s_panel_update),

	&PKTINFO(r8s_porch_set),
	&PKTINFO(r8s_panel_update),

	&PKTINFO(r8s_hbm_transition), /* 53h should be not included in bl_seq */
	&SEQINFO(r8s_set_bl_param_seq), /* includes FPS setting also */

	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
	&DLYINFO(r8s_wait_sleep_out_90msec),

#if defined(CONFIG_SEC_FACTORY)
	/* gpara setting & 60ms delay */
	&SEQINFO(r8s_res_init_seq),
#endif
};

static void *r8s_res_init_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),

	&s6e3fc3_restbl[RES_ID],
	&s6e3fc3_restbl[RES_COORDINATE],
	&s6e3fc3_restbl[RES_CODE],
	&s6e3fc3_restbl[RES_DATE],
	&s6e3fc3_restbl[RES_ELVSS],
	&s6e3fc3_restbl[RES_OCTA_ID],
#ifdef CONFIG_DISPLAY_USE_INFO
	&s6e3fc3_restbl[RES_CHIP_ID],
	&s6e3fc3_restbl[RES_SELF_DIAG],
	&s6e3fc3_restbl[RES_ERR_FG],
	&s6e3fc3_restbl[RES_DSI_ERR],
#endif
	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
};
#if defined(CONFIG_SEC_FACTORY)
static DEFINE_SEQINFO(r8s_res_init_seq, r8s_res_init_cmdtbl);
#endif

static void *r8s_set_bl_param_cmdtbl[] = {
	&PKTINFO(r8s_fps_1),
	&PKTINFO(r8s_dimming_speed),
	&PKTINFO(r8s_tset_set),
	&PKTINFO(r8s_acl_set),
	&PKTINFO(r8s_panel_update),
	&PKTINFO(r8s_acl_control),
	&PKTINFO(r8s_wrdisbv),
	&PKTINFO(r8s_panel_update),
};

static DEFINE_SEQINFO(r8s_set_bl_param_seq, r8s_set_bl_param_cmdtbl);

static void *r8s_set_bl_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),
	&PKTINFO(r8s_hbm_transition),
	&SEQINFO(r8s_set_bl_param_seq),
	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
};

static DEFINE_COND(r8s_cond_is_panel_state_not_lpm, is_panel_state_not_lpm);

static void *r8s_set_fps_cmdtbl[] = {
	&CONDINFO_S(r8s_cond_is_panel_state_not_lpm),
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),
	&PKTINFO(r8s_hbm_transition),
	&SEQINFO(r8s_set_bl_param_seq),
	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
	&CONDINFO_E(r8s_cond_is_panel_state_not_lpm),
};

static void *r8s_display_on_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&PKTINFO(r8s_display_on),
	&KEYINFO(r8s_level1_key_disable),
};

static void *r8s_display_off_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&PKTINFO(r8s_display_off),
	&PKTINFO(r8s_err_fg_off),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
	&DLYINFO(r8s_wait_display_off),
};

static void *r8s_exit_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),
#ifdef CONFIG_DISPLAY_USE_INFO
	&s6e3fc3_dmptbl[DUMP_DSI_ERR],
	&s6e3fc3_dmptbl[DUMP_SELF_DIAG],
#endif
	&PKTINFO(r8s_sleep_in),
	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
	&DLYINFO(r8s_wait_sleep_in),
};

static void *r8s_alpm_enter_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),

	&PKTINFO(r8s_aod_setting),
	&PKTINFO(r8s_vaint_setting),

	&PKTINFO(r8s_lpm_nit),

	&PKTINFO(r8s_lpm_porch_0_on),
	&PKTINFO(r8s_lpm_porch_1_on),
	&PKTINFO(r8s_lpm_porch_2_on),

	&PKTINFO(r8s_panel_update),

	&DLYINFO(r8s_wait_1usec),
	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
	&DLYINFO(r8s_wait_17msec),
};

static void *r8s_alpm_exit_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),

	&PKTINFO(r8s_swire_no_pulse),
	&PKTINFO(r8s_lpm_porch_0_off),
	&PKTINFO(r8s_lpm_porch_1_off),
	&PKTINFO(r8s_lpm_porch_2_off),
	&PKTINFO(r8s_normal_setting),

	&PKTINFO(r8s_dimming_speed),
	&PKTINFO(r8s_normal_mode),
	&PKTINFO(r8s_panel_update),

	&DLYINFO(r8s_wait_34msec),

	&SEQINFO(r8s_set_bl_param_seq),
	&DLYINFO(r8s_wait_1usec),

	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
};

static void *r8s_dump_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),
	&s6e3fc3_dmptbl[DUMP_RDDPM],
	&s6e3fc3_dmptbl[DUMP_RDDSM],
	&s6e3fc3_dmptbl[DUMP_DSI_ERR],
	&s6e3fc3_dmptbl[DUMP_SELF_DIAG],
	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
};

#ifdef CONFIG_DYNAMIC_FREQ
static void *r8s_dynamic_ffc_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),
	&PKTINFO(r8s_ffc_set_1),
	&PKTINFO(r8s_ffc_set_2),
	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
};

static void *r8s_dynamic_ffc_off_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),
	&PKTINFO(r8s_ffc_off_1),
	&PKTINFO(r8s_ffc_off_2),
	&PKTINFO(r8s_ffc_off_3),
	&PKTINFO(r8s_panel_update),
	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
};
#endif

#if defined(CONFIG_SEC_FACTORY) && defined(CONFIG_SUPPORT_FAST_DISCHARGE)
static void *r8s_fast_discharge_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&PKTINFO(r8s_fast_discharge),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
	&DLYINFO(r8s_wait_34msec),
};
#endif

static void *r8s_check_condition_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),
	&s6e3fc3_dmptbl[DUMP_RDDPM],
	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
};

#ifdef CONFIG_SUPPORT_MASK_LAYER
static void *r8s_mask_layer_workaround_cmdtbl[] = {
	&DLYINFO(r8s_wait_1_vsync),
};

static void *r8s_mask_layer_before_cmdtbl[] = {
	&DLYINFO(r8s_wait_1_vsync),

	&CONDINFO_S(r8s_cond_is_120hz),
	&DLYINFO(r8s_wait_1msec),
	&CONDINFO_E(r8s_cond_is_120hz),

	&CONDINFO_S(r8s_cond_is_60hz),
	&DLYINFO(r8s_wait_9msec),
	&CONDINFO_E(r8s_cond_is_60hz),
};

static void *r8s_mask_layer_after_cmdtbl[] = {
	&CONDINFO_S(r8s_cond_is_120hz),
	&DLYINFO(r8s_wait_1_vsync),
	&CONDINFO_E(r8s_cond_is_120hz),
};


static void *r8s_mask_layer_enter_br_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),

	&PKTINFO(r8s_acl_control),
	&PKTINFO(r8s_dimming_speed),
	&PKTINFO(r8s_fod_enter_1),
	&PKTINFO(r8s_hbm_transition),
	&PKTINFO(r8s_wrdisbv),
	&PKTINFO(r8s_fod_enter_2),

	&CONDINFO_S(r8s_cond_is_120hz),
	&DLYINFO(r8s_wait_9msec),
	&CONDINFO_E(r8s_cond_is_120hz),

	&CONDINFO_S(r8s_cond_is_60hz),
	&DLYINFO(r8s_wait_17msec),
	&CONDINFO_E(r8s_cond_is_60hz),

	&PKTINFO(r8s_fod_enter_3),

	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
};

static void *r8s_mask_layer_exit_br_cmdtbl[] = {
	&KEYINFO(r8s_level1_key_enable),
	&KEYINFO(r8s_level2_key_enable),
	&KEYINFO(r8s_level3_key_enable),

	&PKTINFO(r8s_acl_control),

	&PKTINFO(r8s_dimming_speed),
	&PKTINFO(r8s_fod_exit_1),
	&PKTINFO(r8s_hbm_transition),
	&PKTINFO(r8s_wrdisbv),
	&PKTINFO(r8s_fod_exit_2),

	&CONDINFO_S(r8s_cond_is_120hz),
	&DLYINFO(r8s_wait_9msec),
	&CONDINFO_E(r8s_cond_is_120hz),

	&CONDINFO_S(r8s_cond_is_60hz),
	&DLYINFO(r8s_wait_17msec),
	&CONDINFO_E(r8s_cond_is_60hz),

	&PKTINFO(r8s_fod_exit_3),

	&KEYINFO(r8s_level3_key_disable),
	&KEYINFO(r8s_level2_key_disable),
	&KEYINFO(r8s_level1_key_disable),
};
#endif

static void *r8s_dummy_cmdtbl[] = {
	NULL,
};

static struct seqinfo r8s_seqtbl[MAX_PANEL_SEQ] = {
	[PANEL_INIT_SEQ] = SEQINFO_INIT("init-seq", r8s_init_cmdtbl),
	[PANEL_RES_INIT_SEQ] = SEQINFO_INIT("resource-init-seq", r8s_res_init_cmdtbl),
	[PANEL_SET_BL_SEQ] = SEQINFO_INIT("set-bl-seq", r8s_set_bl_cmdtbl),
	[PANEL_DISPLAY_ON_SEQ] = SEQINFO_INIT("display-on-seq", r8s_display_on_cmdtbl),
	[PANEL_DISPLAY_OFF_SEQ] = SEQINFO_INIT("display-off-seq", r8s_display_off_cmdtbl),
	[PANEL_EXIT_SEQ] = SEQINFO_INIT("exit-seq", r8s_exit_cmdtbl),
	[PANEL_DISPLAY_MODE_SEQ] = SEQINFO_INIT("fps-seq", r8s_set_fps_cmdtbl),
	[PANEL_ALPM_ENTER_SEQ] = SEQINFO_INIT("alpm-enter-seq", r8s_alpm_enter_cmdtbl),
	[PANEL_ALPM_EXIT_SEQ] = SEQINFO_INIT("alpm-exit-seq", r8s_alpm_exit_cmdtbl),
#ifdef CONFIG_DYNAMIC_FREQ
	[PANEL_DYNAMIC_FFC_SEQ] = SEQINFO_INIT("dynamic-ffc-seq", r8s_dynamic_ffc_cmdtbl),
	[PANEL_DYNAMIC_FFC_OFF_SEQ] = SEQINFO_INIT("dynamic-ffc-off-seq", r8s_dynamic_ffc_off_cmdtbl),
#endif
#ifdef CONFIG_SUPPORT_MASK_LAYER
	[PANEL_MASK_LAYER_STOP_DIMMING_SEQ] = SEQINFO_INIT("mask-layer-workaround-seq", r8s_mask_layer_workaround_cmdtbl),
	[PANEL_MASK_LAYER_BEFORE_SEQ] = SEQINFO_INIT("mask-layer-before-seq", r8s_mask_layer_before_cmdtbl),
	[PANEL_MASK_LAYER_ENTER_BR_SEQ] = SEQINFO_INIT("mask-layer-enter-br-seq", r8s_mask_layer_enter_br_cmdtbl), //temp br
	[PANEL_MASK_LAYER_EXIT_BR_SEQ] = SEQINFO_INIT("mask-layer-exit-br-seq", r8s_mask_layer_exit_br_cmdtbl),
#endif
#if defined(CONFIG_SEC_FACTORY) && defined(CONFIG_SUPPORT_FAST_DISCHARGE)
	[PANEL_FD_SEQ] = SEQINFO_INIT("fast-discharge-seq", r8s_fast_discharge_cmdtbl),
#endif
	[PANEL_DUMP_SEQ] = SEQINFO_INIT("dump-seq", r8s_dump_cmdtbl),
	[PANEL_CHECK_CONDITION_SEQ] = SEQINFO_INIT("check-condition-seq", r8s_check_condition_cmdtbl),
	[PANEL_DUMMY_SEQ] = SEQINFO_INIT("dummy-seq", r8s_dummy_cmdtbl),
};

struct common_panel_info s6e3fc3_r8s_panel_info = {
	.ldi_name = "s6e3fc3",
	.name = "s6e3fc3_r8s_default",
	.model = "AMS646YB01",
	.vendor = "SDC",
	.id = 0x800042,
	.rev = 0,
	.ddi_props = {
		.gpara = (DDI_SUPPORT_WRITE_GPARA |DDI_SUPPORT_READ_GPARA | DDI_SUPPORT_2BYTE_GPARA | DDI_SUPPORT_POINT_GPARA),
		.err_fg_recovery = false,
		.support_vrr = true,
	},
#if defined(CONFIG_PANEL_DISPLAY_MODE)
	.common_panel_modes = &s6e3fc3_r8s_display_modes,
#endif
	.mres = {
		.nr_resol = ARRAY_SIZE(s6e3fc3_r8s_default_resol),
		.resol = s6e3fc3_r8s_default_resol,
	},
	.vrrtbl = s6e3fc3_r8s_default_vrrtbl,
	.nr_vrrtbl = ARRAY_SIZE(s6e3fc3_r8s_default_vrrtbl),
	.maptbl = r8s_maptbl,
	.nr_maptbl = ARRAY_SIZE(r8s_maptbl),
	.seqtbl = r8s_seqtbl,
	.nr_seqtbl = ARRAY_SIZE(r8s_seqtbl),
	.rditbl = s6e3fc3_rditbl,
	.nr_rditbl = ARRAY_SIZE(s6e3fc3_rditbl),
	.restbl = s6e3fc3_restbl,
	.nr_restbl = ARRAY_SIZE(s6e3fc3_restbl),
	.dumpinfo = s6e3fc3_dmptbl,
	.nr_dumpinfo = ARRAY_SIZE(s6e3fc3_dmptbl),

#ifdef CONFIG_EXYNOS_DECON_MDNIE_LITE
	.mdnie_tune = &s6e3fc3_r8s_mdnie_tune,
#endif
	.panel_dim_info = {
		[PANEL_BL_SUBDEV_TYPE_DISP] = &s6e3fc3_r8s_panel_dimming_info,
#ifdef CONFIG_SUPPORT_AOD_BL
		[PANEL_BL_SUBDEV_TYPE_AOD] = &s6e3fc3_r8s_panel_aod_dimming_info,
#endif
	},
#ifdef CONFIG_DYNAMIC_FREQ
	.df_freq_tbl = r8s_s6e3fc3_dynamic_freq_set,
#endif

#ifdef CONFIG_SUPPORT_DISPLAY_PROFILER
	.profile_tune = NULL,
#endif
};

static int __init s6e3fc3_r8s_panel_init(void)
{
	register_common_panel(&s6e3fc3_r8s_panel_info);

	return 0;
}
arch_initcall(s6e3fc3_r8s_panel_init)
#endif /* __S6E3FC3_R8S_PANEL_H__ */
