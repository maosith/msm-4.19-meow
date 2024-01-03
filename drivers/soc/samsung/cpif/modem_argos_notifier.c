/*
 * Copyright (C) 2015 Samsung Electronics.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/module.h>
#include <linux/notifier.h>

#include "modem_prj.h"
#include "s51xx_pcie.h"

#define MIF_MAX_RPS_STR		8
#define MIF_MAX_NDEV_NUM	8
#define MIF_ARGOS_IPC_LABEL	"IPC"
#define MIF_ARGOS_CLAT_LABEL	"CLAT"

//#define MIF_ARGOS_DEBUG
#define MIF_ARGOS_SUPPORT_PCIE_LANECHANGE

const char *ndev_prefix[2] = {"rmnet", "v4-rmnet"};

enum ndev_prefix_idx {
	IPC = 0,
	CLAT
};

struct argos_notifier {
	struct notifier_block ipc_nb;
	struct notifier_block clat_nb;
	struct modem_ctl *mc;
	unsigned int prev_ipc_rps;
	unsigned int prev_clat_rps;
	unsigned int prev_level;
};

enum prev_mask {
	PREV_MASK_LANE_CHANGED 	= 0,
	PREV_MASK_GEN_CHANGED	= 1,
	PREV_MASK_MAX		= 4,
};

#define PREV_BIT_MASK_LEVEL		(0xF)
#define PREV_BIT_MASK_LANE_CHANGED	((1 << PREV_MASK_LANE_CHANGED) << 4)
#define PREV_BIT_MASK_GEN_CHANGED	((1 << PREV_MASK_GEN_CHANGED) << 4)
#define PREV_BIT_MASK_CLEAR		((PREV_MASK_MAX) << 4)

unsigned int lit_rmnet_rps = 0x06;
module_param(lit_rmnet_rps, uint, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(lit_rmnet_rps, "rps_cpus for rmnetx: LITTLE(only rmnetx up)");

unsigned int lit_rmnet_clat_rps = 0x06;
module_param(lit_rmnet_clat_rps, uint, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(lit_rmnet_clat_rps, "rps_cpus for rmnetx: LITTLE(both up)");

unsigned int lit_clat_rps = 0x30;
module_param(lit_clat_rps, uint, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(lit_clat_rps, "rps_cpus for v4-rmnetx: LITTLE(both up)");

unsigned int big_rmnet_rps = 0x30;
module_param(big_rmnet_rps, uint, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(big_rmnet_rps, "rps_cpus for rmnetx: BIG(only rmnetx up)");

unsigned int big_rmnet_clat_rps = 0x30;
module_param(big_rmnet_clat_rps, uint, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(big_rmnet_clat_rps, "rps_cpus for rmnetx: BIG(both up)");

unsigned int big_clat_rps = 0xc0;
module_param(big_clat_rps, uint, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(big_clat_rps, "rps_cpus for v4-rmnetx: BIG(both up)");

unsigned int mif_rps_thresh = 300;
module_param(mif_rps_thresh, uint, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(mif_rps_thresh, "threshold speed");

int pcie_lane_change_thresh = 6;
module_param(pcie_lane_change_thresh, int, S_IRUGO | S_IWUSR | S_IWGRP);
MODULE_PARM_DESC(pcie_lane_change_thresh, "pcie lane change thresh");

int mif_gro_flush_thresh[] = {100, 200, -1};
long mif_gro_flush_time[] = {10000, 50000, 100000};

static int mif_store_rps_map(struct netdev_rx_queue *queue, char *buf, size_t len)
{
	struct rps_map *old_map, *map;
	cpumask_var_t mask;
	int err, cpu, i;
	static DEFINE_MUTEX(rps_map_mutex);

	if (!alloc_cpumask_var(&mask, GFP_KERNEL)) {
		mif_err("failed to alloc_cpumask\n");
		return -ENOMEM;
	}

	err = bitmap_parse(buf, len, cpumask_bits(mask), nr_cpumask_bits);
	if (err) {
		free_cpumask_var(mask);
		mif_err("failed to parse bitmap\n");
		return err;
	}

	map = kzalloc(max_t(unsigned long,
		RPS_MAP_SIZE(cpumask_weight(mask)), L1_CACHE_BYTES),
		GFP_KERNEL);
	if (!map) {
		free_cpumask_var(mask);
		mif_err("failed to alloc kmem\n");
		return -ENOMEM;
	}

	i = 0;
	for_each_cpu_and(cpu, mask, cpu_online_mask) {
		map->cpus[i++] = cpu;
	}

	if (i) {
		map->len = i;
	} else {
		kfree(map);
		map = NULL;

		free_cpumask_var(mask);
		mif_err("failed to map rps_cpu\n");
		return -EINVAL;
	}

	mutex_lock(&rps_map_mutex);
	old_map = rcu_dereference_protected(queue->rps_map,
		mutex_is_locked(&rps_map_mutex));
	rcu_assign_pointer(queue->rps_map, map);

	if (map)
		static_key_slow_inc(&rps_needed);
	if (old_map)
		static_key_slow_dec(&rps_needed);

	mutex_unlock(&rps_map_mutex);

	if (old_map)
		kfree_rcu(old_map, rcu);

	free_cpumask_var(mask);
#ifdef MIF_ARGOS_DEBUG
	mif_info("map:%d\n", map->len);
#endif
	return map->len;
}

static void mif_update_ndevs_rps(const char *ndev_prefix, unsigned int rps_value)
{
	struct net_device *ndev;
	char ndev_name[IFNAMSIZ];
	char value[MIF_MAX_RPS_STR];
	int ret;
	int i;

	if (!rps_value)
		return;

	snprintf(value, MIF_MAX_RPS_STR, "%x", rps_value);

	for (i = 0; i < MIF_MAX_NDEV_NUM; i++) {
		memset(ndev_name, 0, IFNAMSIZ);
		snprintf(ndev_name, IFNAMSIZ, "%s%d", ndev_prefix, i);

		ndev = dev_get_by_name(&init_net, ndev_name);
		if (!ndev) {
#ifdef MIF_ARGOS_DEBUG
			mif_info("Cannot find %s from init_net\n", ndev_name);
#endif
			continue;
		}

		ret = mif_store_rps_map(ndev->_rx, value, strlen(value));
		dev_put(ndev);

		if (ret < 0) {
			mif_err("failed to update ndevs_rps (%s)\n", ndev_name);
			continue;
		}

#ifdef MIF_ARGOS_DEBUG
		mif_info("%s's rps: 0x%s\n", ndev_name, value);
#endif
	}
}

#ifdef CONFIG_MODEM_IF_NET_GRO
static void mif_argos_notifier_gro_flushtime(unsigned long speed)
{
	int loop;

	for (loop = 0; mif_gro_flush_thresh[loop] != -1; loop++)
		if (speed < mif_gro_flush_thresh[loop])
			break;
	gro_flush_time = mif_gro_flush_time[loop];

	mif_info("Speed: %luMbps, GRO flush time: %ld\n", speed, gro_flush_time);
}
#else
static inline void mif_argos_notifier_gro_flushtime(unsigned long speed) {}
#endif

#ifdef MIF_ARGOS_SUPPORT_PCIE_LANECHANGE
static int mif_argos_pcie_rc_lanechange(struct modem_ctl *mc, int lane)
{
	unsigned long flags;
	int ret;

	if (!mc) {
		mif_info("modem_ctrl not initialized\n");
		return -1;
	}

	spin_lock_irqsave(&mc->pcie_tx_lock, flags);
	ret = exynos_pcie_rc_lanechange(1/*CP*/, lane);
	spin_unlock_irqrestore(&mc->pcie_tx_lock, flags);

	return ret;
}

static void mif_argos_notifier_pcie_ctrl(struct argos_notifier *nf, void *data)
{
	struct modem_ctl *mc = nf->mc;
	int new_level    = *(int *)data;
	int lane_changed = (nf->prev_level & PREV_BIT_MASK_LANE_CHANGED) >> 4;

#ifdef MIF_ARGOS_DEBUG
	mif_info("level :[%d -> %d], lane=%d\n",
			(nf->prev_level & PREV_BIT_MASK_LEVEL),
			new_level, lane_changed);
#endif

	/* change pcie lane to 2 for high tput */
	if (new_level >= pcie_lane_change_thresh && !lane_changed) {
		nf->prev_level = new_level;
		if (!mif_argos_pcie_rc_lanechange(mc, 2)) {
			nf->prev_level |= PREV_BIT_MASK_LANE_CHANGED;
		} else {
			mif_err("fail to change PCI lane 2\n");
		}
	} else if (lane_changed && new_level == -1) {
		/* lane chaned bit cleared */
		nf->prev_level = (new_level & PREV_BIT_MASK_LEVEL);
	}
}
#else
static inline void mif_argos_notifier_pcie_ctrl(struct argos_notifier *nf, void *data) {}
#endif

static int mif_argos_notifier_ipc(struct notifier_block *nb, unsigned long speed, void *data)
{
	struct argos_notifier *nf = container_of(nb, struct argos_notifier, ipc_nb);
	unsigned int prev_ipc_rps = nf->prev_ipc_rps, new_ipc_rps = 0;

#ifdef MIF_ARGOS_DEBUG
	mif_info("Speed: %luMbps, IPC|CLAT: 0x%02x|0x%02x\n",
			speed, prev_ipc_rps, nf->prev_clat_rps);
#endif

	if (speed >= mif_rps_thresh)
		new_ipc_rps = nf->prev_clat_rps ? big_rmnet_clat_rps : big_rmnet_rps;
	else
		new_ipc_rps = nf->prev_clat_rps ? lit_rmnet_clat_rps : lit_rmnet_rps;

	if (prev_ipc_rps != new_ipc_rps) {
		mif_update_ndevs_rps(ndev_prefix[IPC], new_ipc_rps);
		nf->prev_ipc_rps = new_ipc_rps;

		mif_info("Speed: %luMbps, IPC|CLAT: 0x%02x|0x%02x -> 0x%02x|0x%02x\n",
			speed, prev_ipc_rps, nf->prev_clat_rps, new_ipc_rps, nf->prev_clat_rps);
	}

	mif_argos_notifier_gro_flushtime(speed);

	mif_argos_notifier_pcie_ctrl(nf, data);

	return NOTIFY_OK;
}

#ifdef SUPPORT_CLATD
static int mif_argos_notifier_clat(struct notifier_block *nb, unsigned long speed, void *data)
{
	struct argos_notifier *nf = container_of(nb, struct argos_notifier, clat_nb);
	unsigned int prev_clat_rps = nf->prev_clat_rps, new_clat_rps = 0;
	unsigned int prev_ipc_rps = nf->prev_ipc_rps, new_ipc_rps = 0;
	bool changed = false;

#ifdef MIF_ARGOS_DEBUG
	mif_info("Speed: %luMbps, IPC|CLAT: 0x%02x|0x%02x\n",
			speed, prev_ipc_rps, nf->prev_clat_rps);
#endif

	/* If already set to Big core */
	if (nf->prev_ipc_rps > 0xF) {
		new_clat_rps = (speed > 0) ? big_clat_rps : 0;
		new_ipc_rps = (new_clat_rps > 0) ? big_rmnet_clat_rps : big_rmnet_rps;
	} else {
		new_clat_rps = (speed > 0) ? lit_clat_rps : 0;
		new_ipc_rps = (new_clat_rps > 0) ? lit_rmnet_clat_rps : lit_rmnet_rps;
	}

	if (prev_clat_rps != new_clat_rps) {
		mif_update_ndevs_rps(ndev_prefix[CLAT], new_clat_rps);
		nf->prev_clat_rps = new_clat_rps;
		changed = true;
	}

	if (prev_ipc_rps != new_ipc_rps) {
		mif_update_ndevs_rps(ndev_prefix[IPC], new_ipc_rps);
		nf->prev_ipc_rps = new_ipc_rps;
		changed = true;
	}

	if (changed) {
		mif_info("Speed: %luMbps, IPC|CLAT: 0x%02x|0x%02x -> 0x%02x|0x%02x\n",
			speed, prev_ipc_rps, prev_clat_rps, new_ipc_rps, new_clat_rps);
	}

	mif_argos_notifier_gro_flushtime(speed);

	mif_argos_notifier_pcie_ctrl(nf, data);

	return NOTIFY_OK;
}
#endif

int mif_init_argos_notifier(struct modem_ctl *mc)
{
	struct argos_notifier *argos_nf;
	int ret;

	mif_info("++\n");

	argos_nf = kzalloc(sizeof(struct argos_notifier), GFP_ATOMIC);
	if (!argos_nf) {
		mif_err("failed to allocate argos_nf\n");
		return -ENOMEM;
	}

	argos_nf->mc = mc;

	argos_nf->ipc_nb.notifier_call = mif_argos_notifier_ipc;
	ret = sec_argos_register_notifier(&argos_nf->ipc_nb, MIF_ARGOS_IPC_LABEL);
	if (ret < 0) {
		mif_err("failed to register ipc_nb(%d)\n", ret);
		goto exit;
	}

#ifdef SUPPORT_CLATD
	argos_nf->clat_nb.notifier_call = mif_argos_notifier_clat;
	ret = sec_argos_register_notifier(&argos_nf->clat_nb, MIF_ARGOS_CLAT_LABEL);
	if (ret < 0) {
		mif_err("failed to register clat_nb(%d)\n", ret);
		sec_argos_unregister_notifier(&argos_nf->ipc_nb, MIF_ARGOS_IPC_LABEL);
		goto exit;
	}
#endif
	/* default rmnetx rps: 0x06 */
	mif_update_ndevs_rps(ndev_prefix[IPC], lit_rmnet_rps);

	mif_info("--\n");
	return 0;

exit:
	kfree(argos_nf);
	return ret;
}

