/*
 * portabook_backlight.c - Portabook battery driver
 * Copyright (C) 2016  MURAMATSU Atsushi <amura@tomato.sakura.ne.jp>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/backlight.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/kernel.h>

#ifndef FB_BLANK_UNBLANK
#define FB_BLANK_UNBLANK 3
#endif
#ifndef FB_BLANK_POWERDOWN
#define FB_BLANK_POWERDOWN 0
#endif

static int intel_soc_pmic_rw_init(void);
static void intel_soc_pmic_writeb(int reg, u8 val);
static u8 intel_soc_pmic_readb(int reg);

static void
portabook_disable_backlight(void)
{
    intel_soc_pmic_writeb(0x51, 0x00);
    intel_soc_pmic_writeb(0x4B, 0x7F);
}

static void
portabook_enable_backlight(void)
{
    intel_soc_pmic_writeb(0x4B, 0xFF);
    intel_soc_pmic_writeb(0x4E, 0xFF);
    intel_soc_pmic_writeb(0x51, 0x01);
}

static u32
portabook_get_backlight(void)
{
    return intel_soc_pmic_readb(0x4E);
}
 
static void
portabook_set_backlight(u32 level)
{
    intel_soc_pmic_writeb(0x4E, level);
}

/* interface for backlight */
static int backlight_disabled;
static struct backlight_device *portabook_backlight_device;
static int
portabook_backlight_update_status(struct backlight_device *dev)
{
    if (dev->props.power == FB_BLANK_POWERDOWN ||
	(dev->props.state & (BL_CORE_SUSPENDED | BL_CORE_FBBLANK))) {
	portabook_set_backlight(0);
	portabook_disable_backlight();
	backlight_disabled = 1;
    }
    else {
	if (backlight_disabled)
	    portabook_enable_backlight();
	portabook_set_backlight(dev->props.brightness);
	backlight_disabled = 0;
    }
    return 1;
}

static int
portabook_backlight_get_brightness(struct backlight_device *dev)
{
    return portabook_get_backlight();
}

static struct backlight_ops portabook_backlight_ops = {
    .update_status = portabook_backlight_update_status,
    .get_brightness = portabook_backlight_get_brightness,
};

static int
portabook_backlight_device_register(struct device *parent)
{
    struct backlight_properties props;
    memset(&props, 0, sizeof(props));
    props.type = BACKLIGHT_RAW;
    props.max_brightness = 255;
    props.brightness = portabook_get_backlight();
    props.power = FB_BLANK_UNBLANK;
    
    portabook_enable_backlight();
    backlight_disabled = 0;
    portabook_set_backlight(props.brightness);

    portabook_backlight_device =
	backlight_device_register("portabook_bl",
				  parent, NULL,
				  &portabook_backlight_ops,
				  &props);
    if (IS_ERR(portabook_backlight_device)) {
	printk("portabook BACKLIGHT register error");
	return -ENODEV;
    }
    return 0;
}

static void
portabook_backlight_device_unregister(void)
{
    backlight_device_unregister(portabook_backlight_device);
}

static struct i2c_client *intel_soc_pmic_i2c;

static int
intel_soc_pmic_rw_init(void)
{
    struct device *dev;
    dev = bus_find_device_by_name(&i2c_bus_type, NULL, "i2c-INT33FD:00");
    if (!dev)
	return -1;
    intel_soc_pmic_i2c = i2c_verify_client(dev);
    
    if (!intel_soc_pmic_i2c)
	return -1;
    return 0;
}

static u8
intel_soc_pmic_readb(int reg)
{
    int s;
    char buf[1];

    if (!intel_soc_pmic_i2c) return -1;
    
    /* send reg no */
    buf[0] = reg;
    s = i2c_master_send(intel_soc_pmic_i2c, buf, 1);
    if (s < 0) return 255;
    /* recv data */
    s = i2c_master_recv(intel_soc_pmic_i2c, buf, 1);
    if (s < 0) return 255;
    
    return buf[0] & 0xff;
}

static void
intel_soc_pmic_writeb(int reg, u8 val)
{
    char buf[2];
    if (!intel_soc_pmic_i2c) return;

    buf[0] = reg;
    buf[1] = val;
    i2c_master_send(intel_soc_pmic_i2c, buf, 2);
}

int
portabook_backlight_init(void)
{
    int error = 0;
printk("Portabook backlight initialized.\n");
    error = intel_soc_pmic_rw_init();
    if (error)
	return -ENODEV;

    error = portabook_backlight_device_register(&intel_soc_pmic_i2c->dev);
    if (error)
	return -EINVAL;
    return 0;
}

void
portabook_backlight_cleanup(void)
{
    portabook_backlight_device_unregister();
}
