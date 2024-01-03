/*
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * HIU driver for HAFM(HW-intervention Adaptive Frequency Manager) support
 * Auther : PARK CHOONGHOON (choong.park@samsung.com)
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpumask.h>
#include <linux/regmap.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/debug-snapshot.h>

#include <soc/samsung/exynos-cpupm.h>
#include <soc/samsung/cal-if.h>

#include "exynos-hiu.h"
#include "../../cpufreq/exynos-acme.h"

static struct exynos_hiu_data *hiu_data;
atomic_t hiu_normdvfs_req = ATOMIC_INIT(0);

static void hiu_stats_create_table(struct cpufreq_policy *policy);

#define POLL_PERIOD 100

/****************************************************************/
/*			HIU HELPER FUNCTION			*/
/****************************************************************/
static unsigned int hiu_get_freq_level(unsigned int freq)
{
	int level;
	struct hiu_stats *stats = hiu_data->stats;

	if (unlikely(!stats))
		return 0;

	for (level = 0; level < stats->last_level; level++)
		if (stats->freq_table[level] == freq)
			return level + hiu_data->level_offset;

	return -EINVAL;
}

static unsigned int hiu_get_power_budget(unsigned int freq)
{
	/* SW deliver fixed value as power budget */
	return hiu_data->sw_pbl;
}

static void hiu_update_reg(int offset, int mask, int shift, unsigned int val)
{
	unsigned int reg_val;

	reg_val = __raw_readl(hiu_data->base + offset);
	reg_val &= ~(mask << shift);
	reg_val |= val << shift;
	__raw_writel(reg_val, hiu_data->base + offset);
}

static unsigned int hiu_read_reg(int offset, int mask, int shift)
{
	unsigned int reg_val;

	reg_val = __raw_readl(hiu_data->base + offset);
	return (reg_val >> shift) & mask;
}

static unsigned int hiu_get_act_dvfs(void)
{
	return hiu_read_reg(HIUTOPCTL1, ACTDVFS_MASK, ACTDVFS_SHIFT);
}

static void hiu_control_err_interrupts(int enable)
{
	if (enable)
		hiu_update_reg(HIUTOPCTL1, ENB_ERR_INTERRUPTS_MASK, 0, ENB_ERR_INTERRUPTS_MASK);
	else
		hiu_update_reg(HIUTOPCTL1, ENB_ERR_INTERRUPTS_MASK, 0, 0);
}

static void hiu_control_mailbox(int enable)
{
	hiu_update_reg(HIUTOPCTL1, ENB_SR1INTR_MASK, ENB_SR1INTR_SHIFT, !!enable);
	hiu_update_reg(HIUTOPCTL1, ENB_ACPM_COMM_MASK, ENB_ACPM_COMM_SHIFT, !!enable);
}

static void hiu_set_limit_dvfs(unsigned int freq)
{
	unsigned int level;

	level = hiu_get_freq_level(freq);

	hiu_update_reg(HIUTOPCTL2, LIMITDVFS_MASK, LIMITDVFS_SHIFT, level);
}

static void hiu_set_tb_dvfs(unsigned int freq)
{
	unsigned int level;

	level = hiu_get_freq_level(freq);

	hiu_update_reg(HIUTBCTL, TBDVFS_MASK, TBDVFS_SHIFT, level);
}

static void hiu_control_tb(int enable)
{
	hiu_update_reg(HIUTBCTL, TB_ENB_MASK, TB_ENB_SHIFT, !!enable);
}

static void hiu_control_pc(int enable)
{
	hiu_update_reg(HIUTBCTL, PC_DISABLE_MASK, PC_DISABLE_SHIFT, !enable);
}

static void hiu_set_boost_level_inc(void)
{
	unsigned int inc;
	struct device_node *dn = hiu_data->dn;

	if (!of_property_read_u32(dn, "bl1-inc", &inc))
		hiu_update_reg(HIUTBCTL, B1_INC_MASK, B1_INC_SHIFT, inc);
	if (!of_property_read_u32(dn, "bl2-inc", &inc))
		hiu_update_reg(HIUTBCTL, B2_INC_MASK, B2_INC_SHIFT, inc);
	if (!of_property_read_u32(dn, "bl3-inc", &inc))
		hiu_update_reg(HIUTBCTL, B3_INC_MASK, B3_INC_SHIFT, inc);
}

static void hiu_set_tb_ps_cfg_each(int index, unsigned int cfg_val)
{
	int offset;

	offset = HIUTBPSCFG_BASE + index * HIUTBPSCFG_OFFSET;
	hiu_update_reg(offset, HIUTBPSCFG_MASK, 0, cfg_val);
}

static int hiu_set_tb_ps_cfg(void)
{
	int index;
	unsigned int val;

	for (index = 0; index < hiu_data->table_size / 4; index++) {
		val = 0;
		val |= hiu_data->cfgs[index].power_borrowed << PB_SHIFT;
		val |= hiu_data->cfgs[index].boost_level << BL_SHIFT;
		val |= hiu_data->cfgs[index].power_budget_limit << PBL_SHIFT;
		val |= hiu_data->cfgs[index].power_threshold_inc << TBPWRTHRESH_INC_SHIFT;

		hiu_set_tb_ps_cfg_each(index, val);
	}

	return 0;
}

static void hiu_reg_print(void)
{
	unsigned int val;

	SYS_READ(HIUTOPCTL1, val);
	hiu_data->regs.hiutopctl1 = val;

	SYS_READ(HIUTOPCTL2, val);
	hiu_data->regs.hiutopctl2 = val;

	SYS_READ(HIUDBGSR0, val);
	hiu_data->regs.hiudbgsr0 = val;

	SYS_READ(HIUTBCTL, val);
	hiu_data->regs.hiutbctl = val;

	pr_info("====== print HIU register ======");
	pr_info("HIUTOPCTL1 : 0x%x", hiu_data->regs.hiutopctl1);
	pr_info("HIUTOPCTL2 : 0x%x", hiu_data->regs.hiutopctl2);
	pr_info("HIUDBGSR0 : 0x%x", hiu_data->regs.hiudbgsr0);
	pr_info("HIUTBCTL : 0x%x", hiu_data->regs.hiutbctl);
}

static void control_hiu_sr1_irq_pending(int enable)
{
	hiu_update_reg(GCUCTL, HIUINTR_EN_MASK, HIUINTR_EN_SHIFT, enable);
}

static bool check_hiu_sr1_irq_pending(void)
{
	return !!hiu_read_reg(HIUTOPCTL1, HIU_MBOX_RESPONSE_MASK, SR1INTR_SHIFT);
}

static void clear_hiu_sr1_irq_pending(void)
{
	hiu_update_reg(HIUTOPCTL1, HIU_MBOX_RESPONSE_MASK, SR1INTR_SHIFT, 0);
}

static void control_hiu_mailbox_err_pending(int enable)
{
	hiu_update_reg(GCUCTL, HIUERR_EN_MASK, HIUERR_EN_SHIFT, enable);
}

static bool check_hiu_mailbox_err_pending(void)
{
	return !!hiu_read_reg(HIUTOPCTL1, HIU_MBOX_ERR_MASK, HIU_MBOX_ERR_SHIFT);
}

static unsigned int get_hiu_mailbox_err(void)
{
	return hiu_read_reg(HIUTOPCTL1, HIU_MBOX_ERR_MASK, HIU_MBOX_ERR_SHIFT);
}

static void hiu_mailbox_err_handler(void)
{
	unsigned int err, val;

	err = get_hiu_mailbox_err();

	if (err & SR1UXPERR_MASK)
		pr_err("exynos-hiu: unexpected error occurs\n");

	if (err & SR1SNERR_MASK) {
		val = __raw_readl(hiu_data->base + HIUTOPCTL2);
		val = (val >> SEQNUM_SHIFT) & SEQNUM_MASK;
		pr_err("exynos-hiu: erroneous sequence num %d\n", val);
	}

	if (err & SR1TIMEOUT_MASK)
		pr_err("exynos-hiu: TIMEOUT on SR1 write\n");

	if (err & SR0RDERR_MASK)
		pr_err("exynos-hiu: SR0 read twice or more\n");

	hiu_reg_print();
}

static bool check_hiu_req_freq_updated(unsigned int req_freq)
{
	unsigned int cur_level, cur_freq;

	cur_level = hiu_get_act_dvfs();
	cur_freq = hiu_data->stats->freq_table[cur_level - hiu_data->level_offset];

	return cur_freq == req_freq;
}

static bool check_hiu_normal_req_done(unsigned int req_freq)
{
	return check_hiu_sr1_irq_pending() &&
		check_hiu_req_freq_updated(req_freq);
}

static bool check_hiu_need_register_restore(void)
{
	return !hiu_read_reg(HIUTOPCTL1, ENB_SR1INTR_MASK, ENB_SR1INTR_SHIFT);
}

static int request_dvfs_on_sr0(unsigned int req_freq)
{
	unsigned int val, level, budget;

	/* Get dvfs level */
	level = hiu_get_freq_level(req_freq);
	hiu_data->last_req_level = level;
	hiu_data->last_req_freq = req_freq;
	if (level < 0)
		return -EINVAL;

	/* Get power budget */
	budget = hiu_get_power_budget(req_freq);
	if (budget < 0)
		return -EINVAL;

	/* write REQDVFS & REQPBL to HIU SFR */
	val = __raw_readl(hiu_data->base + HIUTOPCTL2);
	val &= ~(REQDVFS_MASK << REQDVFS_SHIFT | REQPBL_MASK << REQPBL_SHIFT);
	val |= (level << REQDVFS_SHIFT | budget << REQPBL_SHIFT);
	__raw_writel(val, hiu_data->base + HIUTOPCTL2);

	return 0;
}

static void hiu_set_bl_tbpwr_threshold_each(int level)
{
	unsigned int val, offset;

	val = 0;
	val |= hiu_data->tbpwr_thresh[0] << R_SHIFT;
	val |= hiu_data->tbpwr_thresh[1] << MONINTERVAL_SHIFT;
	val |= hiu_data->tbpwr_thresh[2] << TBPWRTHRESH_EXP_SHIFT;
	val |= hiu_data->tbpwr_thresh[3] << TBPWRTHRESH_FRAC_SHIFT;

	offset = HIUTBPWRTHRESH_BASE + (level - 1) * HIUTBPWRTHRESH_OFFSET;
	hiu_update_reg(offset, HIUTBPWRTHRESH_MASK, 0, val);
}

static int hiu_set_bl_tbpwr_thresholds(void)
{
	int index;

	for (index = 0; index < HIUTBPWRTHRESH_NUM; index++)
		hiu_set_bl_tbpwr_threshold_each(index + 1);

	return 0;
}

/****************************************************************/
/*			     HIU API				*/
/****************************************************************/
static void __exynos_hiu_update_data(struct cpufreq_policy *policy);
int exynos_hiu_set_freq(unsigned int id, unsigned int req_freq)
{
	bool cpd_blocked_changed = false;

	if (unlikely(!hiu_data))
		return -ENODEV;

	if (!hiu_data->enabled)
		return -ENODEV;

	if (check_hiu_need_register_restore())
		__exynos_hiu_update_data(NULL);

	/*
	 * If HIU H/W communicates with ACPM depending on middle core's p-state,
	 * there might be ITMON error due to big core's cpd and releated register access at the same time
	 * These codes below prevent that case.
	 */
	if (req_freq >= hiu_data->boost_threshold && !hiu_data->cpd_blocked) {
		hiu_data->cpd_blocked = true;
		cpd_blocked_changed = true;

		disable_power_mode(cpumask_any(&hiu_data->cpus), POWERMODE_TYPE_CLUSTER);
	} else if (req_freq < hiu_data->boost_threshold && hiu_data->cpd_blocked) {
		hiu_data->cpd_blocked = false;
		cpd_blocked_changed = true;

		enable_power_mode(cpumask_any(&hiu_data->cpus), POWERMODE_TYPE_CLUSTER);
	}

	/* In interrupt mode, need to set normal dvfs request flag */
	if (hiu_data->operation_mode == INTERRUPT_MODE)
		atomic_inc(&hiu_normdvfs_req);

	/* Write req_freq on SR0 to request DVFS */
	if (request_dvfs_on_sr0(req_freq))
		goto fail_request_on_sr0;

	dbg_snapshot_printk("HIU_enter:%u->%u\n", hiu_data->cur_freq, req_freq);
	if (hiu_data->operation_mode == POLLING_MODE) {
		while (!check_hiu_normal_req_done(req_freq) &&
			!check_hiu_mailbox_err_pending())
			usleep_range(POLL_PERIOD, 2 * POLL_PERIOD);

		if (check_hiu_mailbox_err_pending()) {
			hiu_mailbox_err_handler();
			BUG_ON(1);
		}

		clear_hiu_sr1_irq_pending();
	} else if (hiu_data->operation_mode == INTERRUPT_MODE) {
		wait_event(hiu_data->normdvfs_wait, hiu_data->normdvfs_done);

		hiu_data->normdvfs_done = false;
	}
	dbg_snapshot_printk("HIU_exit:%u->%u\n", hiu_data->cur_freq, req_freq);

	hiu_data->cur_freq = req_freq;

	pr_debug("exynos-hiu: set REQDVFS to HIU : %ukHz\n", req_freq);

	return 0;

fail_request_on_sr0:
	if (cpd_blocked_changed) {
		if (hiu_data->cpd_blocked)
			enable_power_mode(cpumask_any(&hiu_data->cpus), POWERMODE_TYPE_CLUSTER);
		else
			disable_power_mode(cpumask_any(&hiu_data->cpus), POWERMODE_TYPE_CLUSTER);

		hiu_data->cpd_blocked = !hiu_data->cpd_blocked;
	}

	dbg_snapshot_printk("HIU_request_on_sr0_fail:%ukHz\n", req_freq);

	return -EIO;
}

int exynos_hiu_get_freq(unsigned int id)
{
	if (unlikely(!hiu_data))
		return -ENODEV;

	return hiu_data->cur_freq;
}

int exynos_hiu_get_max_freq(void)
{
	if (unlikely(!hiu_data))
		return -1;

	return hiu_data->clipped_freq;
}

unsigned int exynos_pstate_get_boost_freq(int cpu)
{
	if (unlikely(!hiu_data))
		return 0;

	if (!cpumask_test_cpu(cpu, &hiu_data->cpus))
		return 0;

	return hiu_data->boost_max;
}

/****************************************************************/
/*			HIU SR1 WRITE HANDLER			*/
/****************************************************************/
#define SWI_ENABLE	(1)
#define SWI_DISABLE	(0)

static bool check_hiu_act_freq_changed(void)
{
	unsigned int act_freq, level;
	struct hiu_stats *stats = hiu_data->stats;

	level = hiu_get_act_dvfs();
	act_freq = stats->freq_table[level - hiu_data->level_offset];

	return hiu_data->cur_freq != act_freq;
}

static void exynos_hiu_work(struct work_struct *work)
{
	/* To do if needed */
	while (!check_hiu_act_freq_changed())
		usleep_range(POLL_PERIOD, 2 * POLL_PERIOD);

	hiu_data->normdvfs_done = true;
	wake_up(&hiu_data->normdvfs_wait);
}

static void exynos_hiu_hwi_work(struct work_struct *work)
{
	/* To do if needed */
}

static irqreturn_t exynos_hiu_irq_handler(int irq, void *id)
{
	irqreturn_t ret = IRQ_NONE;

	if (!check_hiu_sr1_irq_pending() && !check_hiu_mailbox_err_pending())
		return ret;

	if (check_hiu_mailbox_err_pending()) {
		hiu_mailbox_err_handler();
		BUG_ON(1);
	}

	ret = IRQ_HANDLED;

	if (atomic_read(&hiu_normdvfs_req)) {
		atomic_dec(&hiu_normdvfs_req);
		schedule_work_on(cpumask_any(cpu_coregroup_mask(0)), &hiu_data->work);
	}

	clear_hiu_sr1_irq_pending();

	return ret;
}

/****************************************************************/
/*			EXTERNAL EVENT HANDLER			*/
/****************************************************************/
static void hiu_dummy_request(unsigned int dummy_freq)
{
	if (hiu_data->operation_mode == INTERRUPT_MODE) {
		mutex_lock(&hiu_data->lock);
		hiu_data->normdvfs_req = true;
	}

	request_dvfs_on_sr0(dummy_freq);

	if (hiu_data->operation_mode == POLLING_MODE) {
		while (!check_hiu_normal_req_done(dummy_freq) &&
			!check_hiu_mailbox_err_pending())
			usleep_range(POLL_PERIOD, 2 * POLL_PERIOD);

		if (check_hiu_mailbox_err_pending()) {
			hiu_mailbox_err_handler();
			BUG_ON(1);
		}

		clear_hiu_sr1_irq_pending();

	} else if (hiu_data->operation_mode == INTERRUPT_MODE) {
		mutex_unlock(&hiu_data->lock);
		wait_event(hiu_data->normdvfs_wait, hiu_data->normdvfs_done);

		hiu_data->normdvfs_req = false;
		hiu_data->normdvfs_done = false;
	}
}

static void __exynos_hiu_update_data(struct cpufreq_policy *policy)
{
	/* Set dvfs limit and TB threshold */
	hiu_set_limit_dvfs(hiu_data->clipped_freq);
	hiu_set_tb_dvfs(hiu_data->boost_threshold);

	/* Initialize TB level offset */
	hiu_set_boost_level_inc();

	/* Initialize TB power state config */
	hiu_set_tb_ps_cfg();

	/* Enable TB */
	hiu_control_pc(hiu_data->pc_enabled);
	hiu_control_tb(hiu_data->tb_enabled);

	if (hiu_data->pc_enabled)
		hiu_set_bl_tbpwr_thresholds();

	/* Enable error interrupts */
	hiu_control_err_interrupts(1);
	/* Enable mailbox communication with ACPM */
	hiu_control_mailbox(1);

	if (hiu_data->operation_mode == INTERRUPT_MODE) {
		control_hiu_sr1_irq_pending(SWI_ENABLE);
		control_hiu_mailbox_err_pending(SWI_ENABLE);
	}

	/*
	 * This request is dummy to give plugin threshold
	 * when hiu driver is not enabled; it is only called when cpufreq driver is registered.
	 * This request is not to change real frequency
	 */
	if (!hiu_data->enabled) {
		hiu_dummy_request(hiu_data->boost_threshold);
		hiu_dummy_request(hiu_data->cur_freq);
	}
}

static int exynos_hiu_update_data(struct cpufreq_policy *policy)
{
	if (!cpumask_test_cpu(hiu_data->cpu, policy->cpus))
		return 0;

	if (hiu_data->enabled)
		return 0;

	hiu_data->clipped_freq = hiu_data->boost_max;
	hiu_stats_create_table(policy);

	__exynos_hiu_update_data(policy);

	hiu_data->enabled = true;

	pr_info("exynos-hiu: HIU data structure update complete\n");

	return 0;
}

static struct exynos_cpufreq_ready_block exynos_hiu_ready = {
	.update = exynos_hiu_update_data,
};

/****************************************************************/
/*			SYSFS INTERFACE				*/
/****************************************************************/
static ssize_t
hiu_enable_show(struct device *dev, struct device_attribute *devattr,
		       char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hiu_data->enabled);
}

static ssize_t
hiu_enable_store(struct device *dev, struct device_attribute *devattr,
			const char *buf, size_t count)
{
	unsigned int input;

	if (kstrtos32(buf, 10, &input))
		return -EINVAL;

	return count;
}

static ssize_t
hiu_boosted_show(struct device *dev, struct device_attribute *devattr,
		       char *buf)
{
	unsigned int boosted;

	disable_power_mode(cpumask_any(&hiu_data->cpus), POWERMODE_TYPE_CLUSTER);

	boosted = hiu_read_reg(HIUTBCTL, BOOSTED_MASK, BOOSTED_SHIFT);

	enable_power_mode(cpumask_any(&hiu_data->cpus), POWERMODE_TYPE_CLUSTER);

	return snprintf(buf, PAGE_SIZE, "%d\n", boosted);
}

static ssize_t
hiu_boost_threshold_show(struct device *dev, struct device_attribute *devattr,
		       char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", hiu_data->boost_threshold);
}

static ssize_t
hiu_dvfs_limit_show(struct device *dev, struct device_attribute *devattr,
		       char *buf)
{
	unsigned int dvfs_limit;

	disable_power_mode(cpumask_any(&hiu_data->cpus), POWERMODE_TYPE_CLUSTER);

	dvfs_limit = hiu_read_reg(HIUTOPCTL2, LIMITDVFS_MASK, LIMITDVFS_SHIFT);

	enable_power_mode(cpumask_any(&hiu_data->cpus), POWERMODE_TYPE_CLUSTER);

	return snprintf(buf, PAGE_SIZE, "%d\n", dvfs_limit);
}

static ssize_t
hiu_dvfs_limit_store(struct device *dev, struct device_attribute *devattr,
			const char *buf, size_t count)
{
	unsigned int input;

	if (kstrtos32(buf, 10, &input))
		return -EINVAL;

	disable_power_mode(cpumask_any(&hiu_data->cpus), POWERMODE_TYPE_CLUSTER);

	hiu_update_reg(HIUTOPCTL2, LIMITDVFS_MASK, LIMITDVFS_SHIFT, input);

	enable_power_mode(cpumask_any(&hiu_data->cpus), POWERMODE_TYPE_CLUSTER);

	return count;
}

static DEVICE_ATTR(enabled, 0644, hiu_enable_show, hiu_enable_store);
static DEVICE_ATTR(boosted, 0444, hiu_boosted_show, NULL);
static DEVICE_ATTR(boost_threshold, 0444, hiu_boost_threshold_show, NULL);
static DEVICE_ATTR(dvfs_limit, 0644, hiu_dvfs_limit_show, hiu_dvfs_limit_store);

static struct attribute *exynos_hiu_attrs[] = {
	&dev_attr_enabled.attr,
	&dev_attr_boosted.attr,
	&dev_attr_boost_threshold.attr,
	&dev_attr_dvfs_limit.attr,
	NULL,
};

static struct attribute_group exynos_hiu_attr_group = {
	.name = "hiu",
	.attrs = exynos_hiu_attrs,
};

/****************************************************************/
/*		INITIALIZE EXYNOS HIU DRIVER			*/
/****************************************************************/
static int hiu_dt_parsing(struct device_node *dn)
{
	const char *buf;
	char buffer[20];
	unsigned int val;
	int ret = 0;
	int level;

	ret |= of_property_read_u32(dn, "operation-mode", &hiu_data->operation_mode);
	ret |= of_property_read_u32(dn, "sw-pbl", &hiu_data->sw_pbl);
	ret |= of_property_read_string(dn, "sibling-cpus", &buf);
	if (ret)
		return ret;

	if (!of_property_read_u32(dn, "cal-id", &hiu_data->cal_id))
		val = cal_dfs_get_max_freq(hiu_data->cal_id);

	if (!val) {
		pr_warn("exynos-hiu: failed to get max freq\n");
		val = UINT_MAX;
	}

	if (of_property_read_u32(dn, "boost-max", &hiu_data->boost_max))
		hiu_data->boost_max = UINT_MAX;
	hiu_data->boost_max = min(val, hiu_data->boost_max);
	if (hiu_data->boost_max == UINT_MAX)
		return -ENODEV;

	if (of_property_read_u32(dn, "boost-threshold", &hiu_data->boost_threshold))
		return -ENODEV;

	hiu_data->boost_threshold = min(hiu_data->boost_threshold, hiu_data->boost_max);

	hiu_data->cur_freq = cal_dfs_get_boot_freq(hiu_data->cal_id);
	if (!hiu_data->cur_freq)
		return -ENODEV;

	if (of_property_read_bool(dn, "pc-enabled"))
		hiu_data->pc_enabled = true;

	if (of_property_read_bool(dn, "tb-enabled"))
		hiu_data->tb_enabled = true;

	hiu_data->table_size = of_property_count_u32_elems(dn, "config-table");
	if (hiu_data->table_size < 0)
		return hiu_data->table_size;

	hiu_data->cfgs = kzalloc(sizeof(struct hiu_cfg) * hiu_data->table_size / 4, GFP_KERNEL);
	if (!hiu_data->cfgs)
		return -ENOMEM;

	ret = of_property_read_u32_array(dn, "config-table", (unsigned int *)hiu_data->cfgs, hiu_data->table_size);
	if (ret)
		return ret;

	for (level = 1; level <= HIUTBPWRTHRESH_NUM; level++) {
		snprintf(buffer, 20, "bl%d-tbpwr-threshold", level);

		ret = of_property_read_u32_array(hiu_data->dn, buffer, hiu_data->tbpwr_thresh, HIUTBPWRTHRESH_FIELDS);
		if (ret)
			continue;
	}

	cpulist_parse(buf, &hiu_data->cpus);
	cpumask_and(&hiu_data->cpus, &hiu_data->cpus, cpu_possible_mask);
	cpumask_and(&hiu_data->cpus, &hiu_data->cpus, cpu_online_mask);
	if (cpumask_weight(&hiu_data->cpus) == 0)
		return -ENODEV;

	hiu_data->cpu = cpumask_first(&hiu_data->cpus);

	return 0;
}

static void hiu_stats_create_table(struct cpufreq_policy *policy)
{
	unsigned int i = 0, count = 0, alloc_size;
	struct hiu_stats *stats;
	struct cpufreq_frequency_table *pos, *table;

	table = policy->freq_table;
	if (unlikely(!table))
		return;

	stats = kzalloc(sizeof(*stats), GFP_KERNEL);
	if (!stats)
		return;

	cpufreq_for_each_valid_entry(pos, table)
		count++;

	alloc_size = count * (sizeof(unsigned int) + sizeof(u64));

	stats->freq_table = kzalloc(alloc_size, GFP_KERNEL);
	if (!stats->freq_table)
		goto free_stat;

	stats->time_in_state = (unsigned long long *)(stats->freq_table + count);

	stats->last_level = count;

	cpufreq_for_each_valid_entry(pos, table)
		stats->freq_table[i++] = pos->frequency;

	hiu_data->stats = stats;

	cpufreq_for_each_valid_entry(pos, table) {
		hiu_data->level_offset = pos->driver_data;
		break;
	}

	return;
free_stat:
	kfree(stats);
}

static int exynos_hiu_probe(struct platform_device *pdev)
{
	struct device_node *dn = pdev->dev.of_node;
	int ret;

	hiu_data = kzalloc(sizeof(struct exynos_hiu_data), GFP_KERNEL);
	if (!hiu_data)
		return -ENOMEM;

	hiu_data->enabled = false;

	mutex_init(&hiu_data->lock);

	platform_set_drvdata(pdev, hiu_data);

	hiu_data->base = ioremap(APB_BASE_ADDR, SZ_2M);

	ret = hiu_dt_parsing(dn);
	if (ret) {
		dev_err(&pdev->dev, "Failed to parse HIU data\n");
		goto free_data;
	}

	hiu_data->dn = dn;

	if (hiu_data->operation_mode == INTERRUPT_MODE) {
		init_waitqueue_head(&hiu_data->normdvfs_wait);
		hiu_data->normdvfs_done = false;

		hiu_data->irq = irq_of_parse_and_map(dn, 0);
		if (hiu_data->irq <= 0) {
			dev_err(&pdev->dev, "Failed to get IRQ\n");
			goto free_data;
		}

		ret = devm_request_irq(&pdev->dev, hiu_data->irq, exynos_hiu_irq_handler,
					IRQF_TRIGGER_HIGH | IRQF_SHARED, dev_name(&pdev->dev), hiu_data);

		if (ret) {
			dev_err(&pdev->dev, "Failed to request IRQ handler: %d\n", hiu_data->irq);
			goto free_data;
		}
	}

	INIT_WORK(&hiu_data->work, exynos_hiu_work);
	INIT_WORK(&hiu_data->hwi_work, exynos_hiu_hwi_work);

	ret = sysfs_create_group(&pdev->dev.kobj, &exynos_hiu_attr_group);
	if (ret)
		dev_err(&pdev->dev, "Failed to create Exynos HIU attr group");

	exynos_cpufreq_ready_list_add(&exynos_hiu_ready);

	dev_info(&pdev->dev, "HIU Handler initialization complete\n");
	return 0;

free_data:
	kfree(hiu_data);
	hiu_data = NULL;

	return -ENODEV;
}

static const struct of_device_id of_exynos_hiu_match[] = {
	{ .compatible = "samsung,exynos-hiu", },
	{ },
};

static const struct platform_device_id exynos_hiu_ids[] = {
	{ "exynos-hiu", },
	{ }
};

static struct platform_driver exynos_hiu_driver = {
	.driver = {
		.name = "exynos-hiu",
		.owner = THIS_MODULE,
		.of_match_table = of_exynos_hiu_match,
	},
	.probe		= exynos_hiu_probe,
	.id_table	= exynos_hiu_ids,
};

int __init exynos_hiu_init(void)
{
	return platform_driver_register(&exynos_hiu_driver);
}
device_initcall(exynos_hiu_init);
