// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2024 Houmo AI Inc.
 * Author: hongxing.ma<hongxing.ma@houmo.ai>
 */

#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/bitmap.h>
#include <linux/slab.h>
#include <xh2a_pcie_api.h>
#include "xh2a_memory_transfer_sysdma.h"

/*
 * transfer inner xh2a within sysdma.
 * sysdma's bandwidth is lower than pcie & ipu.
 * only used for small data transfer.
 */

/* Definition for sysdma's msi */
#define XH2A_CPUSS_MSI_MASKN		   0x48000080
#define XH2A_CPUSS_MSI_VLD		   0x48000084
#define XH2A_CPUSS_MSI_DMAC_MASKN_DMAC_BIT 0

/* Definition for sysdma */
#define XH2A_CPUSS_CRG_BASE    0x48C00034
#define XH2A_CPUSS_CLK_EN      0x48C00038
#define XH2A_DWC_DMAC_BASE     0x48C10000
#define DWC_DMAC_IDREG	       0x8
#define DWC_DMAC_DMACFG	       0x10
#define DWC_DMAC_CHX_EN	       0x18
#define DWC_DMAC_CHX_ABORT     0x28
#define DWC_DMAC_REG_INT_CLR   0x38
#define DWC_DMAC_REG_INT_STS   0x50
#define DWC_DMAC_CHX_SAR       0x100
#define DWC_DMAC_CHX_DAR       0x108
#define DWC_DMAC_CHX_BLOCK_TX  0x110
#define DWC_DMAC_CHX_CTL       0x118
#define DWC_DMAC_CHX_CFG       0x120
#define DWC_DMAC_CHX_STATUS    0x130
#define DWC_DMAC_CHX_INTEN     0x180
#define DWC_DMAC_CHX_INTSTATUS 0x188
#define DWC_DMAC_CHX_INTSIGEN  0x190
#define DWC_DMAC_CHX_INTCLEAR  0x198
#define DWC_DMAC_CHX_SIZE      0x100

#define DWC_DMAC_CTL_AWLEN_BIT	   48
#define DWC_DMAC_CTL_AWLEN_EN_BIT  47
#define DWC_DMAC_CTL_ARLEN_BIT	   39
#define DWC_DMAC_CTL_ARLEN_EN_BIT  38
#define DWC_DMAC_CTL_DST_MSIZE_BIT 18
#define DWC_DMAC_CTL_SRC_MSIZE_BIT 14
#define DWC_DMAC_CTL_DST_TR_WIDTH  11
#define DWC_DMAC_CTL_SRC_TR_WIDTH  8
#define DWC_DMAC_CTL_SINC	   4

#define DWC_DMAC_CFG_DST_OSR_LMT_BIT 59
#define DWC_DMAC_CFG_SRC_OSR_LMT_BIT 55

#define DWC_DMAC_CHEN_EN_BIT 0
#define DWC_DMAC_CHEN_WE_BIT 16

#define DWC_DMAC_CFG_EN_BIT    0
#define DWC_DMAC_CFG_INTEN_BIT 1

#define DWC_DMAC_INTSTATUS_DMA_DONE_BIT 1
#define DWC_DMAC_INTSTATUS_BLK_DONE_BIT 0

#define DWC_DMAC_MAX_BLOCK_SIZE	      0x80000
#define DWC_DMAC_MAX_DATA_WIDTH_SHIFT 3
#define DWC_DMAC_MAX_BLOCK_BYTES \
	(DWC_DMAC_MAX_BLOCK_SIZE << DWC_DMAC_MAX_DATA_WIDTH_SHIFT)

#define XH2A_DWC_DMAC_POLL_TIMEOUT 4000UL

/* for memcpy */
#define XH2A_MEMORY_TRANSFER_PIO_THRESHOLD BIT(DWC_DMAC_MAX_DATA_WIDTH_SHIFT)

/* for memset */
#define XH2A_MEMORY_TRANSFER_SYSDMA_THRESHOLD 256

static inline bool xh2a_memory_transfer_sysdma_is_usable(
	struct xh2a_memory_transfer_sysdma_handle *handle)
{
	int ret;
	uint32_t val;

	ret = xh2a_pcie_pio_readl(handle->private_data,
				  (XH2A_DWC_DMAC_BASE + DWC_DMAC_IDREG), &val);

	if (ret != 0 || val == 0xffffffff) {
		dev_err(handle->dev,
			"%s: pcie_pio_readl failed. ret %d, val 0x%x\n",
			__func__, ret, val);
		return false;
	}

	ret = xh2a_pcie_pio_readl(handle->private_data,
				  (XH2A_DWC_DMAC_BASE + DWC_DMAC_DMACFG), &val);

	if (ret != 0 || val == 0xffffffff) {
		dev_err(handle->dev,
			"%s: pcie_pio_readl failed. ret %d, val 0x%x\n",
			__func__, ret, val);
		return false;
	}

	/* enable dmac's interrupt */
	val |= (BIT(DWC_DMAC_CFG_EN_BIT) | BIT(DWC_DMAC_CFG_INTEN_BIT));

	ret = xh2a_pcie_pio_writel(handle->private_data,
				   (XH2A_DWC_DMAC_BASE + DWC_DMAC_DMACFG), val);

	if (ret != 0) {
		dev_err(handle->dev, "%s: pcie_pio_writel failed. ret %d\n",
			__func__, ret);
		return false;
	}

	return true;
}

static inline bool xh2a_memory_transfer_sysdma_enable_msi(
	struct xh2a_memory_transfer_sysdma_handle *handle)
{
	int ret;

	/* enable msi for sysdma: set bits 0 and 8 */
	ret = xh2a_pcie_pio_rmw_regl(handle->private_data, XH2A_CPUSS_MSI_MASKN,
				     XH2A_PCIE_MSI_ID_CPUSS_MASK,
				     XH2A_PCIE_MSI_ID_CPUSS_MASK,
				     XH2A_PCIE_RMW_OR);
	if (ret != 0) {
		dev_err(handle->dev, "%s: rmw_regl failed %d\n", __func__, ret);
		return false;
	}

	return true;
}

static inline bool xh2a_memory_transfer_sysdma_disable_msi(
	struct xh2a_memory_transfer_sysdma_handle *handle)
{
	int ret;

	/* disable msi for sysdma: clear bits 0 and 8 */
	ret = xh2a_pcie_pio_rmw_regl(handle->private_data, XH2A_CPUSS_MSI_MASKN,
				     XH2A_PCIE_MSI_ID_CPUSS_MASK,
				     XH2A_PCIE_MSI_ID_CPUSS_MASK,
				     XH2A_PCIE_RMW_AND);
	if (ret != 0) {
		dev_err(handle->dev, "%s: rmw_regl failed %d\n", __func__, ret);
		return false;
	}

	return true;
}

static inline int xh2a_memory_transfer_sysdma_get_chan(
	struct xh2a_memory_transfer_sysdma_handle *handle, int *chan)
{
	int ret;

	ret = mutex_lock_interruptible(&handle->sysdma_mutex);

	if (ret != 0) {
		dev_err(handle->dev, "%s: mutex_lock_interruptible failed\n",
			__func__);
		return -ERESTARTSYS;
	}

	while (find_first_zero_bit(handle->chan_bitmap,
				   XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM) >=
	       XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM) {
		mutex_unlock(&handle->sysdma_mutex);

		ret = wait_event_interruptible(
			handle->chan_avail_wq,
			find_first_zero_bit(
				handle->chan_bitmap,
				XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM) <
				XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM);

		if (ret < 0) {
			dev_err(handle->dev, "%s: wait event interrupted %d\n",
				__func__, ret);
			return ret;
		}

		ret = mutex_lock_interruptible(&handle->sysdma_mutex);

		if (ret != 0) {
			dev_err(handle->dev,
				"%s: mutex_lock_interruptible failed\n",
				__func__);
			return -ERESTARTSYS;
		}
	}

	ret = find_first_zero_bit(handle->chan_bitmap,
				  XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM);

	__set_bit(ret, handle->chan_bitmap);
	*chan = ret;
	mutex_unlock(&handle->sysdma_mutex);

	return 0;
}

static inline int xh2a_memory_transfer_sysdma_put_chan(
	struct xh2a_memory_transfer_sysdma_handle *handle, int chan)
{
	int ret;

	ret = mutex_lock_interruptible(&handle->sysdma_mutex);

	if (ret != 0) {
		dev_err(handle->dev, "%s: mutex_lock_interruptible failed\n",
			__func__);
		return -ERESTARTSYS;
	}

	__clear_bit(chan, handle->chan_bitmap);

	wake_up_interruptible(&handle->chan_avail_wq);

	mutex_unlock(&handle->sysdma_mutex);

	return 0;
}

static inline void xh2a_memory_transfer_sysdma_block_irq_clear(
	struct xh2a_memory_transfer_sysdma_handle *handle, int chan)
{
	uint64_t chan_off = chan * DWC_DMAC_CHX_SIZE;
	struct xh2a_pcie_reg_entry batch[3] = {
		{ DWC_DMAC_CHX_INTCLEAR + chan_off, 0xffffffffffffffffULL },
		{ DWC_DMAC_CHX_INTEN + chan_off, 0ULL },
		{ DWC_DMAC_CHX_INTSIGEN + chan_off, 0ULL },
	};

	xh2a_pcie_pio_write_regq_batch(handle->private_data, XH2A_DWC_DMAC_BASE,
				       batch, 3);
}

static inline void xh2a_memory_transfer_sysdma_block_xfer_complete(
	struct xh2a_memory_transfer_sysdma_handle *handle, int chan)
{
	xh2a_memory_transfer_sysdma_block_irq_clear(handle, chan);
	complete(&handle->chan_completion[chan]);
}

static inline void xh2a_memory_transfer_sysdma_block_handle_err(
	struct xh2a_memory_transfer_sysdma_handle *handle, int chan)
{
	int ret;
	uint32_t val;

	ret = xh2a_pcie_pio_readl(handle->private_data,
				  XH2A_DWC_DMAC_BASE + DWC_DMAC_CHX_EN, &val);

	if (ret != 0 || val == 0xffffffff) {
		dev_err(handle->dev,
			"%s: pcie_pio_readl failed. ret %d, val 0x%x\n",
			__func__, ret, val);
		return;
	}

	/* ref to manual: disable channel. DO NOT abort channel. */
	if (val & BIT(chan)) {
		dev_err(handle->dev, "%s: channel %d force abort\n", __func__,
			chan);

		xh2a_pcie_pio_writeq(
			handle->private_data,
			XH2A_DWC_DMAC_BASE + DWC_DMAC_CHX_EN,
			(((1ULL << chan) << DWC_DMAC_CHEN_WE_BIT) |
			 ((0ULL << chan) << DWC_DMAC_CHEN_EN_BIT)));
	}

	/* clear interrupts */
	xh2a_memory_transfer_sysdma_block_irq_clear(handle, chan);
}

static void xh2a_memory_transfer_sysdma_work_handler(struct work_struct *work)
{
	int ret;
	uint32_t val;
	struct xh2a_memory_transfer_sysdma_handle *handle = container_of(
		work, struct xh2a_memory_transfer_sysdma_handle, sysdma_work);

	dev_dbg(handle->dev, "%s: %p\n", __func__, handle);

	ret = xh2a_pcie_pio_readl(handle->private_data, XH2A_CPUSS_MSI_VLD,
				  &val);

	if (ret != 0 || val == 0xffffffff) {
		dev_err(handle->dev,
			"%s: pcie_pio_readl failed. ret %d, val 0x%x\n",
			__func__, ret, val);
		return;
	}

	/* XH2A_CPUSS_MSI is shared with many devices. check register flags. */
	if (!(val & BIT(XH2A_CPUSS_MSI_DMAC_MASKN_DMAC_BIT))) {
		dev_dbg(handle->dev, "%s: not dmac's msi interrupt. ignore.\n",
			__func__);
		return;
	}

	/* temporarily disable msi for sysdma.
	 * check all channels, if the channel is completed, release it.
	 * released channel could be used by other threads, and generate level
	 * interrupt again. NO msi would be triggered until enabled again.
	 */
	xh2a_memory_transfer_sysdma_disable_msi(handle);

	if (XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM > 0) {
		struct xh2a_pcie_reg_entry
			batch[XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM];
		int j;

		for (j = 0; j < XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM; j++) {
			batch[j].offset =
				DWC_DMAC_CHX_INTSTATUS + j * DWC_DMAC_CHX_SIZE;
			batch[j].val = 0;
		}

		ret = xh2a_pcie_pio_read_regl_batch(
			handle->private_data, XH2A_DWC_DMAC_BASE, batch,
			XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM);

		if (ret != 0) {
			dev_err(handle->dev,
				"%s: read_regl_batch chan int status failed "
				"%d\n",
				__func__, ret);
			goto reenable;
		}

		for (j = 0; j < XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM; j++) {
			val = batch[j].val;

			if (val == 0) {
				/* suppress spurious interrupt */
			} else if (val &
				   (BIT(DWC_DMAC_INTSTATUS_DMA_DONE_BIT) |
				    BIT(DWC_DMAC_INTSTATUS_BLK_DONE_BIT))) {
				dev_dbg(handle->dev,
					"%s: sysdma chan %d xfer complete "
					"0x%x.\n",
					__func__, j, val);
				if (test_bit(j, handle->chan_bitmap))
					xh2a_memory_transfer_sysdma_block_xfer_complete(
						handle, j);
			} else {
				dev_err(handle->dev,
					"%s: sysdma chan %d xfer error 0x%x\n",
					__func__, j, val);
				xh2a_memory_transfer_sysdma_block_handle_err(
					handle, j);
			}
		}
	}

	/* handle for common reg interrupts */
	ret = xh2a_pcie_pio_readl(handle->private_data,
				  XH2A_DWC_DMAC_BASE + DWC_DMAC_REG_INT_STS,
				  &val);
	if (ret != 0) {
		dev_err(handle->dev,
			"%s: sysdma read common reg int status failed\n",
			__func__);
		goto reenable;
	}

	if (val != 0) {
		dev_dbg(handle->dev, "%s: sysdma common reg int status 0x%x\n",
			__func__, val);

		xh2a_pcie_pio_writel(handle->private_data,
				     XH2A_DWC_DMAC_BASE + DWC_DMAC_REG_INT_CLR,
				     val);
	}

reenable:
	/* re-enable msi for sysdma. level int pended would trigger msi */
	xh2a_memory_transfer_sysdma_enable_msi(handle);
}

static int xh2a_memory_trasfer_sysdma_one_block(
	struct xh2a_memory_transfer_sysdma_handle *handle, int chan,
	uint64_t src_addr, uint64_t dst_addr, uint64_t size, int sinc)
{
	int ret;
	uint32_t val;
	uint64_t data_width, block_size;
	uint64_t ctl_val;
	uint64_t chan_off = chan * DWC_DMAC_CHX_SIZE;
	struct xh2a_pcie_reg_entry batch[8];
	int b = 0;

	if (atomic_read(&handle->chan_removed)) {
		return -ENODEV;
	}

	ret = xh2a_pcie_pio_readl(handle->private_data,
				  (XH2A_DWC_DMAC_BASE + DWC_DMAC_CHX_EN), &val);

	val &= 0xffff;

	if (ret != 0 || val == 0xffff) {
		dev_err(handle->dev, "%s: no available channel\n", __func__);
		return -EBUSY;
	}

	if (val & BIT(chan)) {
		dev_err(handle->dev,
			"%s: channel %d is in use. cannot xfer [0x%llx -> "
			"0x%llx]\n",
			__func__, chan, src_addr, dst_addr);
		return -EBUSY;
	}

	if (size & (BIT(DWC_DMAC_MAX_DATA_WIDTH_SHIFT) - 1))
		data_width = 0;
	else
		data_width = DWC_DMAC_MAX_DATA_WIDTH_SHIFT;

	block_size = (size >> data_width) - 1;

	ctl_val = ((0x20ULL << DWC_DMAC_CTL_AWLEN_BIT) |
		   (1ULL << DWC_DMAC_CTL_AWLEN_EN_BIT) |
		   (0x20ULL << DWC_DMAC_CTL_ARLEN_BIT) |
		   (1ULL << DWC_DMAC_CTL_ARLEN_EN_BIT) |
		   (1ULL << DWC_DMAC_CTL_DST_MSIZE_BIT) |
		   (1ULL << DWC_DMAC_CTL_SRC_MSIZE_BIT) |
		   (data_width << DWC_DMAC_CTL_DST_TR_WIDTH) |
		   (data_width << DWC_DMAC_CTL_SRC_TR_WIDTH) |
		   (sinc ? BIT(DWC_DMAC_CTL_SINC) : 0ULL));

	batch[b].offset = DWC_DMAC_CHX_SAR + chan_off;
	batch[b++].val = src_addr;
	batch[b].offset = DWC_DMAC_CHX_DAR + chan_off;
	batch[b++].val = dst_addr;
	batch[b].offset = DWC_DMAC_CHX_BLOCK_TX + chan_off;
	batch[b++].val = block_size;
	batch[b].offset = DWC_DMAC_CHX_CTL + chan_off;
	batch[b++].val = ctl_val;
	batch[b].offset = DWC_DMAC_CHX_CFG + chan_off;
	batch[b++].val = ((0xFULL << DWC_DMAC_CFG_DST_OSR_LMT_BIT) |
			  (0xFULL << DWC_DMAC_CFG_SRC_OSR_LMT_BIT));
	batch[b].offset = DWC_DMAC_CHX_INTEN + chan_off;
	batch[b++].val = 0xffffffffffffffffULL;
	batch[b].offset = DWC_DMAC_CHX_INTSIGEN + chan_off;
	batch[b++].val = 0xffffffffffffffffULL;
	batch[b].offset = DWC_DMAC_CHX_INTCLEAR + chan_off;
	batch[b++].val = 0xffffffffffffffffULL;

	ret = xh2a_pcie_pio_write_regq_batch(handle->private_data,
					     XH2A_DWC_DMAC_BASE, batch, b);
	if (ret != 0) {
		dev_err(handle->dev, "%s: write_regq_batch failed %d\n",
			__func__, ret);
		return ret;
	}

	reinit_completion(&handle->chan_completion[chan]);

	ret = xh2a_pcie_pio_writeq(handle->private_data,
				   XH2A_DWC_DMAC_BASE + DWC_DMAC_CHX_EN,
				   (((1ULL << chan) << DWC_DMAC_CHEN_WE_BIT) |
				    ((1ULL << chan) << DWC_DMAC_CHEN_EN_BIT)));
	if (ret != 0) {
		dev_err(handle->dev, "%s: writeq CHX_EN failed %d\n", __func__,
			ret);
		return ret;
	}

	ret = wait_for_completion_interruptible_timeout(
		&handle->chan_completion[chan],
		msecs_to_jiffies(XH2A_DWC_DMAC_POLL_TIMEOUT));

	if (atomic_read(&handle->chan_removed)) {
		return -ENODEV;
	}

	if (ret <= 0) {
		/* timed out or interrupted, return */
		dev_err(handle->dev,
			"%s: timeout or interrupted [%d][0x%llx->0x%llx] %d\n",
			__func__, chan, src_addr, dst_addr, ret);
		xh2a_memory_transfer_sysdma_block_handle_err(handle, chan);
		return -ERESTARTSYS;
	}

	return 0;
}

int xh2a_memory_transfer_sysdma_init(
	struct xh2a_memory_transfer_sysdma_handle *handle, struct device *dev,
	void *private_data)
{
	int i;

	handle->private_data = private_data;
	handle->dev = dev;

	xh2a_memory_transfer_sysdma_disable_msi(handle);

	for (i = 0; i < XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM; i++)
		xh2a_memory_transfer_sysdma_block_irq_clear(handle, i);

	bitmap_zero(handle->chan_bitmap,
		    XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM);

	init_waitqueue_head(&handle->chan_avail_wq);

	for (i = 0; i < XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM; i++)
		init_completion(&handle->chan_completion[i]);

	INIT_WORK(&handle->sysdma_work,
		  xh2a_memory_transfer_sysdma_work_handler);

	mutex_init(&handle->sysdma_mutex);

	atomic_set(&handle->chan_removed, 0);

	xh2a_memory_transfer_sysdma_enable_msi(handle);

	return 0;
}

int xh2a_memory_transfer_sysdma_deinit(
	struct xh2a_memory_transfer_sysdma_handle *handle)
{
	int i;

	xh2a_memory_transfer_sysdma_disable_msi(handle);

	wake_up_interruptible(&handle->chan_avail_wq);

	cancel_work_sync(&handle->sysdma_work);

	atomic_set(&handle->chan_removed, 1);
	for (i = 0; i < XH2A_MEMORY_TRANSFER_SYSDMA_CHANNEL_NUM; i++)
		complete_all(&handle->chan_completion[i]);

	return 0;
}

int xh2a_memory_transfer_sysdma_memcpy(
	struct xh2a_memory_transfer_sysdma_handle *handle, uint64_t src_addr,
	uint64_t dst_addr, uint64_t size)
{
	int ret, chan;
	uint64_t done_size = 0;
	uint64_t xfer_size = 0;
	uint64_t xfer_size_align = 0;
	uint64_t xfer_size_remain = 0;
	uint64_t quirk_size = 0;
	uint8_t pio_buf[XH2A_MEMORY_TRANSFER_PIO_THRESHOLD] = { 0 };

	if (!handle) {
		pr_err("handle is null\n");
		return -EINVAL;
	}

	if (size == 0)
		return 0;

	if (size < XH2A_MEMORY_TRANSFER_PIO_THRESHOLD) {
		ret = xh2a_pcie_pio_read_mem(handle->private_data, src_addr,
					     pio_buf, (uint32_t)size);
		if (ret != 0) {
			dev_err(handle->dev,
				"pio read memcpy small [0x%llx][0x%llx] failed "
				"%d\n",
				src_addr, size, ret);
			return ret;
		}

		ret = xh2a_pcie_pio_write_mem(handle->private_data, dst_addr,
					      pio_buf, (uint32_t)size);
		if (ret != 0) {
			dev_err(handle->dev,
				"pio write memcpy small [0x%llx][0x%llx] "
				"failed %d\n",
				dst_addr, size, ret);
			return ret;
		}

		return 0;
	}

	if (!xh2a_memory_transfer_sysdma_is_usable(handle)) {
		dev_err(handle->dev, "%s: sysdma is not usable\n", __func__);
		return -EINVAL;
	}

	/* sysdma hardware cannot hanle un-aligned start address */
	quirk_size = src_addr & ((BIT(DWC_DMAC_MAX_DATA_WIDTH_SHIFT) - 1));
	quirk_size = BIT(DWC_DMAC_MAX_DATA_WIDTH_SHIFT) - quirk_size;
	quirk_size = (quirk_size > size) ? size : quirk_size;

	if (quirk_size != BIT(DWC_DMAC_MAX_DATA_WIDTH_SHIFT)) {
		ret = xh2a_pcie_pio_read_mem(handle->private_data, src_addr,
					     pio_buf, quirk_size);
		if (ret != 0) {
			dev_err(handle->dev,
				"pio read memcpy quirk [0x%llx][0x%llx] failed "
				"%d\n",
				src_addr, quirk_size, ret);
			return ret;
		}

		ret = xh2a_pcie_pio_write_mem(handle->private_data, dst_addr,
					      pio_buf, quirk_size);
		if (ret != 0) {
			dev_err(handle->dev,
				"pio write memcpy quirk [0x%llx][0x%llx] "
				"failed %d\n",
				dst_addr, quirk_size, ret);
			return ret;
		}

		done_size = quirk_size;
	}

	if (done_size == size)
		return 0;

	ret = xh2a_memory_transfer_sysdma_get_chan(handle, &chan);
	if (ret != 0) {
		dev_err(handle->dev, "%s: sysdma get chan failed\n", __func__);
		return ret;
	}

	while (done_size != size) {
		if (size - done_size < DWC_DMAC_MAX_BLOCK_BYTES)
			xfer_size = size - done_size;
		else
			xfer_size = DWC_DMAC_MAX_BLOCK_BYTES;

		xfer_size_align = xfer_size &
				  (~(BIT(DWC_DMAC_MAX_DATA_WIDTH_SHIFT) - 1));
		xfer_size_remain = xfer_size - xfer_size_align;
		if (xfer_size_align) {
			ret = xh2a_memory_trasfer_sysdma_one_block(
				handle, chan, src_addr + done_size,
				dst_addr + done_size, xfer_size_align, 0);

			if (ret == -ENODEV) {
				pr_err("sysdma memcpy [%d][0x%llx -> "
				       "0x%llx][0x%llx] failed %d\n",
				       chan, src_addr + done_size,
				       dst_addr + done_size, xfer_size_align,
				       ret);
				xh2a_memory_transfer_sysdma_put_chan(handle,
								     chan);
				return ret;
			}

			if (ret != 0) {
				dev_err(handle->dev,
					"sysdma memcpy [%d][0x%llx -> "
					"0x%llx][0x%llx] failed %d\n",
					chan, src_addr + done_size,
					dst_addr + done_size, xfer_size_align,
					ret);
				xh2a_memory_transfer_sysdma_put_chan(handle,
								     chan);
				return ret;
			}
		}

		if (xfer_size_remain) {
			ret = xh2a_memory_trasfer_sysdma_one_block(
				handle, chan,
				src_addr + done_size + xfer_size_align,
				dst_addr + done_size + xfer_size_align,
				xfer_size_remain, 0);

			if (ret == -ENODEV) {
				pr_err("sysdma memcpy [%d][0x%llx -> "
				       "0x%llx][0x%llx] failed %d\n",
				       chan,
				       src_addr + done_size + xfer_size_align,
				       dst_addr + done_size + xfer_size_align,
				       xfer_size_remain, ret);
				xh2a_memory_transfer_sysdma_put_chan(handle,
								     chan);
				return ret;
			}

			if (ret != 0) {
				dev_err(handle->dev,
					"sysdma memcpy [%d][0x%llx -> "
					"0x%llx][0x%llx] failed %d\n",
					chan,
					src_addr + done_size + xfer_size_align,
					dst_addr + done_size + xfer_size_align,
					xfer_size_remain, ret);
				xh2a_memory_transfer_sysdma_put_chan(handle,
								     chan);
				return ret;
			}
		}

		done_size += xfer_size;
	}

	xh2a_memory_transfer_sysdma_put_chan(handle, chan);

	return 0;
}

int xh2a_memory_transfer_sysdma_memset(
	struct xh2a_memory_transfer_sysdma_handle *handle, uint64_t paddr,
	uint64_t size, int c)
{
	int ret;
	uint64_t filled;
	uint64_t copy_size;
	uint64_t seed_size;
	uint64_t aligned_off;
	uint8_t c1;
	uint8_t pio_buf[XH2A_MEMORY_TRANSFER_SYSDMA_THRESHOLD] = { 0 };

	if (!handle) {
		pr_err("handle is null\n");
		return -EINVAL;
	}

	if (size == 0)
		return 0;

	c1 = (uint8_t)c;

	if (size < XH2A_MEMORY_TRANSFER_SYSDMA_THRESHOLD) {
		memset(pio_buf, c1, (size_t)size);
		ret = xh2a_pcie_pio_write_mem(handle->private_data, paddr,
					      pio_buf, (uint32_t)size);
		return ret;
	}

	/*
	 * seed + doubling copy:
	 * 1) write a seed by write_mem (iatu_copy, batched)
	 * 2) repeatedly copy [base, base + filled) to [base + filled, ...)
	 *
	 * Optimizations:
	 *   - write_mem (iatu_copy) instead of writeb loop for seed and tail
	 *   - absorb tiny tail (<= SYSDMA_THRESHOLD) via write_mem to skip
	 *     one DMA round
	 *   - align the seed start so that the subsequent sysdma memcpy
	 *     always has its source address 8-byte-aligned for maximum
	 *     DMA efficiency
	 */
	aligned_off = roundup(paddr, BIT(DWC_DMAC_MAX_DATA_WIDTH_SHIFT)) -
			       paddr;

	if (aligned_off > 0 && aligned_off < size) {
		memset(pio_buf, c1, (size_t)aligned_off);
		ret = xh2a_pcie_pio_write_mem(handle->private_data, paddr,
					      pio_buf, (uint32_t)aligned_off);
		if (ret != 0) {
			dev_err(handle->dev,
				"sysdma memset misalign head [0x%llx][0x%llx] "
				"failed %d\n",
				paddr, aligned_off, ret);
			return ret;
		}
	}

	seed_size = XH2A_MEMORY_TRANSFER_SYSDMA_THRESHOLD;
	memset(pio_buf, c1, (size_t)seed_size);
	ret = xh2a_pcie_pio_write_mem(handle->private_data,
				      paddr + aligned_off, pio_buf,
				      (uint32_t)seed_size);
	if (ret != 0) {
		dev_err(handle->dev,
			"sysdma memset seed write [0x%llx][0x%llx] failed %d\n",
			paddr + aligned_off, seed_size, ret);
		return ret;
	}

	filled = aligned_off + seed_size;
	while (filled < size) {
		copy_size = min((uint64_t)(size - filled),
				(uint64_t)(filled - aligned_off));

		if (size - filled <= XH2A_MEMORY_TRANSFER_SYSDMA_THRESHOLD) {
			memset(pio_buf, c1, (size_t)(size - filled));
			ret = xh2a_pcie_pio_write_mem(
				handle->private_data, paddr + filled, pio_buf,
				(uint32_t)(size - filled));
			return ret;
		}

		ret = xh2a_memory_transfer_sysdma_memcpy(
			handle, paddr + aligned_off, paddr + filled,
			copy_size);
		if (ret == -ENODEV) {
			pr_err("sysdma memset copy [0x%llx -> 0x%llx][0x%llx] "
			       "failed %d\n",
			       paddr + aligned_off, paddr + filled, copy_size,
			       ret);
			return ret;
		}

		if (ret != 0) {
			dev_err(handle->dev,
				"sysdma memset copy [0x%llx -> 0x%llx][0x%llx] "
				"failed %d\n",
				paddr + aligned_off, paddr + filled, copy_size,
				ret);
			return ret;
		}
		filled += copy_size;
	}

	return 0;
}

int xh2a_memory_transfer_sysdma_pm_prepare(
	struct xh2a_memory_transfer_sysdma_handle *handle)
{
	xh2a_memory_transfer_sysdma_disable_msi(handle);

	wake_up_interruptible(&handle->chan_avail_wq);

	cancel_work_sync(&handle->sysdma_work);

	return 0;
}

int xh2a_memory_transfer_sysdma_pm_complete(
	struct xh2a_memory_transfer_sysdma_handle *handle)
{
	xh2a_memory_transfer_sysdma_enable_msi(handle);

	return 0;
}
