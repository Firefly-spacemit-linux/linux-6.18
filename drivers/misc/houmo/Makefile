# SPDX-License-Identifier: GPL-2.0
#
# Kbuild file for the Houmo XH2A PCIe accelerator driver.

obj-$(CONFIG_HOUMO_XH2A) += xh2a_drv.o

# Keep the version visible in the existing sysfs and module information.
XH2A_HOST_DRIVER_RELEASE ?= 0.4.0
XH2A_HOST_DRIVER_BUILDTIME ?= K3-Linux-6.18

ccflags-y += -DXH2A_HOST_DRIVER_RELEASE='"$(XH2A_HOST_DRIVER_RELEASE)"'
ccflags-y += -DXH2A_HOST_DRIVER_BUILDTIME='"$(XH2A_HOST_DRIVER_BUILDTIME)"'
ccflags-y += -I$(src)/include
ccflags-y += -I$(src)/public
ccflags-y += -I$(src)/ipu/include
ccflags-y += -I$(src)/ipu/policy
ccflags-y += -I$(src)/rpmsg_lite
ccflags-y += -I$(src)/rpmsg_lite/rpmsg-lite/lib/include
ccflags-y += -I$(src)/rpmsg_lite/rpmsg-lite/lib/include/environment/xh2a_host
ccflags-y += -I$(src)/rpmsg_lite/rpmsg-lite/lib/include/platform/xh2a_host
ccflags-y += -DIPU_DEBUG_TRACE -DIPU_PRIORITY_POLICY -DHM_ADDRESS_LAYOUT

xh2a_drv-y := xh2a_host_drv.o

ifeq ($(CONFIG_HOUMO_XH2A_NOR_MICRON),y)
xh2a_drv-y += qspi/nor_flash/device_nor_mt25.o
ccflags-y += -DDEVICE_NOR_MICRON
else ifeq ($(CONFIG_HOUMO_XH2A_NOR_GIGADEVICE),y)
xh2a_drv-y += qspi/nor_flash/device_nor_gd25.o
ccflags-y += -DDEVICE_NOR_GIGADEVICE
else
ccflags-y += -DDEVICE_NOR_UNKNOWN
endif

xh2a_drv-y += ctl/xh2a_ctl.o ctl/xh2a_ctl_lock.o
xh2a_drv-y += pcie/xh2a_pcie.o pcie/xh2a_pcie_hdma.o pcie/xh2a_pcie_api.o
xh2a_drv-y += pcie/xh2a_pcie_misc.o pcie/xh2a_pcie_efuse.o
xh2a_drv-y += sys/xh2a_sys.o sys/xh2a_ctc.o
xh2a_drv-y += fast_memory/xh2a_fast_memory.o
xh2a_drv-y += memory_allocator/xh2a_memory_allocator.o
xh2a_drv-y += memory_allocator/allocator_algorithm.o
xh2a_drv-y += memory_allocator/xh2a_memory_allocator_spm_api.o
xh2a_drv-y += memory_transfer/xh2a_memory_transfer.o
xh2a_drv-y += memory_transfer/xh2a_memory_transfer_sysdma.o
xh2a_drv-y += transport/xh2a_transport.o transport/transport_protocol.o
xh2a_drv-y += qspi/xh2a_qspi.o qspi/axi_crc/xh2a_axi_crc.o
xh2a_drv-y += i2c/xh2a_i2c.o

xh2a_drv-y += ipu/policy/xh2a_priority_policy.o
xh2a_drv-y += ipu/xh2a_ipu_hw.o ipu/xh2a_ipu_device.o
xh2a_drv-y += ipu/xh2a_ipu_file_handle.o ipu/xh2a_ipu_group.o
xh2a_drv-y += ipu/xh2a_ipu_kernel.o ipu/xh2a_ipu_interrupt.o
xh2a_drv-y += ipu/xh2a_ipu_ioctl.o ipu/xh2a_ipu_mempool.o
xh2a_drv-y += ipu/xh2a_ipu_load.o ipu/xh2a_ipu_policy.o

xh2a_drv-y += rpmsg_lite/rpmsg-lite/lib/common/llist.o
xh2a_drv-y += rpmsg_lite/rpmsg-lite/lib/virtio/virtqueue.o
xh2a_drv-y += rpmsg_lite/rpmsg-lite/lib/rpmsg_lite/rpmsg_ns.o
xh2a_drv-y += rpmsg_lite/rpmsg-lite/lib/rpmsg_lite/rpmsg_queue.o
xh2a_drv-y += rpmsg_lite/rpmsg-lite/lib/rpmsg_lite/rpmsg_lite.o
xh2a_drv-y += rpmsg_lite/rpmsg-lite/lib/rpmsg_lite/porting/environment/rpmsg_env_xh2a_host.o
xh2a_drv-y += rpmsg_lite/rpmsg-lite/lib/rpmsg_lite/porting/platform/xh2a_host/rpmsg_platform.o
xh2a_drv-y += rpmsg_lite/xh2a_rpmsg_core.o rpmsg_lite/xh2a_rpmsg_lite.o
xh2a_drv-y += rpmsg_lite/xh2a_rpmsg_lsm.o rpmsg_lite/xh2a_rpmsg_lite_ext.o
