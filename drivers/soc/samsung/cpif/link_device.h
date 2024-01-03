#ifndef __LINK_DEVICE_H__
#define __LINK_DEVICE_H__

#include <linux/types.h>

#include "link_device_memory.h"

bool check_mem_link_tx_pending(struct mem_link_device *mld);

#ifdef CONFIG_LINK_DEVICE_PCIE
int request_pcie_msi_int(struct link_device *ld,
				struct platform_device *pdev);
#endif

#endif /* end of __LINK_DEVICE_H__ */

