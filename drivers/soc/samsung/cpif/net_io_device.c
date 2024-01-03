/*
 * Copyright (C) 2019 Samsung Electronics.
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/if_ether.h>
#include <linux/etherdevice.h>
#include <linux/device.h>
#include <linux/module.h>
#include <trace/events/napi.h>
#include <net/ip.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/netdevice.h>

#include "modem_prj.h"
#include "modem_utils.h"
#include "modem_dump.h"
#ifdef CONFIG_MODEM_IF_LEGACY_QOS
#include "cpif_qos_info.h"
#endif

static int vnet_open(struct net_device *ndev)
{
	struct vnet *vnet = netdev_priv(ndev);
	struct io_device *iod = vnet->iod;
	struct modem_shared *msd = vnet->iod->msd;
	struct link_device *ld;
	int ret;

	atomic_inc(&iod->opened);

	list_for_each_entry(ld, &msd->link_dev_list, list) {
		if (IS_CONNECTED(iod, ld) && ld->init_comm) {
			ret = ld->init_comm(ld, iod);
			if (ret < 0) {
				mif_err("%s<->%s: ERR! init_comm fail(%d)\n",
					iod->name, ld->name, ret);
				atomic_dec(&iod->opened);
				return ret;
			}
		}
	}
	list_add(&iod->node_ndev, &iod->msd->activated_ndev_list);

	netif_start_queue(ndev);

#if defined(CONFIG_SEC_MODEM_S5000AP) && defined(CONFIG_SEC_MODEM_S5100)
	update_rmnet_status(iod, true);
#endif

	mif_err("%s (opened %d) by %s\n",
		iod->name, atomic_read(&iod->opened), current->comm);

	return 0;
}

static int vnet_stop(struct net_device *ndev)
{
	struct vnet *vnet = netdev_priv(ndev);
	struct io_device *iod = vnet->iod;
	struct modem_shared *msd = iod->msd;
	struct link_device *ld;

	if (atomic_dec_and_test(&iod->opened))
		skb_queue_purge(&vnet->iod->sk_rx_q);

	list_for_each_entry(ld, &msd->link_dev_list, list) {
		if (IS_CONNECTED(iod, ld) && ld->terminate_comm)
			ld->terminate_comm(ld, iod);
	}

	spin_lock(&msd->active_list_lock);
	list_del(&iod->node_ndev);
	spin_unlock(&msd->active_list_lock);
	netif_stop_queue(ndev);

#if defined(CONFIG_SEC_MODEM_S5000AP) && defined(CONFIG_SEC_MODEM_S5100)
	update_rmnet_status(iod, false);
#endif

	mif_err("%s (opened %d) by %s\n",
		iod->name, atomic_read(&iod->opened), current->comm);

	return 0;
}

static netdev_tx_t vnet_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct vnet *vnet = netdev_priv(ndev);
	struct io_device *iod = vnet->iod;
	struct link_device *ld = get_current_link(iod);
	struct modem_ctl *mc = iod->mc;
	unsigned int count = skb->len;
	struct sk_buff *skb_new = skb;
	char *buff;
	int ret;
	u8 cfg = 0;
	u16 cfg_sit = 0;
	unsigned int headroom;
	unsigned int tailroom;
	unsigned int tx_bytes;
#ifdef DEBUG_MODEM_IF
	struct timespec ts;
#endif

#ifdef DEBUG_MODEM_IF
	/* Record the timestamp */
	getnstimeofday(&ts);
#endif

#if defined(CONFIG_SEC_MODEM_S5000AP) && defined(CONFIG_SEC_MODEM_S5100)
	ld = get_current_link(get_current_rmnet_tx_iod(iod->ch));
	mc = ld->mc;
#endif

	if (unlikely(!cp_online(mc))) {
		if (!netif_queue_stopped(ndev))
			netif_stop_queue(ndev);
		/* Just drop the TX packet */
		goto drop;
	}

	if (iod->link_header) {
		switch (ld->protocol) {
		case PROTOCOL_SIPC:
			cfg = sipc5_build_config(iod, ld, count);
			headroom = sipc5_get_hdr_len(&cfg);
		break;
		case PROTOCOL_SIT:
			cfg_sit = exynos_build_fr_config(iod, ld, count);
			headroom = EXYNOS_HEADER_SIZE;
		break;
		default:
			mif_err("protocol error %d\n", ld->protocol);
			return -EINVAL;
		}
		if (ld->aligned)
			tailroom = ld->calc_padding_size(headroom + count);
		else
			tailroom = 0;
	} else {
		cfg = 0;
		cfg_sit = 0;
		headroom = 0;
		tailroom = 0;
	}

	tx_bytes = headroom + count + tailroom;

	if (skb_headroom(skb) < headroom || skb_tailroom(skb) < tailroom) {
		skb_new = skb_copy_expand(skb, headroom, tailroom, GFP_ATOMIC);
		if (!skb_new) {
			mif_info("%s: ERR! skb_copy_expand fail\n", iod->name);
			goto retry;
		}
	}

	/* Store the IO device, the link device, etc. */
	skbpriv(skb_new)->iod = iod;
	skbpriv(skb_new)->ld = ld;

	skbpriv(skb_new)->lnk_hdr = iod->link_header;
	skbpriv(skb_new)->sipc_ch = iod->ch;

#ifdef DEBUG_MODEM_IF
	/* Copy the timestamp to the skb */
	memcpy(&skbpriv(skb_new)->ts, &ts, sizeof(struct timespec));
#endif
#if defined(DEBUG_MODEM_IF_IODEV_TX) && defined(DEBUG_MODEM_IF_PS_DATA)
	mif_pkt(iod->ch, "IOD-TX", skb_new);
#endif

	/* Build SIPC5 link header*/
	buff = skb_push(skb_new, headroom);
	if (cfg || cfg_sit) {
		switch (ld->protocol) {
		case PROTOCOL_SIPC:
			sipc5_build_header(iod, buff, cfg, count, 0);
		break;
		case PROTOCOL_SIT:
			exynos_build_header(iod, ld, buff, cfg_sit, 0, count);
		break;
		default:
			mif_err("protocol error %d\n", ld->protocol);
			return -EINVAL;
		}
	}

	/* IP loop-back */
	if (iod->msd->loopback_ipaddr) {
		struct iphdr *ip_header = (struct iphdr *)skb->data;
		if (ip_header->daddr == iod->msd->loopback_ipaddr) {
			swap(ip_header->saddr, ip_header->daddr);
			buff[SIPC5_CH_ID_OFFSET] = DATA_LOOPBACK_CHANNEL;
		}
	}

	/* Apply padding */
	if (tailroom)
		skb_put(skb_new, tailroom);

	ret = ld->send(ld, iod, skb_new);
	if (unlikely(ret < 0)) {
		static DEFINE_RATELIMIT_STATE(_rs, HZ, 100);

		if (ret != -EBUSY) {
			mif_err_limited("%s->%s: ERR! %s->send fail:%d (tx_bytes:%d len:%d)\n",
				iod->name, mc->name, ld->name, ret,
				tx_bytes, count);
			goto drop;
		}

		/* do 100-retry for every 1sec */
		if (__ratelimit(&_rs))
			goto retry;
		goto drop;
	}

	if (ret != tx_bytes) {
		mif_info("%s->%s: WARN! %s->send ret:%d (tx_bytes:%d len:%d)\n",
			iod->name, mc->name, ld->name, ret, tx_bytes, count);
	}

	ndev->stats.tx_packets++;
	ndev->stats.tx_bytes += count;

	/*
	 * If @skb has been expanded to $skb_new, @skb must be freed here.
	 * ($skb_new will be freed by the link device.)
	 */
	if (skb_new != skb)
		dev_consume_skb_any(skb);

	return NETDEV_TX_OK;

retry:
	/*
	If @skb has been expanded to $skb_new, only $skb_new must be freed here
	because @skb will be reused by NET_TX.
	*/
	if (skb_new && skb_new != skb)
		dev_consume_skb_any(skb_new);

	return NETDEV_TX_BUSY;

drop:
	ndev->stats.tx_dropped++;

	dev_kfree_skb_any(skb);

	/*
	If @skb has been expanded to $skb_new, $skb_new must also be freed here.
	*/
	if (skb_new != skb)
		dev_consume_skb_any(skb_new);

	return NETDEV_TX_OK;
}

#if defined(CONFIG_MODEM_IF_LEGACY_QOS) || defined(CONFIG_MODEM_IF_QOS)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
static u16 vnet_select_queue(struct net_device *dev, struct sk_buff *skb,
		struct net_device *sb_dev, select_queue_fallback_t fallback)
#else
static u16 vnet_select_queue(struct net_device *dev, struct sk_buff *skb,
		void *accel_priv, select_queue_fallback_t fallback)
#endif
{
#if defined(CONFIG_MODEM_IF_QOS)
	return (skb && skb->priomark == RAW_HPRIO) ? 1 : 0;
#elif defined(CONFIG_MODEM_IF_LEGACY_QOS)
	return ((skb && skb->truesize == 2) || (skb && skb->sk && cpif_qos_get_node(skb->sk->sk_uid.val))) ? 1 : 0;
#endif
}
#endif

static const struct net_device_ops vnet_ops = {
	.ndo_open = vnet_open,
	.ndo_stop = vnet_stop,
	.ndo_start_xmit = vnet_xmit,
#if defined(CONFIG_MODEM_IF_LEGACY_QOS) || defined(CONFIG_MODEM_IF_QOS)
	.ndo_select_queue = vnet_select_queue,
#endif
};

void vnet_setup(struct net_device *ndev)
{
	ndev->netdev_ops = &vnet_ops;
	ndev->type = ARPHRD_RAWIP;
	ndev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	ndev->addr_len = 0;
	ndev->hard_header_len = 0;
	ndev->tx_queue_len = 1000;
	ndev->mtu = ETH_DATA_LEN;
	ndev->watchdog_timeo = 5 * HZ;
#ifdef CONFIG_MODEM_IF_NET_GRO
	ndev->features |= NETIF_F_GRO;
#endif
}
