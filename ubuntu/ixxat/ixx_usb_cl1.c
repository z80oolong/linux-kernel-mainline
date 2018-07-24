// SPDX-License-Identifier: GPL-2.0

/* CAN driver adapter for IXXAT USB-to-CAN CL1
 *
 * Copyright (C) 2018 HMS Industrial Networks <socketcan@hms-networks.de>
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

#include "ixx_usb_core.h"

#define IXXAT_USB_CLOCK  8000000

#define IXXAT_USB_BUFFER_SIZE_RX  512
#define IXXAT_USB_BUFFER_SIZE_TX  256

#define IXXAT_USB_MODES  (CAN_CTRLMODE_3_SAMPLES | \
			  CAN_CTRLMODE_BERR_REPORTING | \
			  CAN_CTRLMODE_LISTENONLY) \

#define IXXAT_USB_BTMODE_TSM_CL1  0x80

/* bittiming parameters */
#define IXXAT_USB2CAN_NAME  "ixxat_usb"

#define IXXAT_USB2CAN_TSEG1_MIN   1
#define IXXAT_USB2CAN_TSEG1_MAX  16
#define IXXAT_USB2CAN_TSEG2_MIN   1
#define IXXAT_USB2CAN_TSEG2_MAX   8
#define IXXAT_USB2CAN_SJW_MAX     4
#define IXXAT_USB2CAN_BRP_MIN     1
#define IXXAT_USB2CAN_BRP_MAX    64
#define IXXAT_USB2CAN_BRP_INC     1

/* USB endpoint mapping for CL1 */
#define IXXAT_USB2CAN_EP1_IN   (1 | USB_DIR_IN)
#define IXXAT_USB2CAN_EP2_IN   (2 | USB_DIR_IN)
#define IXXAT_USB2CAN_EP3_IN   (3 | USB_DIR_IN)
#define IXXAT_USB2CAN_EP4_IN   (4 | USB_DIR_IN)
#define IXXAT_USB2CAN_EP5_IN   (5 | USB_DIR_IN)

#define IXXAT_USB2CAN_EP1_OUT  (1 | USB_DIR_OUT)
#define IXXAT_USB2CAN_EP2_OUT  (2 | USB_DIR_OUT)
#define IXXAT_USB2CAN_EP3_OUT  (3 | USB_DIR_OUT)
#define IXXAT_USB2CAN_EP4_OUT  (4 | USB_DIR_OUT)
#define IXXAT_USB2CAN_EP5_OUT  (5 | USB_DIR_OUT)

#define IXXAT_USB_CAN_CMD_INIT  0x325

static const struct can_bittiming_const usb2can_bt = {
	.name = IXXAT_USB2CAN_NAME,
	.tseg1_min = IXXAT_USB2CAN_TSEG1_MIN,
	.tseg1_max = IXXAT_USB2CAN_TSEG1_MAX,
	.tseg2_min = IXXAT_USB2CAN_TSEG2_MIN,
	.tseg2_max = IXXAT_USB2CAN_TSEG2_MAX,
	.sjw_max = IXXAT_USB2CAN_SJW_MAX,
	.brp_min = IXXAT_USB2CAN_BRP_MIN,
	.brp_max = IXXAT_USB2CAN_BRP_MAX,
	.brp_inc = IXXAT_USB2CAN_BRP_INC,
};

static int ixxat_usb_init_ctrl(struct ixxat_usb_device *dev)
{
	int ret;
	u8 data[IXXAT_USB_CMD_BUFFER_SIZE] = { 0 };
	struct ixxat_usb_init_cmd *cmd = (struct ixxat_usb_init_cmd *)data;
	struct ixxat_usb_init_cl1_req *req = &cmd->req_cl1;
	struct ixxat_usb_init_res *res = &cmd->res;
	u8 opmode = IXXAT_USB_OPMODE_EXTENDED | IXXAT_USB_OPMODE_STANDARD;
	const struct can_bittiming *bt = &dev->can.bittiming;

	/* Bittiming calculation from kernel
	 * (see can-utils/can-calc-bit-timing.c)
	 */
	u8 btr0 = ((bt->brp - 1) & 0x3f) | (((bt->sjw - 1) & 0x3) << 6);
	u8 btr1 = ((bt->prop_seg + bt->phase_seg1 - 1) & 0xf) |
		  (((bt->phase_seg2 - 1) & 0x7) << 4);

	if (dev->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		btr1 |= IXXAT_USB_BTMODE_TSM_CL1;
	if (dev->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
		opmode |= IXXAT_USB_OPMODE_ERRFRAME;
	if (dev->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		opmode |= IXXAT_USB_OPMODE_LISTONLY;

	ixxat_usb_setup_cmd(&req->dal_req, &res->dal_res);
	req->dal_req.req_size = cpu_to_le32(sizeof(*req));
	req->dal_req.req_code = cpu_to_le32(IXXAT_USB_CAN_CMD_INIT);
	req->dal_req.req_port = cpu_to_le16(dev->ctrl_index);
	req->mode = opmode;
	req->btr0 = btr0;
	req->btr1 = btr1;

	ret = ixxat_usb_send_cmd(dev->udev, &req->dal_req);
	if (ret < 0)
		return ret;

	ret = ixxat_usb_rcv_cmd(dev->udev, &res->dal_res, dev->ctrl_index);
	if (ret < 0)
		return ret;

	return le32_to_cpu(res->dal_res.ret_code);
}

const struct ixxat_usb_adapter usb2can_cl1 = {
	.clock = IXXAT_USB_CLOCK,
	.bt = &usb2can_bt,
	.btd = NULL,
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
	.ep_offs = 0,
	.init_ctrl = ixxat_usb_init_ctrl
};
