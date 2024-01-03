/*
 * linux/drivers/video/fbdev/exynos/panel/s6e3hab/s6e3hab_hubble_irc.h
 *
 * Header file for S63EXA0 Driver
 *
 * Copyright (c) 2016 Samsung Electronics
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __S6E3XA0_HUBBLE_IRC_H__
#define __S6E3XA0_HUBBLE_IRC_H__

#include "../panel.h"
#include "../panel_irc.h"

#define S6E3XA0_IRC_DYNAMIC_OFS		2
#define S6E3XA0_IRC_DYNAMIC_LEN		15
#define S6E3XA0_IRC_FIXED_OFS		15
#define S6E3XA0_IRC_FIXED_LEN		11
#define S6E3XA0_IRC_TOTAL_LEN		S6E3XA0_IRC_DYNAMIC_OFS + S6E3XA0_IRC_FIXED_LEN + S6E3XA0_IRC_DYNAMIC_LEN

static u8 irc_buffer[S6E3XA0_IRC_TOTAL_LEN];
static u8 irc_ref_tbl[S6E3XA0_IRC_TOTAL_LEN];


static struct panel_irc_info s6e3xa0_win2_irc = {
	.irc_version = IRC_INTERPOLATION_V2,
	.fixed_offset = S6E3XA0_IRC_FIXED_OFS,
	.fixed_len = S6E3XA0_IRC_FIXED_LEN,
	.dynamic_offset = S6E3XA0_IRC_DYNAMIC_OFS,
	.dynamic_len = S6E3XA0_IRC_DYNAMIC_LEN,
	.total_len = S6E3XA0_IRC_TOTAL_LEN,
	.buffer = irc_buffer,
	.ref_tbl = irc_ref_tbl
};

#endif /* __S6E3XA0_HUBBLE_IRC_H__ */
