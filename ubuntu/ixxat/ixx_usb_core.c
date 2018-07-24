// SPDX-License-Identifier: GPL-2.0

/* CAN driver for IXXAT USB-to-CAN
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/can/dev.h>
#include <linux/kthread.h>
#include <linux/usb.h>

#include "ixx_usb_core.h"

MODULE_AUTHOR("Marcel Schmidt <socketcan@hms-networks.de>");
MODULE_DESCRIPTION("CAN driver for IXXAT USB-to-CAN FD adapters");
MODULE_LICENSE("GPL v2");

/* Table of devices that work with this driver */
static const struct usb_device_id ixxat_usb_table[] = {
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_COMPACT_PRODUCT_ID) },
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_EMBEDDED_PRODUCT_ID) },
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_PROFESSIONAL_PRODUCT_ID) },
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_AUTOMOTIVE_PRODUCT_ID) },
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_FD_COMPACT_PRODUCT_ID) },
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_FD_PROFESSIONAL_PRODUCT_ID) },
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_FD_AUTOMOTIVE_PRODUCT_ID) },
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAN_FD_PCIE_MINI_PRODUCT_ID) },
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, USB2CAR_PRODUCT_ID) },
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, CAN_IDM100_PRODUCT_ID) },
	{ USB_DEVICE(IXXAT_USB_VENDOR_ID, CAN_IDM101_PRODUCT_ID) },
	{ } /* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, ixxat_usb_table);

void ixxat_usb_setup_cmd(struct ixxat_usb_dal_req *req,
			 struct ixxat_usb_dal_res *res)
{
	req->req_size = sizeof(*req);
	req->req_port = 0xffff;
	req->req_socket = 0xffff;
	req->req_code = 0;

	res->res_size = sizeof(*res);
	res->ret_size = 0;
	res->ret_code = 0xffffffff;
}

int ixxat_usb_send_cmd(struct usb_device *dev,
		       struct ixxat_usb_dal_req *dal_req)
{
	int ret = 0;
	int i;
	u16 size = le32_to_cpu(dal_req->req_size) +
		   sizeof(struct ixxat_usb_dal_res);
	u16 value = le16_to_cpu(dal_req->req_port);
	u8 request = 0xff;
	u8 requesttype = USB_TYPE_VENDOR | USB_DIR_OUT;
	u8 *buf = kmalloc(size, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	memcpy(buf, dal_req, size);

	for (i = 0; i < IXXAT_USB_MAX_COM_REQ; ++i) {
		const int to = msecs_to_jiffies(IXXAT_USB_MSG_TIMEOUT);

		ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), request,
				      requesttype, value, 0, buf, size, to);

		if (ret < 0)
			msleep(IXXAT_USB_MSG_CYCLE);
		else
			break;
	}

	kfree(buf);

	if (ret < 0)
		dev_err(&dev->dev, "Error %d: Sending command failure\n", ret);

	return ret;
}

int ixxat_usb_rcv_cmd(struct usb_device *dev,
		      struct ixxat_usb_dal_res *dal_res, int value)
{
	int ret;
	int res_size = 0;
	int i;
	int size_to_read = le32_to_cpu(dal_res->res_size);
	u8 req = 0xff;
	u8 req_type = USB_TYPE_VENDOR | USB_DIR_IN;
	u8 *buf = kmalloc(size_to_read, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	for (i = 0; i < IXXAT_USB_MAX_COM_REQ; ++i) {
		const int to = msecs_to_jiffies(IXXAT_USB_MSG_TIMEOUT);
		void *data = buf + (u8)res_size;
		const int size = size_to_read - res_size;

		ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0),
				      req, req_type, value, 0,
				      data, size, to);

		if (ret < 0) {
			msleep(IXXAT_USB_MSG_CYCLE);
			continue;
		}

		res_size += ret;
		if (res_size < size_to_read)
			msleep(IXXAT_USB_MSG_CYCLE);
		else
			break;
	}

	if (res_size != size_to_read)
		ret = -EBADMSG;

	if (ret < 0) {
		dev_err(&dev->dev, "Error %d: Receiving command failure\n",
			ret);
		kfree(buf);
		return ret;
	}

	memcpy(dal_res, buf, size_to_read);
	kfree(buf);

	return ret;
}

static void ixxat_usb_update_ts_now(struct ixxat_usb_device *dev, u32 ts_now)
{
	u32 *ts_dev = &dev->time_ref.ts_dev_0;
	ktime_t *kt_host = &dev->time_ref.kt_host_0;
	u64 timebase = (u64)0x00000000FFFFFFFF - (u64)(*ts_dev) + (u64)ts_now;

	*kt_host = ktime_add_us(*kt_host, timebase);
	*ts_dev = ts_now;
}

static void ixxat_usb_get_ts_tv(struct ixxat_usb_device *dev, u32 ts,
				ktime_t *k_time)
{
	ktime_t tmp_time = dev->time_ref.kt_host_0;

	if (ts < dev->time_ref.ts_dev_last)
		ixxat_usb_update_ts_now(dev, ts);

	dev->time_ref.ts_dev_last = ts;
	tmp_time = ktime_add_us(tmp_time, ts - dev->time_ref.ts_dev_0);

	if (k_time)
		*k_time = tmp_time;
}

static void ixxat_usb_set_ts_now(struct ixxat_usb_device *dev, u32 ts_now)
{
	dev->time_ref.ts_dev_0 = ts_now;
	dev->time_ref.kt_host_0 = ktime_get_real();
	dev->time_ref.ts_dev_last = ts_now;
}

static int ixxat_usb_get_dev_caps(struct usb_device *dev,
				  struct ixxat_dev_caps *dev_caps)
{
	int i;
	int ret;
	u8 data[IXXAT_USB_CMD_BUFFER_SIZE] = { 0 };
	struct ixxat_usb_caps_cmd *cmd = (struct ixxat_usb_caps_cmd *)data;
	struct ixxat_usb_caps_req *req = &cmd->req;
	struct ixxat_usb_caps_res *res = &cmd->res;

	ixxat_usb_setup_cmd(&req->dal_req, &res->dal_res);
	req->dal_req.req_code = cpu_to_le32(IXXAT_USB_BRD_CMD_GET_DEVCAPS);
	res->dal_res.res_size = cpu_to_le32(sizeof(*res));

	ret = ixxat_usb_send_cmd(dev, &req->dal_req);
	if (ret < 0)
		return ret;

	ret = ixxat_usb_rcv_cmd(dev, &res->dal_res, 0xffff);
	if (ret < 0)
		return ret;

	dev_caps->bus_ctrl_count = le16_to_cpu(res->dev_caps.bus_ctrl_count);
	for (i = 0; i < dev_caps->bus_ctrl_count; ++i) {
		u16 type = le16_to_cpu(res->dev_caps.bus_ctrl_types[i]);

		dev_caps->bus_ctrl_types[i] = type;
	}

	return 0;
}

static int ixxat_usb_get_dev_info(struct ixxat_usb_device *dev,
				  struct ixxat_dev_info *dev_info)
{
	int ret;
	u8 data[IXXAT_USB_CMD_BUFFER_SIZE] = { 0 };
	struct ixxat_usb_info_cmd *cmd = (struct ixxat_usb_info_cmd *)data;
	struct ixxat_usb_info_req *req = &cmd->req;
	struct ixxat_usb_info_res *res = &cmd->res;

	ixxat_usb_setup_cmd(&req->dal_req, &res->dal_res);
	req->dal_req.req_code = cpu_to_le32(IXXAT_USB_BRD_CMD_GET_DEVINFO);
	res->dal_res.res_size = cpu_to_le32(sizeof(*res));

	ret = ixxat_usb_send_cmd(dev->udev, &req->dal_req);
	if (ret < 0)
		return ret;

	ret = ixxat_usb_rcv_cmd(dev->udev, &res->dal_res, 0xffff);
	if (ret < 0)
		return ret;

	if (dev_info) {
		const size_t id_size = sizeof(res->info.device_id);
		const size_t nm_size = sizeof(res->info.device_name);
		const u32 fpgav = le32_to_cpu(res->info.device_fpga_version);
		const u16 devv = le16_to_cpu(res->info.device_version);

		memcpy(dev_info->device_id, &res->info.device_id, id_size);
		memcpy(dev_info->device_name, &res->info.device_name, nm_size);
		dev_info->device_fpga_version = fpgav;
		dev_info->device_version = devv;
	}

	return le32_to_cpu(res->dal_res.ret_code);
}

static int ixxat_usb_start_ctrl(struct ixxat_usb_device *dev, u32 *time_ref)
{
	int ret;
	u8 data[IXXAT_USB_CMD_BUFFER_SIZE] = { 0 };
	struct ixxat_usb_start_cmd *cmd = (struct ixxat_usb_start_cmd *)data;
	struct ixxat_usb_start_req *req = &cmd->req;
	struct ixxat_usb_start_res *res = &cmd->res;

	ixxat_usb_setup_cmd(&req->dal_req, &res->dal_res);
	req->dal_req.req_code = cpu_to_le32(IXXAT_USB_CAN_CMD_START);
	req->dal_req.req_port = cpu_to_le16(dev->ctrl_index);
	res->dal_res.res_size = cpu_to_le32(sizeof(*res));
	res->start_time = 0;

	ret = ixxat_usb_send_cmd(dev->udev, &req->dal_req);
	if (ret < 0)
		return ret;

	ret = ixxat_usb_rcv_cmd(dev->udev, &res->dal_res, dev->ctrl_index);
	if (ret < 0)
		return ret;

	if (time_ref)
		*time_ref = le32_to_cpu(res->start_time);

	return le32_to_cpu(res->dal_res.ret_code);
}

static int ixxat_usb_stop_ctrl(struct ixxat_usb_device *dev)
{
	int ret;
	u8 data[IXXAT_USB_CMD_BUFFER_SIZE] = { 0 };
	struct ixxat_usb_stop_cmd *cmd = (struct ixxat_usb_stop_cmd *)data;
	struct ixxat_usb_stop_req *req = &cmd->req;
	struct ixxat_usb_stop_res *res = &cmd->res;

	ixxat_usb_setup_cmd(&req->dal_req, &res->dal_res);
	req->dal_req.req_size = cpu_to_le32(sizeof(*req));
	req->dal_req.req_code = cpu_to_le32(IXXAT_USB_CAN_CMD_STOP);
	req->dal_req.req_port = cpu_to_le16(dev->ctrl_index);
	req->action = cpu_to_le32(IXXAT_USB_STOP_ACTION_CLEARALL);

	ret = ixxat_usb_send_cmd(dev->udev, &req->dal_req);
	if (ret < 0)
		return ret;

	ret = ixxat_usb_rcv_cmd(dev->udev, &res->dal_res, dev->ctrl_index);
	if (ret < 0)
		return ret;

	ret = le32_to_cpu(res->dal_res.ret_code);
	if (!ret)
		dev->can.state = CAN_STATE_STOPPED;

	return ret;
}

static int ixxat_usb_power_ctrl(struct usb_device *dev, u8 mode)
{
	int ret;
	u8 data[IXXAT_USB_CMD_BUFFER_SIZE] = { 0 };
	struct ixxat_usb_power_cmd *cmd = (struct ixxat_usb_power_cmd *)data;
	struct ixxat_usb_power_req *req = &cmd->req;
	struct ixxat_usb_power_res *res = &cmd->res;

	ixxat_usb_setup_cmd(&req->dal_req, &res->dal_res);
	req->dal_req.req_size = cpu_to_le32(sizeof(*req));
	req->dal_req.req_code = cpu_to_le32(IXXAT_USB_BRD_CMD_POWER);
	req->mode = mode;

	ret = ixxat_usb_send_cmd(dev, &req->dal_req);
	if (ret < 0)
		return ret;

	ret = ixxat_usb_rcv_cmd(dev, &res->dal_res, 0xffff);
	if (ret < 0)
		return ret;

	return le32_to_cpu(res->dal_res.ret_code);
}

static int ixxat_usb_reset_ctrl(struct ixxat_usb_device *dev)
{
	int ret;
	u8 data[IXXAT_USB_CMD_BUFFER_SIZE] = { 0 };
	struct ixxat_usb_dal_cmd *cmd = (struct ixxat_usb_dal_cmd *)data;
	struct ixxat_usb_dal_req *req = &cmd->req;
	struct ixxat_usb_dal_res *res = &cmd->res;

	ixxat_usb_setup_cmd(req, res);
	req->req_code = cpu_to_le32(IXXAT_USB_CAN_CMD_RESET);
	req->req_port = cpu_to_le16(dev->ctrl_index);

	ret = ixxat_usb_send_cmd(dev->udev, req);
	if (ret < 0)
		return ret;

	ret = ixxat_usb_rcv_cmd(dev->udev, res, dev->ctrl_index);
	if (ret < 0)
		return ret;

	return le32_to_cpu(res->ret_code);
}

static void ixxat_usb_unlink_all_urbs(struct ixxat_usb_device *dev)
{
	usb_kill_anchored_urbs(&dev->rx_submitted);
	usb_kill_anchored_urbs(&dev->tx_submitted);
	atomic_set(&dev->active_tx_urbs, 0);
}

static int ixxat_usb_set_mode(struct net_device *netdev, enum can_mode mode)
{
	struct ixxat_usb_device *dev = netdev_priv(netdev);
	u32 time_ref;

	switch (mode) {
	case CAN_MODE_START:
		ixxat_usb_stop_ctrl(dev);
		ixxat_usb_start_ctrl(dev, &time_ref);
		break;
	case CAN_MODE_STOP:
	case CAN_MODE_SLEEP:
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ixxat_usb_get_berr_counter(const struct net_device *netdev,
				      struct can_berr_counter *bec)
{
	struct ixxat_usb_device *dev = netdev_priv(netdev);

	*bec = dev->bec;
	return 0;
}

static int ixxat_usb_handle_canmsg(struct ixxat_usb_device *dev,
				   struct ixxat_can_msg *rx)
{
	struct net_device *netdev = dev->netdev;
	union can_frame_union cfu;
	struct sk_buff *skb;
	const u32 flags = le32_to_cpu(rx->base.flags);
	const u8 dlc = IXXAT_USB_DECODE_DLC(flags);

	if (flags & IXXAT_USB_FDMSG_FLAGS_EDL)
		skb = alloc_canfd_skb(netdev, &cfu.cfd);
	else
		skb = alloc_can_skb(netdev, &cfu.cf);

	if (!skb)
		return -ENOMEM;

	if (flags & IXXAT_USB_FDMSG_FLAGS_EDL) {
		if (flags & IXXAT_USB_FDMSG_FLAGS_FDR)
			cfu.cfd->flags |= CANFD_BRS;

		if (flags & IXXAT_USB_FDMSG_FLAGS_ESI)
			cfu.cfd->flags |= CANFD_ESI;

		cfu.cfd->len = can_dlc2len(get_canfd_dlc(dlc));
	} else {
		cfu.cf->can_dlc = get_can_dlc(dlc);
	}

	if (flags & IXXAT_USB_MSG_FLAGS_OVR) {
		netdev->stats.rx_over_errors++;
		netdev->stats.rx_errors++;
		netdev_err(netdev, "Error: Message overflow\n");
	}

	cfu.cf->can_id = le32_to_cpu(rx->base.msg_id);

	if (flags & IXXAT_USB_MSG_FLAGS_EXT)
		cfu.cf->can_id |= CAN_EFF_FLAG;

	if (flags & IXXAT_USB_MSG_FLAGS_RTR) {
		cfu.cf->can_id |= CAN_RTR_FLAG;
	} else {
		if (dev->adapter == &usb2can_cl1)
			memcpy(cfu.cfd->data, rx->cl1.data, cfu.cfd->len);
		else
			memcpy(cfu.cfd->data, rx->cl2.data, cfu.cfd->len);
	}

	ixxat_usb_get_ts_tv(dev, le32_to_cpu(rx->base.time), &skb->tstamp);

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += cfu.cfd->len;
	netif_rx(skb);

	return 0;
}

static int ixxat_usb_handle_status(struct ixxat_usb_device *dev,
				   struct ixxat_can_msg *rx)
{
	struct net_device *netdev = dev->netdev;
	struct can_frame *can_frame;
	struct sk_buff *skb = alloc_can_err_skb(netdev, &can_frame);
	enum can_state new_state = CAN_STATE_ERROR_ACTIVE;
	u32 raw_status;

	if (dev->adapter == &usb2can_cl1)
		raw_status = le32_to_cpu(*(__le32 *)(rx->cl1.data));
	else
		raw_status = le32_to_cpu(*(__le32 *)(rx->cl2.data));

	if (!skb)
		return -ENOMEM;

	if (raw_status == IXXAT_USB_CAN_STATUS_OK) {
		dev->can.state = CAN_STATE_ERROR_ACTIVE;
		can_frame->can_id |= CAN_ERR_CRTL;
		can_frame->data[1] |= CAN_ERR_CRTL_ACTIVE;
	} else if (raw_status & IXXAT_USB_CAN_STATUS_BUSOFF) {
		can_frame->can_id |= CAN_ERR_BUSOFF;
		dev->can.can_stats.bus_off++;
		new_state = CAN_STATE_BUS_OFF;
		can_bus_off(netdev);
	} else {
		if (raw_status & IXXAT_USB_CAN_STATUS_ERRLIM) {
			can_frame->can_id |= CAN_ERR_CRTL;
			can_frame->data[1] |= CAN_ERR_CRTL_TX_WARNING;
			can_frame->data[1] |= CAN_ERR_CRTL_RX_WARNING;
			dev->can.can_stats.error_warning++;
			new_state = CAN_STATE_ERROR_WARNING;
		}

		if (raw_status & IXXAT_USB_CAN_STATUS_ERR_PAS) {
			can_frame->can_id |= CAN_ERR_CRTL;
			can_frame->data[1] |= CAN_ERR_CRTL_TX_PASSIVE;
			can_frame->data[1] |= CAN_ERR_CRTL_RX_PASSIVE;
			dev->can.can_stats.error_passive++;
			new_state = CAN_STATE_ERROR_PASSIVE;
		}

		if (raw_status & IXXAT_USB_CAN_STATUS_OVERRUN) {
			can_frame->can_id |= CAN_ERR_CRTL;
			can_frame->data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
			new_state = CAN_STATE_MAX;
		}
	}

	if (new_state == CAN_STATE_ERROR_ACTIVE) {
		dev->bec.txerr = 0;
		dev->bec.rxerr = 0;
	}

	if (new_state != CAN_STATE_MAX)
		dev->can.state = new_state;

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += can_frame->can_dlc;
	netif_rx(skb);

	return 0;
}

static int ixxat_usb_handle_error(struct ixxat_usb_device *dev,
				  struct ixxat_can_msg *rx)
{
	struct net_device *netdev = dev->netdev;
	struct can_frame *can_frame;
	struct sk_buff *skb = alloc_can_err_skb(netdev, &can_frame);
	u8 raw_error;

	if (dev->adapter == &usb2can_cl1) {
		raw_error = rx->cl1.data[0];
		dev->bec.rxerr = rx->cl1.data[3];
		dev->bec.txerr = rx->cl1.data[4];
	} else {
		raw_error = rx->cl2.data[0];
		dev->bec.rxerr = rx->cl2.data[3];
		dev->bec.txerr = rx->cl2.data[4];
	}

	if (dev->can.state == CAN_STATE_BUS_OFF)
		return 0;

	if (!skb)
		return -ENOMEM;

	switch (raw_error) {
	case IXXAT_USB_CAN_ERROR_ACK:
		can_frame->can_id |= CAN_ERR_ACK;
		netdev->stats.tx_errors++;
		break;
	case IXXAT_USB_CAN_ERROR_BIT:
		can_frame->can_id |= CAN_ERR_PROT;
		can_frame->data[2] |= CAN_ERR_PROT_BIT;
		netdev->stats.rx_errors++;
		break;
	case IXXAT_USB_CAN_ERROR_CRC:
		can_frame->can_id |= CAN_ERR_PROT;
		can_frame->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
		netdev->stats.rx_errors++;
		break;
	case IXXAT_USB_CAN_ERROR_FORM:
		can_frame->can_id |= CAN_ERR_PROT;
		can_frame->data[2] |= CAN_ERR_PROT_FORM;
		netdev->stats.rx_errors++;
		break;
	case IXXAT_USB_CAN_ERROR_STUFF:
		can_frame->can_id |= CAN_ERR_PROT;
		can_frame->data[2] |= CAN_ERR_PROT_STUFF;
		netdev->stats.rx_errors++;
		break;
	default:
		can_frame->can_id |= CAN_ERR_PROT;
		can_frame->data[2] |= CAN_ERR_PROT_UNSPEC;
		netdev->stats.rx_errors++;
		break;
	}

	netdev->stats.rx_packets++;
	netdev->stats.rx_bytes += can_frame->can_dlc;
	netif_rx(skb);

	return 0;
}

static int ixxat_usb_decode_buf(struct urb *urb)
{
	struct ixxat_usb_device *dev = urb->context;
	struct net_device *netdev = dev->netdev;
	struct ixxat_can_msg *can_msg;
	int ret = 0;
	u32 msg_end = urb->actual_length;
	u32 read_size = 0;
	u8 *data = urb->transfer_buffer;

	while (msg_end > 0) {
		u8 msg_type;

		can_msg = (struct ixxat_can_msg *)&data[read_size];

		if (!can_msg || !can_msg->base.size) {
			ret = -ENOTSUPP;
			netdev_err(netdev, "Error %d: Unsupported usb msg\n",
				   ret);
			break;
		}

		if ((read_size + can_msg->base.size + 1) > urb->actual_length) {
			ret = -EBADMSG;
			netdev_err(netdev,
				   "Error %d: Usb rx-buffer size unknown\n",
				   ret);
			break;
		}

		msg_type = le32_to_cpu(can_msg->base.flags);
		msg_type &= IXXAT_USB_MSG_FLAGS_TYPE;

		switch (msg_type) {
		case IXXAT_USB_CAN_DATA:
			ret = ixxat_usb_handle_canmsg(dev, can_msg);
			if (ret < 0)
				goto fail;
			break;
		case IXXAT_USB_CAN_STATUS:
			ret = ixxat_usb_handle_status(dev, can_msg);
			if (ret < 0)
				goto fail;
			break;
		case IXXAT_USB_CAN_ERROR:
			ret = ixxat_usb_handle_error(dev, can_msg);
			if (ret < 0)
				goto fail;
			break;
		case IXXAT_USB_CAN_TIMEOVR:
			ixxat_usb_get_ts_tv(dev, can_msg->base.time, NULL);
			break;
		case IXXAT_USB_CAN_INFO:
		case IXXAT_USB_CAN_WAKEUP:
		case IXXAT_USB_CAN_TIMERST:
			break;
		default:
			netdev_err(netdev,
				   "Unhandled rec type 0x%02x (%d): ignored\n",
				   msg_type, msg_type);
			break;
		}

		read_size += (can_msg->base.size + 1);
		msg_end -= (can_msg->base.size + 1);
	}

fail:
	if (ret < 0)
		netdev_err(netdev, "Error %d: Buffer decoding failed\n", ret);

	return ret;
}

static void ixxat_usb_read_bulk(struct urb *urb)
{
	struct ixxat_usb_device *dev = urb->context;
	const struct ixxat_usb_adapter *adapter = dev->adapter;
	struct net_device *netdev = dev->netdev;
	struct usb_device *udev = dev->udev;
	int ret;

	if (!netif_device_present(netdev))
		return;

	switch (urb->status) {
	case 0: /* success */
		break;
	case -EPROTO:
	case -EILSEQ:
	case -ENOENT:
	case -ECONNRESET:
	case -ESHUTDOWN:
		return;
	default:
		netdev_err(netdev, "Rx urb aborted /(%d)\n", urb->status);
		goto resubmit_urb;
	}

	if (urb->actual_length > 0)
		if (dev->state & IXXAT_USB_STATE_STARTED)
			ret = ixxat_usb_decode_buf(urb);

resubmit_urb:
	usb_fill_bulk_urb(urb, udev, usb_rcvbulkpipe(udev, dev->ep_msg_in),
			  urb->transfer_buffer, adapter->buffer_size_rx,
			  ixxat_usb_read_bulk, dev);

	usb_anchor_urb(urb, &dev->rx_submitted);
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret == 0)
		return;

	usb_unanchor_urb(urb);

	if (ret == -ENODEV)
		netif_device_detach(netdev);
	else
		netdev_err(netdev,
			   "Error %d: Failed to resubmit read bulk urb\n", ret);
}

static void ixxat_usb_write_bulk(struct urb *urb)
{
	struct ixxat_tx_urb_context *context = urb->context;
	struct ixxat_usb_device *dev;
	struct net_device *netdev;

	if (WARN_ON(!context))
		return;

	dev = context->dev;
	netdev = dev->netdev;
	atomic_dec(&dev->active_tx_urbs);

	if (!netif_device_present(netdev))
		return;

	if (!urb->status) {
		netdev->stats.tx_packets += context->count;
		netdev->stats.tx_bytes += context->dlc;
	} else {
		netdev_err(netdev, "Error %d: Tx urb aborted\n", urb->status);
	}

	can_get_echo_skb(netdev, context->echo_index);
	context->echo_index = IXXAT_USB_MAX_TX_URBS;

	if (!urb->status)
		netif_wake_queue(netdev);
}

static void ixxat_usb_encode_msg(struct ixxat_usb_device *dev,
				 struct sk_buff *skb, u8 *obuf,
				 size_t *size)
{
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	struct ixxat_can_msg can_msg = {0};
	struct ixxat_can_msg_base *msg_base = &can_msg.base;

	if (cf->can_id & CAN_RTR_FLAG)
		msg_base->flags |= IXXAT_USB_MSG_FLAGS_RTR;

	if (cf->can_id & CAN_EFF_FLAG) {
		msg_base->flags |= IXXAT_USB_MSG_FLAGS_EXT;
		msg_base->msg_id = cf->can_id & CAN_EFF_MASK;
	} else {
		msg_base->msg_id = cf->can_id & CAN_SFF_MASK;
	}

	if (can_is_canfd_skb(skb)) {
		msg_base->flags |= IXXAT_USB_FDMSG_FLAGS_EDL;

		if (!(cf->can_id & CAN_RTR_FLAG) && (cf->flags & CANFD_BRS))
			msg_base->flags |= IXXAT_USB_FDMSG_FLAGS_FDR;

		msg_base->flags |= IXXAT_USB_ENCODE_DLC(can_len2dlc(cf->len));
	} else {
		msg_base->flags |= IXXAT_USB_ENCODE_DLC(cf->len);
	}

	le32_to_cpus(&msg_base->flags);
	le32_to_cpus(&msg_base->msg_id);

	msg_base->size = (u8)(sizeof(*msg_base) - 1);
	if (dev->adapter == &usb2can_cl1) {
		msg_base->size += (u8)(sizeof(can_msg.cl1) - CAN_MAX_DLEN);
		msg_base->size += cf->len;
		memcpy(can_msg.cl1.data, cf->data, cf->len);
	} else {
		msg_base->size += (u8)(sizeof(can_msg.cl2) - CANFD_MAX_DLEN);
		msg_base->size += cf->len;
		memcpy(can_msg.cl2.data, cf->data, cf->len);
	}

	*size = msg_base->size + 1;
	memcpy(obuf, &can_msg, *size);
	skb->data_len = *size;
}

static netdev_tx_t ixxat_usb_start_xmit(struct sk_buff *skb,
					struct net_device *netdev)
{
	struct ixxat_usb_device *dev = netdev_priv(netdev);
	struct ixxat_tx_urb_context *context = NULL;
	struct net_device_stats *stats = &netdev->stats;
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	struct urb *urb;
	u8 *obuf;
	int i;
	int ret;
	size_t size = dev->adapter->buffer_size_tx;

	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;

	for (i = 0; i < IXXAT_USB_MAX_TX_URBS; i++) {
		if (dev->tx_contexts[i].echo_index == IXXAT_USB_MAX_TX_URBS) {
			context = dev->tx_contexts + i;
			break;
		}
	}

	if (WARN_ON_ONCE(!context))
		return NETDEV_TX_BUSY;

	urb = context->urb;
	obuf = urb->transfer_buffer;

	ixxat_usb_encode_msg(dev, skb, obuf, &size);

	context->echo_index = i;
	context->dlc = cf->len;
	context->count = 1;

	urb->transfer_buffer_length = size;
	usb_anchor_urb(urb, &dev->tx_submitted);
	can_put_echo_skb(skb, netdev, context->echo_index);
	atomic_inc(&dev->active_tx_urbs);

	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(ret)) {
		can_free_echo_skb(netdev, context->echo_index);
		usb_unanchor_urb(urb);
		atomic_dec(&dev->active_tx_urbs);

		context->echo_index = IXXAT_USB_MAX_TX_URBS;

		if (ret == -ENODEV) {
			netif_device_detach(netdev);
		} else {
			stats->tx_dropped++;
			netdev_err(netdev,
				   "Error %d: Submitting tx-urb failed\n", ret);
		}
	} else {
		if (atomic_read(&dev->active_tx_urbs) >= IXXAT_USB_MAX_TX_URBS)
			netif_stop_queue(netdev);
	}

	return NETDEV_TX_OK;
}

static int ixxat_usb_setup_rx_urbs(struct ixxat_usb_device *dev)
{
	int i;
	int ret = 0;
	const struct ixxat_usb_adapter *adapter = dev->adapter;
	struct net_device *netdev = dev->netdev;
	struct usb_device *udev = dev->udev;

	for (i = 0; i < IXXAT_USB_MAX_RX_URBS; i++) {
		struct urb *urb;
		u8 *buf;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			ret = -ENOMEM;
			netdev_err(netdev, "Error %d: No memory for URBs\n",
				   ret);
			break;
		}

		buf = kmalloc(adapter->buffer_size_rx, GFP_KERNEL);
		if (!buf) {
			usb_free_urb(urb);
			ret = -ENOMEM;
			netdev_err(netdev,
				   "Error %d: No memory for USB-buffer\n", ret);
			break;
		}

		usb_fill_bulk_urb(urb, udev,
				  usb_rcvbulkpipe(udev, dev->ep_msg_in), buf,
				  adapter->buffer_size_rx, ixxat_usb_read_bulk,
				  dev);

		urb->transfer_flags |= URB_FREE_BUFFER;
		usb_anchor_urb(urb, &dev->rx_submitted);

		ret = usb_submit_urb(urb, GFP_KERNEL);
		if (ret < 0) {
			usb_unanchor_urb(urb);
			kfree(buf);
			usb_free_urb(urb);

			if (ret == -ENODEV)
				netif_device_detach(netdev);

			break;
		}

		usb_free_urb(urb);
	}

	if (i == 0)
		netdev_err(netdev, "Error: Couldn't setup any rx-URBs\n");

	return ret;
}

static int ixxat_usb_setup_tx_urbs(struct ixxat_usb_device *dev)
{
	int i;
	int ret = 0;
	const struct ixxat_usb_adapter *adapter = dev->adapter;
	struct net_device *netdev = dev->netdev;
	struct usb_device *udev = dev->udev;

	for (i = 0; i < IXXAT_USB_MAX_TX_URBS; i++) {
		struct ixxat_tx_urb_context *context;
		struct urb *urb = NULL;
		u8 *buf = NULL;

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			ret = -ENOMEM;
			netdev_err(netdev, "Error %d: No memory for URBs\n",
				   ret);
			break;
		}

		buf = kmalloc(adapter->buffer_size_tx, GFP_KERNEL);
		if (!buf) {
			usb_free_urb(urb);
			ret = -ENOMEM;
			netdev_err(netdev,
				   "Error %d: No memory for USB-buffer\n", ret);
			break;
		}

		context = dev->tx_contexts + i;
		context->dev = dev;
		context->urb = urb;

		usb_fill_bulk_urb(urb, udev,
				  usb_sndbulkpipe(udev, dev->ep_msg_out), buf,
				  adapter->buffer_size_tx, ixxat_usb_write_bulk,
				  context);

		urb->transfer_flags |= URB_FREE_BUFFER;
	}

	if (i == 0) {
		netdev_err(netdev, "Error: Couldn't setup any tx-URBs\n");
		usb_kill_anchored_urbs(&dev->rx_submitted);
	}

	return ret;
}

static void ixxat_usb_disconnect(struct usb_interface *intf)
{
	struct ixxat_usb_device *dev;
	struct ixxat_usb_device *prev_dev;

	/* unregister the given device and all previous devices */
	for (dev = usb_get_intfdata(intf); dev; dev = prev_dev) {
		prev_dev = dev->prev_dev;
		unregister_netdev(dev->netdev);
		free_candev(dev->netdev);
	}

	usb_set_intfdata(intf, NULL);
}

static int ixxat_usb_start(struct ixxat_usb_device *dev)
{
	int ret;
	int i;
	u32 time_ref = 0;
	const struct ixxat_usb_adapter *adapter = dev->adapter;

	ret = ixxat_usb_setup_rx_urbs(dev);
	if (ret < 0)
		return ret;

	ret = ixxat_usb_setup_tx_urbs(dev);
	if (ret < 0)
		return ret;

	/* Try to reset the controller, in case it is already initialized
	 * from a previous unclean shutdown
	 */
	ixxat_usb_reset_ctrl(dev);

	if (adapter->init_ctrl) {
		ret = adapter->init_ctrl(dev);
		if (ret < 0)
			goto fail;
	}

	if (dev->ctrl_opened_count == 0) {
		ret = ixxat_usb_start_ctrl(dev, &time_ref);
		if (ret < 0)
			goto fail;

		ixxat_usb_set_ts_now(dev, time_ref);
	}

	dev->ctrl_opened_count++;

	dev->bec.txerr = 0;
	dev->bec.rxerr = 0;

	dev->state |= IXXAT_USB_STATE_STARTED;
	dev->can.state = CAN_STATE_ERROR_ACTIVE;

	return 0;

fail:
	if (ret == -ENODEV)
		netif_device_detach(dev->netdev);

	netdev_err(dev->netdev, "Error %d: Couldn't submit control\n", ret);

	for (i = 0; i < IXXAT_USB_MAX_TX_URBS; i++) {
		usb_free_urb(dev->tx_contexts[i].urb);
		dev->tx_contexts[i].urb = NULL;
	}

	return ret;
}

static int ixxat_usb_open(struct net_device *netdev)
{
	struct ixxat_usb_device *dev = netdev_priv(netdev);
	int ret;

	/* common open */
	ret = open_candev(netdev);
	if (ret < 0)
		return ret;

	/* finally start device */
	ret = ixxat_usb_start(dev);
	if (ret < 0) {
		netdev_err(netdev, "Error %d: Couldn't start device.\n", ret);
		close_candev(netdev);
		return ret;
	}

	netif_start_queue(netdev);

	return 0;
}

static int ixxat_usb_stop(struct net_device *netdev)
{
	int ret = 0;
	struct ixxat_usb_device *dev = netdev_priv(netdev);

	netif_stop_queue(netdev);
	ixxat_usb_unlink_all_urbs(dev);

	if (dev->ctrl_opened_count == 1) {
		ret = ixxat_usb_stop_ctrl(dev);
		if (ret < 0)
			return ret;
	}
	dev->ctrl_opened_count--;

	close_candev(netdev);

	dev->can.state = CAN_STATE_STOPPED;

	return 0;
}

static const struct net_device_ops ixxat_usb_netdev_ops = {
	.ndo_open = ixxat_usb_open,
	.ndo_stop = ixxat_usb_stop,
	.ndo_start_xmit = ixxat_usb_start_xmit
};

static const struct ixxat_usb_adapter *ixxat_usb_get_adapter(const u16 id)
{
	switch (id) {
	case USB2CAN_COMPACT_PRODUCT_ID:
	case USB2CAN_EMBEDDED_PRODUCT_ID:
	case USB2CAN_PROFESSIONAL_PRODUCT_ID:
	case USB2CAN_AUTOMOTIVE_PRODUCT_ID:
		return &usb2can_cl1;
	case USB2CAN_FD_COMPACT_PRODUCT_ID:
	case USB2CAN_FD_PROFESSIONAL_PRODUCT_ID:
	case USB2CAN_FD_AUTOMOTIVE_PRODUCT_ID:
	case USB2CAN_FD_PCIE_MINI_PRODUCT_ID:
	case USB2CAR_PRODUCT_ID:
		return &usb2can_cl2;
	case CAN_IDM100_PRODUCT_ID:
		return &can_idm100;
	case CAN_IDM101_PRODUCT_ID:
		return &can_idm101;
	default:
		return NULL;
	}
}

static int ixxat_usb_create_dev(struct usb_interface *intf,
				const struct ixxat_usb_adapter *adapter,
				u16 ctrl_index)
{
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct ixxat_usb_device *dev;
	struct net_device *netdev;
	int ret;
	int i;

	netdev = alloc_candev(sizeof(*dev), IXXAT_USB_MAX_TX_URBS);
	if (!netdev) {
		dev_err(&intf->dev, "Cannot allocate candev\n");
		return -ENOMEM;
	}

	dev = netdev_priv(netdev);

	dev->ctrl_opened_count = 0;
	dev->udev = usb_dev;
	dev->netdev = netdev;
	dev->adapter = adapter;
	dev->ctrl_index = ctrl_index;
	dev->state = IXXAT_USB_STATE_CONNECTED;

	i = ctrl_index + adapter->ep_offs;
	dev->ep_msg_in = adapter->ep_msg_in[i];
	dev->ep_msg_out = adapter->ep_msg_out[i];

	dev->can.clock.freq = adapter->clock;
	dev->can.bittiming_const = adapter->bt;
	dev->can.data_bittiming_const = adapter->btd;

	dev->can.do_set_mode = ixxat_usb_set_mode;
	dev->can.do_get_berr_counter = ixxat_usb_get_berr_counter;

	dev->can.ctrlmode_supported = adapter->modes;

	netdev->netdev_ops = &ixxat_usb_netdev_ops;

	netdev->flags |= IFF_ECHO;

	init_usb_anchor(&dev->rx_submitted);
	init_usb_anchor(&dev->tx_submitted);

	atomic_set(&dev->active_tx_urbs, 0);

	for (i = 0; i < IXXAT_USB_MAX_TX_URBS; i++)
		dev->tx_contexts[i].echo_index = IXXAT_USB_MAX_TX_URBS;

	dev->prev_dev = usb_get_intfdata(intf);
	usb_set_intfdata(intf, dev);

	SET_NETDEV_DEV(netdev, &intf->dev);
	ret = register_candev(netdev);
	if (ret < 0) {
		dev_err(&intf->dev, "Error %d: Failed to register can device\n",
			ret);
		goto free_candev;
	}

	if (dev->prev_dev)
		(dev->prev_dev)->next_dev = dev;

	ret = ixxat_usb_get_dev_info(dev, &dev->dev_info);
	if (ret < 0) {
		dev_err(&intf->dev,
			"Error %d: Failed to get device information\n", ret);
		goto unreg_candev;
	}

	netdev_info(netdev, "%s: Connected Channel %u (device %s)\n",
		    dev->dev_info.device_name, ctrl_index,
		    dev->dev_info.device_id);

	return 0;

unreg_candev:
	unregister_candev(netdev);
free_candev:
	usb_set_intfdata(intf, dev->prev_dev);
	free_candev(netdev);
	return ret;
}

static int ixxat_usb_probe(struct usb_interface *intf,
			   const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	struct usb_host_interface *host_intf = intf->altsetting;
	const struct ixxat_usb_adapter *adapter;
	struct ixxat_dev_caps dev_caps;
	u16 i;
	int ret;

	usb_reset_configuration(udev);

	adapter = ixxat_usb_get_adapter(id->idProduct);
	if (!adapter) {
		dev_err(&intf->dev, "%s: Unknown device id %d\n",
			IXXAT_USB_DRIVER_NAME, id->idProduct);
		return -ENODEV;
	}

	for (i = 0; i < host_intf->desc.bNumEndpoints; i++) {
		const u8 epaddr = host_intf->endpoint[i].desc.bEndpointAddress;
		int match;
		u8 j;

		/* Check if usb-endpoint address matches known usb-endpoints */
		for (j = 0; j < IXXAT_USB_MAX_CHANNEL; j++) {
			u8 ep_msg_in = adapter->ep_msg_in[j];
			u8 ep_msg_out = adapter->ep_msg_in[j];

			if (epaddr == ep_msg_in || epaddr == ep_msg_out) {
				match = 1;
				break;
			}
		}

		if (!match)
			return -ENODEV;
	}

	ret = ixxat_usb_power_ctrl(udev, IXXAT_USB_POWER_WAKEUP);
	if (ret < 0)
		return ret;

	msleep(IXXAT_USB_POWER_WAKEUP_TIME);

	ret = ixxat_usb_get_dev_caps(udev, &dev_caps);
	if (ret < 0) {
		dev_err(&intf->dev, "Failed to get device capabilities\n");
		return ret;
	}

	ret = -ENODEV;
	for (i = 0; i < dev_caps.bus_ctrl_count; i++) {
		u8 bustype = IXXAT_USB_BUS_TYPE(dev_caps.bus_ctrl_types[i]);

		if (bustype == IXXAT_USB_BUS_CAN)
			ret = ixxat_usb_create_dev(intf, adapter, i);

		if (ret < 0) {
			/* deregister already created devices */
			ixxat_usb_disconnect(intf);
			return ret;
		}
	}

	return ret;
}

static struct usb_driver ixxat_usb_driver = {
	.name = IXXAT_USB_DRIVER_NAME,
	.probe = ixxat_usb_probe,
	.disconnect = ixxat_usb_disconnect,
	.id_table = ixxat_usb_table,
};

module_usb_driver(ixxat_usb_driver);
