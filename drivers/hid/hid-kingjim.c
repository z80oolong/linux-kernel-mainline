/*
 *  HID driver for various devices which are apparently based on the same chipset
 *  from certain vendor which produces chips that contain wrong LogicalMaximum
 *  value in their HID report descriptor. Currently supported devices are:
 *
 *    kingjim XMC10
 *
 *  Copyright (c) 2018 Takao Akaki <mongonta@gmail.com>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/hid.h>
#include <linux/module.h>

#include "hid-ids.h"

struct kingjim_drvdata {
	unsigned long quirks;
	struct input_dev *input;
};

static __u8 *kingjim_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int *rsize)
{
	if (*rsize >= 56 && rdesc[54] == 0x25 && (rdesc[55] == 0x65 || rdesc[55] == 0x6a)) {
		hid_info(hdev, "Fixing up logical maximum in report descriptor (kingjim)\n");
		rdesc[55] = 0xdd;
	}
	return rdesc;
}

static const struct hid_device_id kingjim_devices[] = {
	{ HID_I2C_DEVICE(USB_VENDOR_ID_KINGJIM, USB_DEVICE_ID_KINGJIM_XMC10_KEYBOARD) },
	{ }
};
MODULE_DEVICE_TABLE(hid, kingjim_devices);

static struct hid_driver kingjim_driver = {
	.name = "kingjim",
	.id_table = kingjim_devices,
	.report_fixup = kingjim_report_fixup
};
module_hid_driver(kingjim_driver);

MODULE_LICENSE("GPL");
