/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Samsung Exynos SoC series dsp driver
 *
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com/
 */

#ifndef __DSP_HW_PM_H__
#define __DSP_HW_PM_H__

#include <linux/mutex.h>
#include <linux/pm_qos.h>

struct dsp_pm_devfreq;
struct dsp_system;

enum dsp_devfreq_id {
	DSP_DEVFREQ_DNC,
	DSP_DEVFREQ_DSP,
	DSP_DEVFREQ_COUNT,
};

struct dsp_pm {
	struct mutex		lock;
	struct dsp_pm_devfreq	*devfreq;
	bool			dvfs;
	unsigned int		dvfs_disable_count;
	bool			dvfs_lock;
	struct pm_qos_request	mif_qos;
	struct pm_qos_request	int_qos;
	struct pm_qos_request	cl0_qos;
	struct pm_qos_request	cl1_qos;
	struct pm_qos_request	cl2_qos;

	struct dsp_system	*sys;
};

#endif
