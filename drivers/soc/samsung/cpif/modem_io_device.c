/*
 * Copyright (C) 2010 Samsung Electronics.
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

#ifdef CONFIG_LINK_FORWARD
#include <linux/linkforward.h>
#endif

#if defined(CONFIG_SEC_MODEM_S5000AP) && defined(CONFIG_SEC_MODEM_S5100)
#include <linux/modem_notifier.h>
#endif

#ifdef CONFIG_USB_CONFIGFS_F_MBIM
#include <linux/mbim_queue.h>
#endif

#include "modem_prj.h"
#include "modem_utils.h"
#include "modem_dump.h"
#include "cpif_clat_info.h"
#include "cpif_tethering_info.h"

#ifdef CONFIG_MCPS
#include "../../../mcps/mcps.h"
#endif

static ssize_t show_waketime(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	unsigned int msec;
	char *p = buf;
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct io_device *iod = container_of(miscdev, struct io_device,
			miscdev);

	msec = jiffies_to_msecs(iod->waketime);

	p += sprintf(buf, "raw waketime : %ums\n", msec);

	return p - buf;
}

static ssize_t store_waketime(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long msec;
	int ret;
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct io_device *iod = container_of(miscdev, struct io_device,
			miscdev);

	if (!iod) {
		pr_err("mif: %s: INVALID IO device\n", miscdev->name);
		return -EINVAL;
	}

	ret = kstrtoul(buf, 10, &msec);
	if (ret)
		return count;

	if (!msec) {
		mif_info("%s: (%ld) is not valied, use previous value(%d)\n",
			iod->name, msec,
			jiffies_to_msecs(iod->mc->iod->waketime));
		return count;
	}

	iod->waketime = msecs_to_jiffies(msec);
#ifdef DEBUG_MODEM_IF
	mif_err("%s: waketime = %lu ms\n", iod->name, msec);
#endif

	if (iod->format == IPC_MULTI_RAW) {
		struct modem_shared *msd = iod->msd;
		unsigned int i;

		for (i = SIPC_CH_ID_PDP_0; i < SIPC_CH_ID_BT_DUN; i++) {
			iod = get_iod_with_channel(msd, i);
			if (iod) {
				iod->waketime = msecs_to_jiffies(msec);
#ifdef DEBUG_MODEM_IF
				mif_err("%s: waketime = %lu ms\n",
					iod->name, msec);
#endif
			}
		}
	}

	return count;
}

static struct device_attribute attr_waketime =
	__ATTR(waketime, S_IRUGO | S_IWUSR, show_waketime, store_waketime);

static ssize_t show_loopback(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct modem_shared *msd =
		container_of(miscdev, struct io_device, miscdev)->msd;
	unsigned char *ip = (unsigned char *)&msd->loopback_ipaddr;
	char *p = buf;

	p += sprintf(buf, "%u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);

	return p - buf;
}

static ssize_t store_loopback(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct modem_shared *msd =
		container_of(miscdev, struct io_device, miscdev)->msd;

	msd->loopback_ipaddr = ipv4str_to_be32(buf, count);

	return count;
}

static struct device_attribute attr_loopback =
	__ATTR(loopback, S_IRUGO | S_IWUSR, show_loopback, store_loopback);

static void iodev_showtxlink(struct io_device *iod, void *args)
{
	char **p = (char **)args;
	struct link_device *ld = get_current_link(iod);

	if (iod->io_typ == IODEV_NET && IS_CONNECTED(iod, ld))
		*p += sprintf(*p, "%s<->%s\n", iod->name, ld->name);
}

static ssize_t show_txlink(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct modem_shared *msd =
		container_of(miscdev, struct io_device, miscdev)->msd;
	char *p = buf;

	iodevs_for_each(msd, iodev_showtxlink, &p);

	return p - buf;
}

static ssize_t store_txlink(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	/* don't change without gpio dynamic switching */
	return -EINVAL;
}

static struct device_attribute attr_txlink =
	__ATTR(txlink, S_IRUGO | S_IWUSR, show_txlink, store_txlink);

static inline void iodev_lock_wlock(struct io_device *iod)
{
	wake_lock_timeout(&iod->wakelock,
		iod->waketime ?: msecs_to_jiffies(200));
}

static int queue_skb_to_iod(struct sk_buff *skb, struct io_device *iod)
{
	struct sk_buff_head *rxq = &iod->sk_rx_q;
	int len = skb->len;

	if (iod->attrs & IODEV_ATTR(ATTR_NO_CHECK_MAXQ))
		goto enqueue;

	if (rxq->qlen > MAX_IOD_RXQ_LEN) {
		mif_err_limited("%s: application may be dead (rxq->qlen %d > %d)\n",
			iod->name, rxq->qlen, MAX_IOD_RXQ_LEN);
		dev_kfree_skb_any(skb);
		goto exit;
	}

enqueue:
	mif_debug("%s: rxq->qlen = %d\n", iod->name, rxq->qlen);
	skb_queue_tail(rxq, skb);

exit:
	wake_up(&iod->wq);
	return len;
}

static int gather_multi_frame(struct sipc5_link_header *hdr,
			      struct sk_buff *skb)
{
	struct multi_frame_control ctrl = hdr->ctrl;
	struct io_device *iod = skbpriv(skb)->iod;
	struct modem_ctl *mc = iod->mc;
	struct sk_buff_head *multi_q = &iod->sk_multi_q[ctrl.id];
	int len = skb->len;

#ifdef DEBUG_MODEM_IF
	/* If there has been no multiple frame with this ID, ... */
	if (skb_queue_empty(multi_q)) {
		struct sipc_fmt_hdr *fh = (struct sipc_fmt_hdr *)skb->data;
		mif_err("%s<-%s: start of multi-frame (ID:%d len:%d)\n",
			iod->name, mc->name, ctrl.id, fh->len);
	}
#endif
	skb_queue_tail(multi_q, skb);

	if (ctrl.more) {
		/* The last frame has not arrived yet. */
		mif_err("%s<-%s: recv multi-frame (ID:%d rcvd:%d)\n",
			iod->name, mc->name, ctrl.id, skb->len);
	} else {
		struct sk_buff_head *rxq = &iod->sk_rx_q;
		unsigned long flags;

		/* It is the last frame because the "more" bit is 0. */
		mif_err("%s<-%s: end of multi-frame (ID:%d rcvd:%d)\n",
			iod->name, mc->name, ctrl.id, skb->len);

		spin_lock_irqsave(&rxq->lock, flags);
		skb_queue_splice_tail_init(multi_q, rxq);
		spin_unlock_irqrestore(&rxq->lock, flags);

		wake_up(&iod->wq);
	}

	return len;
}

static int gather_multi_frame_sit(struct exynos_link_header *hdr, struct sk_buff *skb)
{
	u16 ctrl = hdr->cfg;
	struct io_device *iod = skbpriv(skb)->iod;
	struct modem_ctl *mc = iod->mc;
	struct sk_buff_head *multi_q = &iod->sk_multi_q[exynos_multi_packet_index(ctrl)];
	int len = skb->len;

#ifdef DEBUG_MODEM_IF_LINK_RX
	/* If there has been no multiple frame with this ID, ... */
	if (skb_queue_empty(multi_q)) {
		mif_err("%s<-%s: start of multi-frame (pkt_index:%d fr_index:%d len:%d)\n",
			iod->name, mc->name, exynos_multi_packet_index(ctrl),
			exynos_multi_frame_index(ctrl), hdr->len);
	}
#endif
	skb_queue_tail(multi_q, skb);

	if (!exynos_multi_last(ctrl)) {
		/* It is the last frame because the "more" bit is 0. */
		mif_err("%s<-%s: recv of multi-frame (CH_ID:0x%02x rcvd:%d)\n",
			iod->name, mc->name, hdr->ch_id, skb->len);
	} else {
		struct sk_buff_head *rxq = &iod->sk_rx_q;
		struct sk_buff *skb_new;
		struct sk_buff *skb_tmp;
		int total_len = 0;

		/* The last frame has not arrived yet. */
		mif_err("%s<-%s: end multi-frame (CH_ID:0x%02x rcvd:%d)\n",
			iod->name, mc->name, hdr->ch_id, skb->len);

		/* check totoal multi packet size */
		skb_tmp = skb_peek(multi_q);

		while (skb_tmp != NULL) {
			total_len += skb_tmp->len;
			skb_tmp = skb_peek_next(skb_tmp, multi_q);
		}
		mif_info("Total multi-frame packet size is %d\n", total_len);

		skb_new = alloc_skb(total_len, GFP_ATOMIC);
		if (unlikely(skb_new == NULL)) {
			mif_err("ERR - alloc_skb fail\n");
			skb_queue_purge(multi_q);
			return -ENOMEM;
		}

		while (!skb_queue_empty(multi_q)) {
			skb_tmp = skb_dequeue(multi_q);
			memcpy(skb_put(skb_new, skb_tmp->len), skb_tmp->data, skb_tmp->len);
		}

		skb_trim(skb_new, skb_new->len);
		skb_queue_tail(rxq, skb_new);

		skb_queue_purge(multi_q);
		skb_queue_head_init(multi_q);

		wake_up(&iod->wq);
	}

	return len;
}

static inline int rx_frame_with_link_header(struct sk_buff *skb)
{
	struct sipc5_link_header *hdr;
	struct exynos_link_header *hdr_sit;
	bool multi_frame = skbpriv(skb)->ld->is_multi_frame(skb->data);
	int hdr_len = skbpriv(skb)->ld->get_hdr_len(skb->data);

	switch (skbpriv(skb)->ld->protocol) {
	case PROTOCOL_SIPC:
		/* Remove SIPC5 link header */
		hdr = (struct sipc5_link_header *)skb->data;
		skb_pull(skb, hdr_len);

		if (multi_frame)
			return gather_multi_frame(hdr, skb);
		else
			return queue_skb_to_iod(skb, skbpriv(skb)->iod);
		break;
	case PROTOCOL_SIT:
		hdr_sit = (struct exynos_link_header *)skb->data;
		skb_pull(skb, EXYNOS_HEADER_SIZE);

		if (multi_frame)
			return gather_multi_frame_sit(hdr_sit, skb);
		else
			return queue_skb_to_iod(skb, skbpriv(skb)->iod);
		break;
	default:
		mif_err("protocol error %d\n", skbpriv(skb)->ld->protocol);
		return -EINVAL;
	}

	return 0;
}

static int rx_fmt_ipc(struct sk_buff *skb)
{
	if (skbpriv(skb)->lnk_hdr)
		return rx_frame_with_link_header(skb);
	else
		return queue_skb_to_iod(skb, skbpriv(skb)->iod);
}

static int rx_raw_misc(struct sk_buff *skb)
{
	struct io_device *iod = skbpriv(skb)->iod;

	if (skbpriv(skb)->lnk_hdr) {
		/* Remove the SIPC5 link header */
		skb_pull(skb, skbpriv(skb)->ld->get_hdr_len(skb->data));
	}

	return queue_skb_to_iod(skb, iod);
}

#ifdef CONFIG_MODEM_IF_NET_GRO
static int check_gro_support(struct sk_buff *skb)
{
	switch (skb->data[0] & 0xF0) {
	case 0x40:
		return (ip_hdr(skb)->protocol == IPPROTO_TCP);

	case 0x60:
		return (ipv6_hdr(skb)->nexthdr == IPPROTO_TCP);
	}
	return 0;
}
#else
static int check_gro_support(struct sk_buff *skb)
{
	return 0;
}
#endif

#ifdef CONFIG_USB_CONFIGFS_F_MBIM
static bool check_pcket_filter(struct sk_buff *skb)
{
	struct link_device *ld = skbpriv(skb)->ld;
	u8 ch = skbpriv(skb)->sipc_ch;
	u8 rmnet_type = ld->get_rmnet_type(ch);
	int i, j;
	u32 filters_count = 0;
	u32 filter_size = 0;
	u8 filter = 0;
	u8 mask = 0;

	filters_count = ld->packet_filter_table.rmnet[rmnet_type].filters_count;

	if (filters_count == 0)
		return false;

	for (i = 0; i < filters_count; i++) {
		filter_size = ld->packet_filter_table.rmnet[rmnet_type].single_filter[i].filter_size;
		if (skb->data_len != filter_size)
			continue;

		for (j = 0; j < filter_size; j++) {
			filter = ld->packet_filter_table.rmnet[rmnet_type].single_filter[i].filter[j];
			mask = ld->packet_filter_table.rmnet[rmnet_type].single_filter[i].mask[j];

			if (filter != (skb->data[j] & mask)) {
				mif_info("The packet is not in this filter. (session_id %d, filters_count %d)\n", rmnet_type, i);
				break;
			}
		}
		if (j == filter_size)
			return true;
	}
	return false;
}
#endif

static int rx_multi_pdp(struct sk_buff *skb)
{
	struct link_device *ld = skbpriv(skb)->ld;
	struct io_device *iod = skbpriv(skb)->iod;
	struct net_device *ndev;
	struct iphdr *iphdr;
	int len = skb->len;
	int ret = 0;
	int __maybe_unused l2forward = 0;
#ifdef CONFIG_USB_CONFIGFS_F_MBIM
	u8 ch = skbpriv(skb)->sipc_ch;
	u8 rmnet_type = ld->get_rmnet_type(ch);
#endif

	ndev = iod->ndev;
	if (!ndev) {
		mif_info("%s: ERR! no iod->ndev\n", iod->name);
		return -ENODEV;
	}

#ifdef CONFIG_USB_CONFIGFS_F_MBIM
	if (mbim_read_opened() != 1) {
		mif_err_limited("ERR! mbim_read is not opened\n");
		return -ENODEV;
	}
#endif

	if (skbpriv(skb)->lnk_hdr) {
		/* Remove the SIPC5 link header */
		skb_pull(skb, skbpriv(skb)->ld->get_hdr_len(skb->data));
	}

	skb->dev = ndev;
	ndev->stats.rx_packets++;
	ndev->stats.rx_bytes += skb->len;
#if defined(CONFIG_CPIF_TP_MONITOR)
	tpmon_add_rx_bytes(skb->len);
#endif

	/* check the version of IP */
	iphdr = (struct iphdr *)skb->data;
	if (iphdr->version == IPv6)
		skb->protocol = htons(ETH_P_IPV6);
	else
		skb->protocol = htons(ETH_P_IP);

#ifdef DEBUG_MODEM_IF_IP_DATA
	print_ipv4_packet(skb->data, RX);
#endif
#if defined(DEBUG_MODEM_IF_IODEV_RX) && defined(DEBUG_MODEM_IF_PS_DATA)
	mif_pkt(iod->ch, "IOD-RX", skb);
#endif

	skb_reset_transport_header(skb);
	skb_reset_network_header(skb);
	skb_reset_mac_header(skb);

#ifdef CONFIG_USB_CONFIGFS_F_MBIM
	/* packet capture for MBIM device */
	mif_queue_skb(skb, RX);

	if (ld->is_modern_standby) {
		if (!check_pcket_filter(skb))
			return -ENODEV;
	}

	if (ld->pdn_table.pdn[rmnet_type].dl_dst == PC) {
		ret = mbim_queue_head(skb);
		return len;
	}
#endif

#ifdef CONFIG_LINK_FORWARD
	/* Link Forward */
	l2forward = get_linkforward_mode() ?
		linkforward_manip_skb(skb, LINK_FORWARD_DIR_REPLY) : 0;
#endif

#if defined(CONFIG_CP_GRO_EXCEPTION)
	if (l2forward || (is_tethering_upstream_device(skb->dev->name) && is_heading_toward_clat(skb))) {
#else
	if (l2forward) {
#endif
		goto cp_gro_exception;
	} else {
#ifdef CONFIG_MCPS
		if (!mcps_try_gro(skb))
			return len;
#endif
		if (!check_gro_support(skb))
			goto cp_gro_exception;

		ret = napi_gro_receive(napi_get_current(), skb);
		if (ret == GRO_DROP)
			mif_err_limited("%s: %s<-%s: ERR! napi_gro_receive\n",
					ld->name, iod->name, iod->mc->name);

		if (ld->gro_flush)
			ld->gro_flush(ld);
	}
	return len;

cp_gro_exception:
	ret = netif_receive_skb(skb);
	if (ret != NET_RX_SUCCESS)
		mif_err_limited("%s: %s<-%s: ERR! netif_receive_skb\n",
				ld->name, iod->name, iod->mc->name);
	return len;
}

static int rx_demux(struct link_device *ld, struct sk_buff *skb)
{
	struct io_device *iod;
	u8 ch = skbpriv(skb)->sipc_ch;
	struct link_device *skb_ld = skbpriv(skb)->ld;

	if (unlikely(ch == 0)) {
		mif_err("%s: ERR! invalid ch# %d\n", ld->name, ch);
		return -ENODEV;
	}

	/* IP loopback */
	if (ch == DATA_LOOPBACK_CHANNEL && ld->msd->loopback_ipaddr)
		ch = SIPC_CH_ID_PDP_0;

	iod = link_get_iod_with_channel(ld, ch);
	if (unlikely(!iod)) {
		mif_err("%s: ERR! no iod with ch# %d\n", ld->name, ch);
		return -ENODEV;
	}

	if (atomic_read(&iod->opened) <= 0) {
		mif_err_limited("%s: ERR! %s is not opened\n",
				ld->name, iod->name);
		return -ENODEV;
	}

	switch (skb_ld->protocol) {
	case PROTOCOL_SIPC:
		if (skb_ld->is_fmt_ch(ch)){
			iod->mc->receive_first_ipc = 1;
			return rx_fmt_ipc(skb);
		} else if (skb_ld->is_ps_ch(ch))
			return rx_multi_pdp(skb);
		else
			return rx_raw_misc(skb);
		break;
	case PROTOCOL_SIT:
		if (skb_ld->is_fmt_ch(ch) || skb_ld->is_wfs0_ch(ch))
			return rx_fmt_ipc(skb);
		else if (skb_ld->is_ps_ch(ch) || skb_ld->is_embms_ch(ch))
			return rx_multi_pdp(skb);
		else
			return rx_raw_misc(skb);
		break;
	default:
		mif_err("protocol error %d\n", skb_ld->protocol);
		return -EINVAL;
	}
}

static int io_dev_recv_skb_single_from_link_dev(struct io_device *iod,
						struct link_device *ld,
						struct sk_buff *skb)
{
	int err;

	iodev_lock_wlock(iod);

	if (skbpriv(skb)->lnk_hdr && ld->aligned) {
		/* Cut off the padding in the current SIPC5 frame */
		skb_trim(skb, skbpriv(skb)->ld->get_frame_len(skb->data));
	}

	err = rx_demux(ld, skb);
	if (err < 0) {
		mif_err_limited("%s<-%s: ERR! rx_demux fail (err %d)\n",
				iod->name, ld->name, err);
	}

	return err;
}

/*
 * @brief	called by a link device with the "recv_net_skb" method to upload each PS
 *	data packet to the network protocol stack
 */
static int io_dev_recv_net_skb_from_link_dev(struct io_device *iod,
					     struct link_device *ld,
					     struct sk_buff *skb)
{
#if defined(CONFIG_SEC_MODEM_S5000AP) && defined(CONFIG_SEC_MODEM_S5100)
	if (iod->link_type == LINKDEV_PCIE) {
		iod = get_static_rmnet_iod_with_channel(iod->ch);
		skbpriv(skb)->iod = iod;
	}
#endif
	if (unlikely(atomic_read(&iod->opened) <= 0)) {
		struct modem_ctl *mc = iod->mc;
		mif_err_limited("%s: %s<-%s: ERR! %s is not opened\n",
				ld->name, iod->name, mc->name, iod->name);
		return -ENODEV;
	}

	iodev_lock_wlock(iod);

	return rx_multi_pdp(skb);
}

static void io_dev_sim_state_changed(struct io_device *iod, bool sim_online)
{
	if (atomic_read(&iod->opened) == 0) {
		mif_info("%s: ERR! not opened\n", iod->name);
	} else if (iod->mc->sim_state.online == sim_online) {
		mif_info("%s: SIM state not changed\n", iod->name);
	} else {
		iod->mc->sim_state.online = sim_online;
		iod->mc->sim_state.changed = true;
		mif_info("%s: SIM state changed {online %d, changed %d}\n",
			iod->name, iod->mc->sim_state.online,
			iod->mc->sim_state.changed);
		wake_up(&iod->wq);
	}
}

void iodev_dump_status(struct io_device *iod, void *args)
{
	if (iod->format == IPC_RAW && iod->io_typ == IODEV_NET) {
		struct link_device *ld = get_current_link(iod);
		mif_com_log(iod->mc->msd, "%s: %s\n", iod->name, ld->name);
	}
}

u16 exynos_build_fr_config(struct io_device *iod, struct link_device *ld,
				unsigned int count)
{
	u16 fr_cfg = 0;
	u8 frames = 0;
	u8 *packet_index  = &iod->packet_index;

	if (iod->format > IPC_DUMP)
		return 0;

	if (iod->format >= IPC_BOOT)
		return fr_cfg |= (EXYNOS_SINGLE_MASK << 8);

	if ((count + EXYNOS_HEADER_SIZE) <= SZ_2K) {
		fr_cfg |= (EXYNOS_SINGLE_MASK << 8);
	} else {
		frames = count / (SZ_2K - EXYNOS_HEADER_SIZE);
		frames = (count % (SZ_2K - EXYNOS_HEADER_SIZE)) ? frames : frames - 1;

		fr_cfg |= ((EXYNOS_MULTI_START_MASK | (0x3f & ++*packet_index)) << 8) | frames;
	}

	return fr_cfg;
}

void exynos_build_header(struct io_device *iod, struct link_device *ld,
				u8 *buff, u16 cfg, u8 ctl, size_t count)
{
	u16 *exynos_header = (u16 *)(buff + EXYNOS_START_OFFSET);
	u16 *frame_seq = (u16 *)(buff + EXYNOS_FRAME_SEQ_OFFSET);
	u16 *frag_cfg = (u16 *)(buff + EXYNOS_FRAG_CONFIG_OFFSET);
	u16 *size = (u16 *)(buff + EXYNOS_LEN_OFFSET);
	struct exynos_seq_num *seq_num = &(iod->seq_num);

	*exynos_header = EXYNOS_START_MASK;
	*frame_seq = ++seq_num->frame_cnt;
	*frag_cfg = cfg;
	*size = (u16)(EXYNOS_HEADER_SIZE + count);
	buff[EXYNOS_CH_ID_OFFSET] = iod->ch;

	if (cfg == EXYNOS_SINGLE_MASK)
		*frag_cfg = cfg;

	buff[EXYNOS_CH_SEQ_OFFSET] = ++seq_num->ch_cnt[iod->ch];
}

static inline void sipc5_inc_info_id(struct io_device *iod)
{
	spin_lock(&iod->info_id_lock);
	iod->info_id = (iod->info_id + 1) & 0x7F;
	spin_unlock(&iod->info_id_lock);
}

u8 sipc5_build_config(struct io_device *iod, struct link_device *ld,
			     unsigned int count)
{
	u8 cfg = SIPC5_START_MASK;

	if (iod->format > IPC_DUMP)
		return 0;

	if (ld->aligned)
		cfg |= SIPC5_PADDING_EXIST;

	if (iod->max_tx_size > 0 &&
		(count + SIPC5_MIN_HEADER_SIZE) > iod->max_tx_size) {
		mif_info("%s: MULTI_FRAME_CFG: count=%u\n", iod->name, count);
		cfg |= SIPC5_MULTI_FRAME_CFG;
		sipc5_inc_info_id(iod);
	}

	return cfg;
}

void sipc5_build_header(struct io_device *iod, u8 *buff, u8 cfg,
		unsigned int tx_bytes, unsigned int remains)
{
	u16 *sz16 = (u16 *)(buff + SIPC5_LEN_OFFSET);
	u32 *sz32 = (u32 *)(buff + SIPC5_LEN_OFFSET);
	unsigned int hdr_len = sipc5_get_hdr_len(&cfg);
	u8 ctrl;

	/* Store the config field and the channel ID field */
	buff[SIPC5_CONFIG_OFFSET] = cfg;
	buff[SIPC5_CH_ID_OFFSET] = iod->ch;

	/* Store the frame length field */
	if (sipc5_ext_len(buff))
		*sz32 = (u32)(hdr_len + tx_bytes);
	else
		*sz16 = (u16)(hdr_len + tx_bytes);

	/* Store the control field */
	if (sipc5_multi_frame(buff)) {
		ctrl = (remains > 0) ? 1 << 7 : 0;
		ctrl |= iod->info_id;
		buff[SIPC5_CTRL_OFFSET] = ctrl;
		mif_info("MULTI: ctrl=0x%x(tx_bytes:%u, remains:%u)\n",
				ctrl, tx_bytes, remains);
	}
}

static int dummy_net_open(struct net_device *ndev)
{
	return -EINVAL;
}
static const struct net_device_ops dummy_net_ops = {
	.ndo_open = dummy_net_open,
};

int sipc5_init_io_device(struct io_device *iod)
{
	int ret = 0;
	int i;
	struct vnet *vnet;

	if (iod->attrs & IODEV_ATTR(ATTR_SBD_IPC))
		iod->sbd_ipc = true;

	if (iod->attrs & IODEV_ATTR(ATTR_NO_LINK_HEADER))
		iod->link_header = false;
	else
		iod->link_header = true;

	iod->sim_state_changed = io_dev_sim_state_changed;

	/* Get data from link device */
	iod->recv_skb_single = io_dev_recv_skb_single_from_link_dev;
	iod->recv_net_skb = io_dev_recv_net_skb_from_link_dev;

	/* Register misc or net device */
	switch (iod->io_typ) {
	case IODEV_BOOTDUMP:
		init_waitqueue_head(&iod->wq);
		skb_queue_head_init(&iod->sk_rx_q);

		iod->miscdev.minor = MISC_DYNAMIC_MINOR;
		iod->miscdev.name = iod->name;
		iod->miscdev.fops = get_bootdump_io_fops();

		ret = misc_register(&iod->miscdev);
		if (ret)
			mif_info("%s: ERR! misc_register failed\n", iod->name);
		break;

	case IODEV_IPC:
		init_waitqueue_head(&iod->wq);
		skb_queue_head_init(&iod->sk_rx_q);

		iod->miscdev.minor = MISC_DYNAMIC_MINOR;
		iod->miscdev.name = iod->name;
		iod->miscdev.fops = get_ipc_io_fops();

		ret = misc_register(&iod->miscdev);
		if (ret)
			mif_info("%s: ERR! misc_register failed\n", iod->name);

		if (iod->ch == SIPC_CH_ID_CPLOG1) {
			iod->ndev = alloc_netdev(0, iod->name,
					NET_NAME_UNKNOWN, vnet_setup);
			if (!iod->ndev) {
				mif_info("%s: ERR! alloc_netdev fail\n", iod->name);
				return -ENOMEM;
			}

			iod->ndev->netdev_ops = &dummy_net_ops;
			ret = register_netdev(iod->ndev);
			if (ret) {
				mif_info("%s: ERR! register_netdev fail\n", iod->name);
				free_netdev(iod->ndev);
			}

			vnet = netdev_priv(iod->ndev);
			vnet->iod = iod;
			mif_info("iod:%s, both registerd\n", iod->name);
		}
		break;

	case IODEV_NET:
		skb_queue_head_init(&iod->sk_rx_q);
		INIT_LIST_HEAD(&iod->node_ndev);

		iod->ndev = alloc_netdev_mqs(sizeof(struct vnet),
					iod->name, NET_NAME_UNKNOWN, vnet_setup,
					MAX_NDEV_TX_Q, MAX_NDEV_RX_Q);
		if (!iod->ndev) {
			mif_info("%s: ERR! alloc_netdev fail\n", iod->name);
			return -ENOMEM;
		}

#if defined(CONFIG_SEC_MODEM_S5000AP) && defined(CONFIG_SEC_MODEM_S5100)
		if (!strncmp(iod->name, "dummy", 5)) {
			insert_rmnet_iod_with_channel(iod, RMNET_LINK_5G);
			goto jump_reg;
		} else
			insert_rmnet_iod_with_channel(iod, RMNET_LINK_4G);
#endif
		ret = register_netdev(iod->ndev);
		if (ret) {
			mif_info("%s: ERR! register_netdev fail\n", iod->name);
			free_netdev(iod->ndev);
		}

#if defined(CONFIG_SEC_MODEM_S5000AP) && defined(CONFIG_SEC_MODEM_S5100)
jump_reg:
#endif
		mif_debug("iod 0x%pK\n", iod);
		vnet = netdev_priv(iod->ndev);
		mif_debug("vnet 0x%pK\n", vnet);
		vnet->iod = iod;

#if defined(CONFIG_CPIF_TP_MONITOR)
		INIT_LIST_HEAD(&iod->node_all_ndev);
		tpmon_add_net_node(&iod->node_all_ndev);
#endif
		break;

	case IODEV_DUMMY:
		skb_queue_head_init(&iod->sk_rx_q);

		iod->miscdev.minor = MISC_DYNAMIC_MINOR;
		iod->miscdev.name = iod->name;

		ret = misc_register(&iod->miscdev);
		if (ret)
			mif_info("%s: ERR! misc_register fail\n", iod->name);

		ret = device_create_file(iod->miscdev.this_device,
					&attr_waketime);
		if (ret)
			mif_info("%s: ERR! device_create_file fail\n",
				iod->name);

		ret = device_create_file(iod->miscdev.this_device,
				&attr_loopback);
		if (ret)
			mif_err("failed to create `loopback file' : %s\n",
					iod->name);

		ret = device_create_file(iod->miscdev.this_device,
				&attr_txlink);
		if (ret)
			mif_err("failed to create `txlink file' : %s\n",
					iod->name);
		break;

	default:
		mif_info("%s: ERR! wrong io_type %d\n", iod->name, iod->io_typ);
		return -EINVAL;
	}

	for (i = 0; i < NUM_SIPC_MULTI_FRAME_IDS; i++)
		skb_queue_head_init(&iod->sk_multi_q[i]);

	return ret;
}

void sipc5_deinit_io_device(struct io_device *iod)
{
	mif_err("%s: io_typ=%d\n", iod->name, iod->io_typ);

	wake_lock_destroy(&iod->wakelock);

	/* De-register misc or net device */
	switch (iod->io_typ) {
	case IODEV_BOOTDUMP:
		misc_deregister(&iod->miscdev);
		break;

	case IODEV_IPC:
		if (iod->ch == SIPC_CH_ID_CPLOG1) {
			unregister_netdev(iod->ndev);
			free_netdev(iod->ndev);
		}

		misc_deregister(&iod->miscdev);
		break;

	case IODEV_NET:
		unregister_netdev(iod->ndev);
		free_netdev(iod->ndev);
		break;

	case IODEV_DUMMY:
		device_remove_file(iod->miscdev.this_device, &attr_waketime);
		device_remove_file(iod->miscdev.this_device, &attr_loopback);
		device_remove_file(iod->miscdev.this_device, &attr_txlink);

		misc_deregister(&iod->miscdev);
		break;
	}
}

