// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 * Samsung Exynos SoC series NPU driver
 *
 * Copyright (c) 2017 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/delay.h>
#include <linux/stm.h>
#include <linux/iommu.h>
#include <linux/soc/samsung/exynos-soc.h>
#include <linux/atomic.h>
#include <linux/sched/clock.h>
#include <coresight-priv.h>

#include "npu-log.h"
#include "npu-config.h"
#include "npu-common.h"
#include "npu-device.h"
#include "npu-system.h"
#include "npu-util-regs.h"
#include "npu-stm-soc.h"
#include "npu-if-session-protodrv.h"

/* Constants for STM_SEL SFR for SYNC signal -> USD0[0:15] is available */
static const u32 STM_SEL_SYNC = 0x78;		/* [6:4] = 0b111 / [3:0] = 0b0001 */

/* SYNC definition */
static const u32 SYNC_EVENT_SEQ[] = {
	0xF, 0x0, 0xF, 0x1, 0xF, 0x2, 0xF, 0x3,		/* Preemble */
	0x9, 0x8, 0x3, 0x0,				/* SoC Code */
	0xF, 0xE};					/* Postfix */
static const u32 STM_SYNC_DELAY_US = 500;

/* Valid event table ID lists */
static const u32 VALID_STM_TABLE_ID[] = {
	0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60,	/* NPU sets */
	0x70, 0x71, 0x72, 0x74, 0x78			/* DSP sets */
};

/* TODO: Move to Kconfig */
#define CONFIG_NPU_FW_ACCESS_MCT
static const u32 EXYNOS9830_MCT_PHYS = 0x10040000;
static const u32 EXYNOS9830_MCT_IOVA = 0x50540000;
static const u32 EXYNOS9830_MCT_SIZE = 0x1000;

/* To pass system reference to link/unlink call back */
static int npu_stm_link(struct stm_source_data *data);
static void npu_stm_unlink(struct stm_source_data *data);

/* Value of npu_stm_data.enabled */
enum npu_stm_enabled {
	NPU_STM_DISABLED = 0,
	NPU_STM_ENABLED,
};

struct npu_stm_data {
	struct stm_source_data	stm_source_data;
	struct npu_system	*system;
	atomic_t		enabled;
	atomic_t		probed;
	atomic_t		guarantee;
	atomic_t		guarantee_working;
};

static struct npu_stm_data npu_stm_data = {
	.stm_source_data = {
		.nr_chans = 1,
		.name = "Exynos-NPU",
		.link = npu_stm_link,
		.unlink = npu_stm_unlink,
	},
	.system = NULL,
	.enabled = ATOMIC_INIT(NPU_STM_DISABLED),
	.probed = ATOMIC_INIT(0),
	.guarantee = ATOMIC_INIT(0),
	.guarantee_working = ATOMIC_INIT(0),
};

/**************************************************************************
 * STM timestamp synchronizer sysfs interface
 */
static const unsigned long OSC_CLK_DEFAULT = 26;
static const u32 TRACE_SET_MASK = 0xFF;

/* Definition for STM register offsets */
enum {
	STMHEER		= 0xD00,
	STMHETER	= 0xD20,
	STMHEBSR	= 0xD60,
	STMHEMCR	= 0xD64,
} stm_regs_e;

struct npu_stm_ts_stat {
	unsigned long long kmsg_time_ns;
	u64 stm_val;
	unsigned long stm_rate;
	u32 trace_set;
	struct npu_iomem_area *sfr_mct_g;
};
static struct npu_stm_ts_stat npu_stm_ts_stat = {
	.kmsg_time_ns = 0,
	.stm_val = 0,
	.stm_rate = OSC_CLK_DEFAULT,
	.trace_set = 0,
};

static u64 read_global_mct_64(void)
{
	unsigned int lo, hi, hi2;

	hi2 = npu_read_hw_reg(npu_stm_ts_stat.sfr_mct_g, 0x4);
	do {
		hi = hi2;
		lo = npu_read_hw_reg(npu_stm_ts_stat.sfr_mct_g, 0x0);
		hi2 = npu_read_hw_reg(npu_stm_ts_stat.sfr_mct_g, 0x4);
	} while (hi != hi2);

	return ((u64)hi << 32) | lo;
}

static u32 read_global_mct_32(void)
{
	return npu_read_hw_reg(npu_stm_ts_stat.sfr_mct_g, 0x0);
}

static const struct npu_profile_operation PROFILE_OPS = {
	.timestamp = read_global_mct_32,
};

static ssize_t stm_ts_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned long long kmsg_time_us;
	u64 stm_val_us;

	/* Calculate rounded value to micro seconds */
	kmsg_time_us = (npu_stm_ts_stat.kmsg_time_ns + 500) / 1000;
	stm_val_us =
		(npu_stm_ts_stat.stm_val + (npu_stm_ts_stat.stm_rate / 2))
		/ npu_stm_ts_stat.stm_rate;

	ret = scnprintf(buf, PAGE_SIZE,
		"STM timestamp status :\n"
		" - kmsg_time : %llu us\n"
		" - kmsg_raw_time : %llu\n"
		" - STM time : %llu us\n"
		" - STM raw_time : %llu\n"
		" - STM TS rate : %lu MHz\n"
		" - Trace set : %02x\n",
		kmsg_time_us, npu_stm_ts_stat.kmsg_time_ns,
		stm_val_us, npu_stm_ts_stat.stm_val,
		npu_stm_ts_stat.stm_rate,
		npu_stm_ts_stat.trace_set);

	return ret;
}

static ssize_t stm_ts_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t len)
{
	unsigned long trace_set;
	int ret;
	size_t i;

	ret = kstrtoul(buf, 16, &trace_set);
	if (ret) {
		npu_err("Failed to parsing trace set. ret = %d\n", ret);
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(VALID_STM_TABLE_ID); i++) {
		if ((u32)trace_set == VALID_STM_TABLE_ID[i]) {
			npu_stm_ts_stat.trace_set = ((u32)trace_set & TRACE_SET_MASK);
			return len;
		}
	}

	/* No matching trace set */
	return -EINVAL;
}

/*
 * sysfs attribute for STM Timestamp stat
 *   name = dev_attr_stm_ts
 *   show = stm_ts_show
 *   store = stm_ts_store
 */
static DEVICE_ATTR_RW(stm_ts);

static ssize_t stm_guarantee_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (atomic_read(&npu_stm_data.guarantee))
		buf[0] = '1';
	else
		buf[0] = '0';

	buf[1] = '\n';

	return 2;
}

static ssize_t stm_guarantee_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t len)
{
	if (len <= 0)
		return 0;

	switch (buf[0]) {
	case '0':
		atomic_set(&npu_stm_data.guarantee, 0);
		return len;
	case '1':
		atomic_set(&npu_stm_data.guarantee, 1);
		return len;
	default:
		return -EINVAL;
	}
}

/*
 * sysfs attribute for guaranteed STM
 *   name = dev_attr_stm_guarantee
 *   show = stm_guarantee_show
 *   store = stm_guarantee_store
 */
static DEVICE_ATTR_RW(stm_guarantee);

/* Register SYSFS entry and initialize MCT clock rate */
static int register_stm_ts_sysfs(struct npu_system *system)
{
	int ret = 0;
	struct clk *mct_clk;
	struct npu_device *device = container_of(system, struct npu_device, system);

	/* Create SYSFS entry for Guaranteed STM setting */
	ret = sysfs_create_file(&device->dev->kobj, &dev_attr_stm_guarantee.attr);
	if (ret) {
		probe_err("sysfs_create_file error : ret = %d\n", ret);
		goto err_exit;
	}

	/* Create SYSFS entry for STM Timestamp check */
	ret = sysfs_create_file(&device->dev->kobj, &dev_attr_stm_ts.attr);
	if (ret) {
		probe_err("sysfs_create_file error : ret = %d\n", ret);
		goto err_exit;
	}

	mct_clk = clk_get(NULL, "fin_pll");
	if (IS_ERR(mct_clk))
		npu_stm_ts_stat.stm_rate = OSC_CLK_DEFAULT;
	else
		npu_stm_ts_stat.stm_rate = clk_get_rate(mct_clk) / 1000000;

	npu_stm_ts_stat.sfr_mct_g = &system->sfr_mct_g;
err_exit:
	return ret;
}

/* Make sync point of MCT and kernel clock */
static void update_stm_ts_stat(void)
{
	/* Get timer value */
	npu_stm_ts_stat.stm_val = read_global_mct_64();
	npu_stm_ts_stat.kmsg_time_ns = sched_clock();
}

static u32 get_stm_trace_set(void)
{
	return npu_stm_ts_stat.trace_set;
}

/*********************************************************
 * STM managing functions
 */
static int __busp_div_1to1(struct npu_system *system)
{
	int ret = 0;
	const struct reg_set_map_2 busp_div_1to1_regs[] = {
		/* Addr			Offset	Val	Mask */
		{&system->sfr_npu[0],	0x1804,	0x0,	0x0000000F},	/* NPU00_CMU_CLK_CON_DIV_DIV_CLK_NPUC_BUSP */
		{&system->sfr_npu[1],	0x1800,	0x0,	0x0000000F},	/* NPU01_CMU_CLK_CON_DIV_DIV_CLK_NPU_BUSP */
		{&system->sfr_npu[2],	0x1804,	0x0,	0x0000000F},	/* NPU10_CMU_CLK_CON_DIV_DIV_CLK_NPUC_BUSP */
		{&system->sfr_npu[3],	0x1800,	0x0,	0x0000000F},	/* NPU11_CMU_CLK_CON_DIV_DIV_CLK_NPU_BUSP */
	};

	ret = npu_set_hw_reg_2(busp_div_1to1_regs, ARRAY_SIZE(busp_div_1to1_regs), 0);
	if (ret) {
		npu_err("Failed to write registers on busp_div_1to1_regs array (%d)\n", ret);
		goto err_exit;
	}

	npu_info("BUSP divider is set to 1:1. Guaranteed STM available.\n");
err_exit:
	return ret;
}

static int __busp_div_1to4(struct npu_system *system)
{
	int ret = 0;
	const struct reg_set_map_2 busp_div_1to4_regs[] = {
		/* Addr			Offset	Val	Mask */
		{&system->sfr_npu[0],	0x1804,	0x3,	0x0000000F},	/* NPU00_CMU_CLK_CON_DIV_DIV_CLK_NPUC_BUSP */
		{&system->sfr_npu[1],	0x1800,	0x3,	0x0000000F},	/* NPU01_CMU_CLK_CON_DIV_DIV_CLK_NPU_BUSP */
		{&system->sfr_npu[2],	0x1804,	0x3,	0x0000000F},	/* NPU10_CMU_CLK_CON_DIV_DIV_CLK_NPUC_BUSP */
		{&system->sfr_npu[3],	0x1800,	0x3,	0x0000000F},	/* NPU11_CMU_CLK_CON_DIV_DIV_CLK_NPU_BUSP */
	};

	ret = npu_set_hw_reg_2(busp_div_1to4_regs, ARRAY_SIZE(busp_div_1to4_regs), 0);
	if (ret) {
		npu_err("Failed to write registers on busp_div_1to4_regs array (%d)\n", ret);
		goto err_exit;
	}

	npu_info("BUSP divider is restored to 1:4. Guaranteed STM no longer available.\n");
err_exit:
	return ret;
}

static int __enable_npu_stm_sfr(struct npu_system *system, u32 stm_sel)
{
	int ret = 0;
	/*const u32 DBG_ATCLK = 800 * 1000 * 1000;	ATCLK = 800MHz */

	const struct reg_set_map_2 enable_npu_stm_regs[] = {
		/* Addr				Offset	Val		Mask */
		{&system->sfr_dnc,		0x0078,	stm_sel,	0x0000007F},	/* STM_SEL[6:4][3:0] */
		{&system->sfr_dnc,		0x0074,	0x1,		0x00000001},	/* STM_ENABLE(DNC) */
		{&system->sfr_npuc[0],		0xF0418,	0x1,	0x00000001},	/* STM_ENABLE(NPUC0) */
		{&system->sfr_npuc[1],		0xF0418,	0x1,	0x00000001},	/* STM_ENABLE(NPUC1) */
		{&system->sfr_coresight,	0x0034,	0x1,		0x00000007},	/* Route NPU to STM */
		{&system->sfr_coresight,	0x002C,	0x1,		0x00000001},	/* Coresight clock enable */
	};

	BUG_ON(!system->sfr_coresight.vaddr);

	ret = npu_set_hw_reg_2(enable_npu_stm_regs, ARRAY_SIZE(enable_npu_stm_regs), 0);
	if (ret) {
		npu_err("Failed to write registers on enable_npu_stm_regs array (%d)\n", ret);
		goto err_exit;
	}

	npu_info("STM enabled with STM_SEL = 0x%08x\n", stm_sel);
err_exit:
	return ret;
}

static int __disable_npu_stm_sfr(struct npu_system *system)
{
	int ret = 0;
	const struct reg_set_map_2 disable_npu_stm_regs[] = {
		/* Addr				Offset		Val	Mask */
		{&system->sfr_coresight,	0x0034,		0x0,	0x00000007},	/* Route NPU to STM */
		{&system->sfr_dnc,		0x0078,		0x00,	0x0000007F},	/* STM_SEL[6:4][3:0] */
		{&system->sfr_dnc,		0x0074,		0x0,	0x00000001},	/* STM_ENABLE(DNC) */
		{&system->sfr_npuc[0],		0xF0418,	0x0,	0x00000001},	/* STM_ENABLE(NPUC0) */
		{&system->sfr_npuc[1],		0xF0418,	0x0,	0x00000001},	/* STM_ENABLE(NPUC1) */
	};

	ret = npu_set_hw_reg_2(disable_npu_stm_regs, ARRAY_SIZE(disable_npu_stm_regs), 0);
	if (ret) {
		npu_err("Failed to write registers on disable_npu_stm_regs array (%d)\n", ret);
		goto err_exit;
	}

	npu_info("STM disabled\n");
err_exit:
	return ret;
}

/*
 * Allow all 64 HW events generated in NPU pass through the CoreSight path.
 * Current CoreSight driver implementation only allows either 0~31th or 32~63th
 * can be traced, but not both, while the CoreSight hardware can trace
 * 64 events at once. This function manually overrides STMHEBSR, STMHETER and STMHEER
 * to make the events traced.
 */
static int npu_stm_allow_64_trace(struct npu_system *system)
{
	int	ret = 0;
	u32	old_hemcr;	/* Backup of current STMHEMCR value */
	struct reg_set_map_2 allow64_npu_stm_regs[] = {
		/* Addr			Offset	Val		Mask */
		{&system->sfr_stm,	STMHEBSR,	0x0,		0x00000001},	/* Bank 0 / event 0~31 */
		{&system->sfr_stm,	STMHETER,	0xFFFFFFFF,	0xFFFFFFFF},	/* Enable all */
		{&system->sfr_stm,	STMHEER,	0xFFFFFFFF,	0xFFFFFFFF},	/* Enable all */
		{&system->sfr_stm,	STMHEBSR,	0x1,		0x00000001},	/* Bank 1 / event 32~63 */
		{&system->sfr_stm,	STMHETER,	0xFFFFFFFF,	0xFFFFFFFF},	/* Enable all */
		{&system->sfr_stm,	STMHEER,	0xFFFFFFFF,	0xFFFFFFFF},	/* Enable all */
	};

	BUG_ON(!system->sfr_stm.vaddr);

	/* Unlock STM register for writing */
	CS_UNLOCK(system->sfr_stm.vaddr);
	old_hemcr = npu_read_hw_reg(&system->sfr_stm, STMHEMCR);

	/* Stop STM before set trace configuration : Settings are in effect after restarting */
	npu_write_hw_reg(&system->sfr_stm, STMHEMCR, 0x0, 0xFFFFFFFF);

	/* Enable events both bank 0 and 1 */
	ret = npu_set_hw_reg_2(allow64_npu_stm_regs, ARRAY_SIZE(allow64_npu_stm_regs), 0);
	if (ret) {
		npu_err("Failed to write registers on probe_npu_stm_regs array (%d)\n", ret);
		goto err_exit;
	}

	/* Restore STMHEMCR */
	npu_write_hw_reg(&system->sfr_stm, STMHEMCR, old_hemcr, 0xFFFFFFFF);

	npu_dbg("64 STM events are enabled\n");
err_exit:
	/* Lock the STM SFR again */
	CS_LOCK(system->sfr_stm.vaddr);
	return ret;
}

/*
 * Write SYNC_EVENT_SEQ to lower 16b part of STM_SFR0 with STM_SYNC_DELAY_US.
 * This function will be a match point when synching timestamp of
 * STM and ftrace
 */
static void stm_sync_signal(struct npu_system *system)
{
	size_t i;
	u32 val;

	for (i = 0; i < ARRAY_SIZE(SYNC_EVENT_SEQ); i++) {
		val = (1 << SYNC_EVENT_SEQ[i]);
		udelay(STM_SYNC_DELAY_US);
		npu_write_hw_reg(&system->sfr_dnc, 0x0080, val, 0xFFFF);
	}
}

/*
 * Make Sync HW signal
 * STM source of DNC will be switched to DSP #1 temporary, to enable host-triggerd signal.
 */

static int __stm_sync(struct npu_system *system)
{
	u32	ret = 0;

	/* Enable STM with DSP #1 setting */
	ret = __enable_npu_stm_sfr(system, STM_SEL_SYNC);
	if (ret) {
		npu_err("__enable_npu_stm_sfr failed : %d\n", ret);
		goto err_exit;
	}

	/* Post 8 STM trigger, with 100us delay */
	npu_info("Start STM sync.\n");
	stm_sync_signal(system);

	/* Update timestamp */
	update_stm_ts_stat();

	/* Disable STM */
	ret = __disable_npu_stm_sfr(system);
	if (ret) {
		npu_err("__disable_npu_stm_sfr failed : %d\n", ret);
		goto err_exit;
	}

err_exit:
	return ret;
}

/*********************************************************
 * STM Link / Unlink handlers
 */
/* Helper function to invoke npu_stm_enable or npu_stm_disable if NPU is currently running */
static int npu_stm_enable_if_running(struct npu_system *system)
{
	if (npu_if_session_protodrv_is_opened())
		return npu_stm_enable(system);

	npu_info("STM enabling will be deferred until the block power enabled.\n");
	return 0;
}

static int npu_stm_disable_if_running(struct npu_system *system)
{
	if (npu_if_session_protodrv_is_opened())
		return npu_stm_disable(system);

	npu_info("STM disabling is not necessary when block power is off.\n");
	return 0;
}


static int npu_stm_link(struct stm_source_data *data)
{
	int ret = 0;

	struct npu_stm_data *npu_stm
		= container_of(data, struct npu_stm_data, stm_source_data);
	npu_info("%s called : data = %p, system = %p\n", __func__, data, npu_stm->system);

	/* Enable full STM events */
	ret = npu_stm_allow_64_trace(npu_stm->system);
	if (ret) {
		npu_err("npu_stm_allow_64_trace failed : %d\n", ret);
		goto err_exit;
	}

	/* Invoke enable_npu_stm if the state is changed */
	if (atomic_cmpxchg(&npu_stm->enabled, NPU_STM_DISABLED, NPU_STM_ENABLED)
		== NPU_STM_DISABLED) {

		npu_info("Do enable_npu_stm\n");
		if (likely(npu_stm->system)) {
			ret = npu_stm_enable_if_running(npu_stm->system);
			if (unlikely(ret)) {
				npu_err("npu_stm_enable_if_running() error : %d\n", ret);
				goto err_exit;
			}
		}
	}

err_exit:
	return ret;
}

static void npu_stm_unlink(struct stm_source_data *data)
{
	int ret = 0;

	struct npu_stm_data *npu_stm
		= container_of(data, struct npu_stm_data, stm_source_data);
	npu_info("%s called : data = %p, system = %p\n", __func__, data, npu_stm->system);

	/* Invoke disable_npu_stm if the state is changed */
	if (atomic_cmpxchg(&npu_stm->enabled, NPU_STM_ENABLED, NPU_STM_DISABLED)
		== NPU_STM_ENABLED) {

		npu_info("Do disable_npu_stm\n");
		if (likely(npu_stm->system)) {
			ret = npu_stm_disable_if_running(npu_stm->system);
			if (unlikely(ret))
				npu_err("npu_stm_disable() error : %d\n", ret);
		}
	}
}

/*********************************************************
 * Exported functions
 */
int npu_stm_enable(struct npu_system *system)
{
	int ret = 0;

	BUG_ON(!system);

	if (atomic_read(&npu_stm_data.enabled) != NPU_STM_ENABLED) {
		/* No need to be enabled */
		return ret;
	}

	/* Adjust BUSP divider if guaranteed STM is activated */
	if (atomic_read(&npu_stm_data.guarantee)) {
		ret = __busp_div_1to1(system);
		if (ret) {
			npu_err("__busp_div_1to1 failed : %d\n", ret);
			goto err_exit;
		}
		atomic_set(&npu_stm_data.guarantee_working, 1);
	}

	/* Send sync signal */
	ret = __stm_sync(system);
	if (ret) {
		npu_err("__stm_sync failed : %d\n", ret);
		goto err_exit;
	}

	/* Enable STM with trace set defined in SYSFS */
	ret = __enable_npu_stm_sfr(system, get_stm_trace_set());
	if (ret) {
		npu_err("__enable_npu_stm_sfr failed : %d\n", ret);
		goto err_exit;
	}
	npu_info("complete %s\n", __func__);

err_exit:
	return ret;
}

int npu_stm_disable(struct npu_system *system)
{
	int ret = 0;

	BUG_ON(!system);

	/*
	 * Disabling will not check npu_stm->enabled flag,
	 * to make sure the STM disabled before shutdown.
	 * But please keep in mind that the npu_stm_disable() can be
	 * more than one time when stm_unlink() is called while the
	 * NPU is turned on.
	 */
	ret = __disable_npu_stm_sfr(system);
	if (ret) {
		npu_err("__disable_npu_stm_sfr failed : %d\n", ret);
		goto err_exit;
	}

	/* Restores BUSP divider if guarantee flag was asserted */
	if (atomic_cmpxchg(&npu_stm_data.guarantee_working, 1, 0) == 1) {
		ret = __busp_div_1to4(system);
		if (ret) {
			npu_err("__busp_div_1to4 failed : %d\n", ret);
			goto err_exit;
		}
	}

	npu_info("complete %s\n", __func__);
err_exit:
	return ret;
}

#ifdef CONFIG_NPU_FW_ACCESS_MCT
static int enable_mct_fw_access(struct npu_system *system)
{
	struct iommu_domain	*domain;
	int			ret = 0;

	if (!system->pdev)
		return 0;

	ret = npu_profile_set_operation(&PROFILE_OPS);
	if (ret) {
		probe_err("fail(%d) in npu_profile_set_operation()\n", ret);
		goto err_exit;
	}

	/* Create a mapping for MCT to let the firmware access to it */
	BUG_ON(!system->pdev);
	domain = iommu_get_domain_for_dev(&(system->pdev->dev));
	if (IS_ERR_OR_NULL(domain)) {
		probe_err("Fail(%d) in iommu_get_domain_for_dev()\n", ret);
		goto err_exit;
	}

	ret = iommu_map(
		domain, EXYNOS9830_MCT_IOVA, EXYNOS9830_MCT_PHYS,
		EXYNOS9830_MCT_SIZE, IOMMU_READ);
	if (ret) {
		probe_err("fail(%d) in iommu_map\n", ret);
		goto err_exit;
	}

err_exit:
	return ret;
}

static int disable_mct_fw_access(struct npu_system *system)
{
	struct iommu_domain	*domain;

	if (system->pdev) {
		domain = iommu_get_domain_for_dev(&(system->pdev->dev));
		iommu_unmap(domain, EXYNOS9830_MCT_IOVA, EXYNOS9830_MCT_SIZE);
	}

	return 0;
}
#else	/* !CONFIG_NPU_FW_ACCESS_MCT */
#define enable_mct_fw_access(s)		(0)
#define disable_mct_fw_access(s)	(0)
#endif

/* Probe and Release called from npu_system_soc_probe() and npu_system_soc_release() */
int npu_stm_probe(struct npu_system *system)
{
	int			ret = 0;

	npu_stm_data.system = system;
	ret = stm_source_register_device(NULL, &npu_stm_data.stm_source_data);
	if (ret) {
		probe_err("fail(%d) in stm_source_register_device()\n", ret);
		goto err_exit;
	}

	ret = register_stm_ts_sysfs(system);
	if (ret) {
		probe_err("fail(%d) in register_stm_ts_sysfs()\n", ret);
		goto err_exit_stm_register;
	}

	ret = enable_mct_fw_access(system);
	if (ret) {
		probe_err("fail(%d) in enable_mct_fw_access()\n", ret);
		goto err_exit_stm_register;
	}

	probe_info("%s completed\n", __func__);

	atomic_set(&npu_stm_data.probed, 1);

	return ret;

err_exit_stm_register:
	stm_source_unregister_device(&npu_stm_data.stm_source_data);
err_exit:

	return ret;
}

int npu_stm_release(struct npu_system *system)
{
	int			ret = 0;

	/* Check whether the initialization is completed */
	if (!atomic_read(&npu_stm_data.probed))
		return ret;

	stm_source_unregister_device(&npu_stm_data.stm_source_data);

	ret = disable_mct_fw_access(system);
	if (ret) {
		probe_err("fail(%d) in disable_mct_fw_access()\n", ret);
		goto err_exit;
	}

	atomic_set(&npu_stm_data.probed, 0);
	return ret;
err_exit:
	return ret;
}
