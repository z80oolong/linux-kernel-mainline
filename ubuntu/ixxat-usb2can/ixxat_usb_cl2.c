// SPDX-License-Identifier: GPL-2.0

/* CAN driver adapter for IXXAT USB-to-CAN CL2
 *
 * Copyright (C) 2018 HMS Industrial Networks <socketcan at hms-networks.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/can/dev.h>
#include <linux/usb.h>

#include "ixxat_usb_core.h"

#define IXXAT_USB_CLOCK 80000000

#define IXXAT_USB_BUFFER_SIZE_RX 512
#define IXXAT_USB_BUFFER_SIZE_TX 512

#define IXXAT_USB_MODES (CAN_CTRLMODE_3_SAMPLES | \
			 CAN_CTRLMODE_LISTENONLY | \
			 CAN_CTRLMODE_BERR_REPORTING | \
			 CAN_CTRLMODE_FD | \
			 CAN_CTRLMODE_FD_NON_ISO)

/* bittiming parameters CL2 */
#define IXXAT_USB2CAN_TSEG1_MIN 1
#define IXXAT_USB2CAN_TSEG1_MAX 256
#define IXXAT_USB2CAN_TSEG2_MIN 1
#define IXXAT_USB2CAN_TSEG2_MAX 256
#define IXXAT_USB2CAN_SJW_MAX 128
#define IXXAT_USB2CAN_BRP_MIN 2
#define IXXAT_USB2CAN_BRP_MAX 513
#define IXXAT_USB2CAN_BRP_INC 1

#define IXXAT_USB2CAN_TSEG1_MIN_DATA 1
#define IXXAT_USB2CAN_TSEG1_MAX_DATA 256
#define IXXAT_USB2CAN_TSEG2_MIN_DATA 1
#define IXXAT_USB2CAN_TSEG2_MAX_DATA 256
#define IXXAT_USB2CAN_SJW_MAX_DATA 128
#define IXXAT_USB2CAN_BRP_MIN_DATA 2
#define IXXAT_USB2CAN_BRP_MAX_DATA 513
#define IXXAT_USB2CAN_BRP_INC_DATA 1

/* bittiming parameters CAN IDM */
#define IXXAT_CANIDM_TSEG1_MIN 1
#define IXXAT_CANIDM_TSEG1_MAX 256
#define IXXAT_CANIDM_TSEG2_MIN 1
#define IXXAT_CANIDM_TSEG2_MAX 128
#define IXXAT_CANIDM_SJW_MAX 128
#define IXXAT_CANIDM_BRP_MIN 1
#define IXXAT_CANIDM_BRP_MAX 512
#define IXXAT_CANIDM_BRP_INC 1

#define IXXAT_CANIDM_TSEG1_MIN_DATA 1
#define IXXAT_CANIDM_TSEG1_MAX_DATA 32
#define IXXAT_CANIDM_TSEG2_MIN_DATA 1
#define IXXAT_CANIDM_TSEG2_MAX_DATA 16
#define IXXAT_CANIDM_SJW_MAX_DATA 8
#define IXXAT_CANIDM_BRP_MIN_DATA 1
#define IXXAT_CANIDM_BRP_MAX_DATA 32
#define IXXAT_CANIDM_BRP_INC_DATA 1

/* USB endpoint mapping for CL2 */
#define IXXAT_USB2CAN_EP1_IN (1 | USB_DIR_IN)
#define IXXAT_USB2CAN_EP2_IN (2 | USB_DIR_IN)
#define IXXAT_USB2CAN_EP3_IN (3 | USB_DIR_IN)
#define IXXAT_USB2CAN_EP4_IN (4 | USB_DIR_IN)
#define IXXAT_USB2CAN_EP5_IN (5 | USB_DIR_IN)

#define IXXAT_USB2CAN_EP1_OUT (1 | USB_DIR_OUT)
#define IXXAT_USB2CAN_EP2_OUT (2 | USB_DIR_OUT)
#define IXXAT_USB2CAN_EP3_OUT (3 | USB_DIR_OUT)
#define IXXAT_USB2CAN_EP4_OUT (4 | USB_DIR_OUT)
#define IXXAT_USB2CAN_EP5_OUT (5 | USB_DIR_OUT)

/* USB endpoint mapping for CAN IDM */
#define IXXAT_CANIDM_EP1_IN (2 | USB_DIR_IN)
#define IXXAT_CANIDM_EP2_IN (4 | USB_DIR_IN)
#define IXXAT_CANIDM_EP3_IN (6 | USB_DIR_IN)
#define IXXAT_CANIDM_EP4_IN (8 | USB_DIR_IN)
#define IXXAT_CANIDM_EP5_IN (10 | USB_DIR_IN)

#define IXXAT_CANIDM_EP1_OUT (1 | USB_DIR_OUT)
#define IXXAT_CANIDM_EP2_OUT (3 | USB_DIR_OUT)
#define IXXAT_CANIDM_EP3_OUT (5 | USB_DIR_OUT)
#define IXXAT_CANIDM_EP4_OUT (7 | USB_DIR_OUT)
#define IXXAT_CANIDM_EP5_OUT (9 | USB_DIR_OUT)

#define IXXAT_USB_CAN_CMD_INIT 0x337

static const struct can_bittiming_const usb2can_bt = {
	.name = IXXAT_USB_CTRL_NAME,
	.tseg1_min = IXXAT_USB2CAN_TSEG1_MIN,
	.tseg1_max = IXXAT_USB2CAN_TSEG1_MAX,
	.tseg2_min = IXXAT_USB2CAN_TSEG2_MIN,
	.tseg2_max = IXXAT_USB2CAN_TSEG2_MAX,
	.sjw_max = IXXAT_USB2CAN_SJW_MAX,
	.brp_min = IXXAT_USB2CAN_BRP_MIN,
	.brp_max = IXXAT_USB2CAN_BRP_MAX,
	.brp_inc = IXXAT_USB2CAN_BRP_INC,
};

static const struct can_bittiming_const usb2can_btd = {
	.name = IXXAT_USB_CTRL_NAME,
	.tseg1_min = IXXAT_USB2CAN_TSEG1_MIN_DATA,
	.tseg1_max = IXXAT_USB2CAN_TSEG1_MAX_DATA,
	.tseg2_min = IXXAT_USB2CAN_TSEG2_MIN_DATA,
	.tseg2_max = IXXAT_USB2CAN_TSEG2_MAX_DATA,
	.sjw_max = IXXAT_USB2CAN_SJW_MAX_DATA,
	.brp_min = IXXAT_USB2CAN_BRP_MIN_DATA,
	.brp_max = IXXAT_USB2CAN_BRP_MAX_DATA,
	.brp_inc = IXXAT_USB2CAN_BRP_INC_DATA,
};

static const struct can_bittiming_const canidm_bt = {
	.name = IXXAT_USB_CTRL_NAME,
	.tseg1_min = IXXAT_CANIDM_TSEG1_MIN,
	.tseg1_max = IXXAT_CANIDM_TSEG1_MAX,
	.tseg2_min = IXXAT_CANIDM_TSEG2_MIN,
	.tseg2_max = IXXAT_CANIDM_TSEG2_MAX,
	.sjw_max = IXXAT_CANIDM_SJW_MAX,
	.brp_min = IXXAT_CANIDM_BRP_MIN,
	.brp_max = IXXAT_CANIDM_BRP_MAX,
	.brp_inc = IXXAT_CANIDM_BRP_INC
};

static const struct can_bittiming_const canidm_btd = {
	.name = IXXAT_USB_CTRL_NAME,
	.tseg1_min = IXXAT_CANIDM_TSEG1_MIN_DATA,
	.tseg1_max = IXXAT_CANIDM_TSEG1_MAX_DATA,
	.tseg2_min = IXXAT_CANIDM_TSEG2_MIN_DATA,
	.tseg2_max = IXXAT_CANIDM_TSEG2_MAX_DATA,
	.sjw_max = IXXAT_CANIDM_SJW_MAX_DATA,
	.brp_min = IXXAT_CANIDM_BRP_MIN_DATA,
	.brp_max = IXXAT_CANIDM_BRP_MAX_DATA,
	.brp_inc = IXXAT_CANIDM_BRP_INC_DATA
};

static int ixxat_usb_init_ctrl(struct ixxat_usb_device *dev)
{
	const struct can_bittiming *bt = &dev->can.bittiming;
	const struct can_bittiming *btd = &dev->can.data_bittiming;
	const u16 port = dev->ctrl_index;
	int err;
	struct ixxat_usb_init_cl2_cmd *cmd;
	const u32 rcv_size = sizeof(cmd->res);
	const u32 snd_size = sizeof(*cmd);
	u32 btmode = IXXAT_USB_BTMODE_NAT;
	u8 exmode = 0;
	u8 opmode = IXXAT_USB_OPMODE_EXTENDED | IXXAT_USB_OPMODE_STANDARD;

	cmd = kmalloc(snd_size, GFP_KERNEL);
	if (!cmd)
		return -ENOMEM;

	if (dev->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		btmode = IXXAT_USB_BTMODE_TSM;
	if (dev->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
		opmode |= IXXAT_USB_OPMODE_ERRFRAME;
	if (dev->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		opmode |= IXXAT_USB_OPMODE_LISTONLY;
	if (dev->can.ctrlmode & (CAN_CTRLMODE_FD | CAN_CTRLMODE_FD_NON_ISO)) {
		exmode |= IXXAT_USB_EXMODE_EXTDATA | IXXAT_USB_EXMODE_FASTDATA;

		if (!(dev->can.ctrlmode & CAN_CTRLMODE_FD_NON_ISO))
			exmode |= IXXAT_USB_EXMODE_ISOFD;
	}

	ixxat_usb_setup_cmd(&cmd->req, &cmd->res);
	cmd->req.size = cpu_to_le32(snd_size - rcv_size);
	cmd->req.code = cpu_to_le32(IXXAT_USB_CAN_CMD_INIT);
	cmd->req.port = cpu_to_le16(port);
	cmd->opmode = opmode;
	cmd->exmode = exmode;
	cmd->sdr.mode = cpu_to_le32(btmode);
	cmd->sdr.bps = cpu_to_le32(bt->brp);
	cmd->sdr.ts1 = cpu_to_le16(bt->prop_seg + bt->phase_seg1);
	cmd->sdr.ts2 = cpu_to_le16(bt->phase_seg2);
	cmd->sdr.sjw = cpu_to_le16(bt->sjw);
	cmd->sdr.tdo = 0;

	if (exmode) {
		cmd->fdr.mode = cpu_to_le32(btmode);
		cmd->fdr.bps = cpu_to_le32(btd->brp);
		cmd->fdr.ts1 = cpu_to_le16(btd->prop_seg + btd->phase_seg1);
		cmd->fdr.ts2 = cpu_to_le16(btd->phase_seg2);
		cmd->fdr.sjw = cpu_to_le16(btd->sjw);
		cmd->fdr.tdo = cpu_to_le16(btd->brp * (btd->phase_seg1 + 1 +
						       btd->prop_seg));
	}

	err = ixxat_usb_send_cmd(dev->udev, port, cmd, snd_size, &cmd->res,
				 rcv_size);
	kfree(cmd);
	return err;
}

const struct ixxat_usb_adapter usb2can_cl2 = {
	.clock = IXXAT_USB_CLOCK,
	.bt = &usb2can_bt,
	.btd = &usb2can_btd,
	.modes = IXXAT_USB_MODES,
	.buffer_size_rx = IXXAT_USB_BUFFER_SIZE_RX,
	.buffer_size_tx = IXXAT_USB_BUFFER_SIZE_TX,
	.ep_msg_in = {
		IXXAT_USB2CAN_EP1_IN,
		IXXAT_USB2CAN_EP2_IN,
		IXXAT_USB2CAN_EP3_IN,
		IXXAT_USB2CAN_EP4_IN,
		IXXAT_USB2CAN_EP5_IN
	},
	.ep_msg_out = {
		IXXAT_USB2CAN_EP1_OUT,
		IXXAT_USB2CAN_EP2_OUT,
		IXXAT_USB2CAN_EP3_OUT,
		IXXAT_USB2CAN_EP4_OUT,
		IXXAT_USB2CAN_EP5_OUT
	},
	.ep_offs = 1,
	.init_ctrl = ixxat_usb_init_ctrl
};

const struct ixxat_usb_adapter can_idm = {
	.clock = IXXAT_USB_CLOCK,
	.bt = &canidm_bt,
	.btd = &canidm_btd,
	.modes = IXXAT_USB_MODES,
	.buffer_size_rx = IXXAT_USB_BUFFER_SIZE_RX,
	.buffer_size_tx = IXXAT_USB_BUFFER_SIZE_TX,
	.ep_msg_in = {
		IXXAT_CANIDM_EP1_IN,
		IXXAT_CANIDM_EP2_IN,
		IXXAT_CANIDM_EP3_IN,
		IXXAT_CANIDM_EP4_IN,
		IXXAT_CANIDM_EP5_IN
	},
	.ep_msg_out = {
		IXXAT_CANIDM_EP1_OUT,
		IXXAT_CANIDM_EP2_OUT,
		IXXAT_CANIDM_EP3_OUT,
		IXXAT_CANIDM_EP4_OUT,
		IXXAT_CANIDM_EP5_OUT
	},
	.ep_offs = 0,
	.init_ctrl = ixxat_usb_init_ctrl
};
