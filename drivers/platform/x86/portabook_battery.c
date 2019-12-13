/*
 * portabook_battery.c - Portabook battery driver
 * Copyright (C) 2016  MURAMATSU Atsushi <amura@tomato.sakura.ne.jp>
 *
 * This code is based on
 *   driver/acpi/battery.c and driver/power/ds2760_battery.c
 *   from linux-4.4 kernel
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
/*
 *  battery.c - ACPI Battery Driver (Revision: 2.0)
 *
 *  Copyright (C) 2007 Alexey Starikovskiy <astarikovskiy@suse.de>
 *  Copyright (C) 2004-2007 Vladimir Lebedev <vladimir.p.lebedev@intel.com>
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
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
/*
 * Driver for batteries with DS2760 chips inside.
 *
 * Copyright © 2007 Anton Vorontsov
 *	       2004-2007 Matt Reimer
 *	       2004 Szabolcs Gyurko
 *
 * Use consistent with the GNU GPL is permitted,
 * provided that this copyright notice is
 * preserved in its entirety in all copies and derived works.
 *
 * Author:  Anton Vorontsov <cbou@mail.ru>
 *	    February 2007
 *
 *	    Matt Reimer <mreimer@vpop.net>
 *	    April 2004, 2005, 2007
 *
 *	    Szabolcs Gyurko <szabolcs.gyurko@tlt.hu>
 *	    September 2004
 */

#include <linux/module.h>
#include <linux/param.h>
#include <linux/jiffies.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>

#define I2C_DEVICE_NAME	"portabook_batt"

#define I2C_ADAPTER_NAME "Synopsys DesignWare I2C adapter"

#ifndef ACPI_BATTERY_STATE_DISCHARGING
#define ACPI_BATTERY_STATE_DISCHARGING	(1 << 0)
#endif
#ifndef ACPI_BATTERY_STATE_CHARGING
#define ACPI_BATTERY_STATE_CHARGING	(1 << 1)
#endif
#ifndef ACPI_BATTERY_STATE_CRITICAL
#define ACPI_BATTERY_STATE_CRITICAL	(1 << 2)
#endif
#ifndef ACPI_BATTERY_VALUE_UNKNOWN
#define ACPI_BATTERY_VALUE_UNKNOWN	0xFFFFFFFF
#endif

#define DESIGN_CAPACITY			4800	/* 4800 mAh */
#define DESIGN_VOLTAGE			3800	/* 3.8V */
#define DESIGN_WARN_CAPACITY		800	/* 800 mAh */

#define BATT_INDEX_CMD			0x82
#define BATT_DATA_CMD			0x80

#define	BATT_INFO_LAST_CAP_H		0x144
#define	BATT_INFO_LAST_CAP_L		0x145
#define	BATT_INFO_STATUS_H		0x1A0
#define	BATT_INFO_STATUS_L		0x1A1
#define	BATT_INFO_PRESENT_RATE_H	0x1A2
#define	BATT_INFO_PRESENT_RATE_L	0x1A3
#define	BATT_INFO_REMAIN_CAP_H		0x1A4
#define	BATT_INFO_REMAIN_CAP_L		0x1A5
#define	BATT_INFO_PRESENT_VOLT_H	0x1A6
#define	BATT_INFO_PRESENT_VOLT_L	0x1A7
#define BATT_INFO_AC_ADAPTER		0x10B

struct portabook_battery {
    struct i2c_client *i2c_client;
    struct mutex lock;
    
    struct power_supply *bat;
    struct power_supply_desc bat_desc;
    struct power_supply *ac;
    struct power_supply_desc ac_desc;
    
    /* portabook battery data,
       valid after calling portabook_batt_battery_read_status() */
    unsigned long update_time;	/* jiffies when data read */
    
    int rate_now;
    int capacity_now;
    int voltage_now;
    int full_charge_capacity;
    int state;
    int ac_adapter;
};

static unsigned int battery_info_cache_time = 1000;
module_param(battery_info_cache_time, uint, 0644);
MODULE_PARM_DESC(battery_info_cache_time,
		 "battery information caching time in milliseconds");

static unsigned int battery_fullcharged_percentage = 95;
module_param(battery_fullcharged_percentage, uint, 0644);
MODULE_PARM_DESC(battery_fullcharged_percentage,
		 "percentage of capacit to be considered as full charged");

static unsigned int battery_ignore_discharge_rate = 200;
module_param(battery_ignore_discharge_rate, uint, 0644);
MODULE_PARM_DESC(battery_ignore_discharge_rate,
		 "smaller discharge rate in mA than this value is ignored");

static int
read_battinfo_reg(struct i2c_client *i2c_client, int reg, u8 *value)
{
    u8 buf[2];
    s32 s;
    buf[0] = reg >> 8;
    buf[1] = reg & 0xff;
    s = i2c_smbus_write_i2c_block_data(i2c_client, BATT_INDEX_CMD,
				       sizeof(buf), buf);
    if (s < 0) return s;
    s = i2c_smbus_read_byte_data(i2c_client, BATT_DATA_CMD);
    if (s < 0) return s;
    *value = s;
    return 0;
}

static int
portabook_battery_read_status(struct portabook_battery *di)
{
    struct i2c_client *i2c_client = di->i2c_client;
    u8 buf[2];
    int s;

    mutex_lock(&di->lock);
    if (di->update_time &&
	time_before(jiffies, di->update_time +
		             msecs_to_jiffies(battery_info_cache_time)))
	goto success;

    s = read_battinfo_reg(i2c_client, BATT_INFO_LAST_CAP_H, &buf[0]);
    if (s < 0) goto error;
    s = read_battinfo_reg(i2c_client, BATT_INFO_LAST_CAP_L, &buf[1]);
    if (s < 0) goto error;
    di->full_charge_capacity = (buf[0] << 8) | buf[1];

    s = read_battinfo_reg(i2c_client, BATT_INFO_STATUS_H, &buf[0]);
    if (s < 0) goto error;
    s = read_battinfo_reg(i2c_client, BATT_INFO_STATUS_L, &buf[1]);
    if (s < 0) goto error;
    di->state = (buf[0] << 8) | buf[1];

    s = read_battinfo_reg(i2c_client, BATT_INFO_PRESENT_RATE_H, &buf[0]);
    if (s < 0) goto error;
    s = read_battinfo_reg(i2c_client, BATT_INFO_PRESENT_RATE_L, &buf[1]);
    if (s < 0) goto error;
    di->rate_now = (buf[0] << 8) | buf[1];

    s = read_battinfo_reg(i2c_client, BATT_INFO_REMAIN_CAP_H, &buf[0]);
    if (s < 0) goto error;
    s = read_battinfo_reg(i2c_client, BATT_INFO_REMAIN_CAP_L, &buf[1]);
    if (s < 0) goto error;
    di->capacity_now = (buf[0] << 8) | buf[1];
    
    s = read_battinfo_reg(i2c_client, BATT_INFO_PRESENT_VOLT_H, &buf[0]);
    if (s < 0) goto error;
    s = read_battinfo_reg(i2c_client, BATT_INFO_PRESENT_VOLT_L, &buf[1]);
    if (s < 0) goto error;
    di->voltage_now = (buf[0] << 8) | buf[1];

    s = read_battinfo_reg(i2c_client, BATT_INFO_AC_ADAPTER, &buf[0]);
    if (s < 0) goto error;
    di->ac_adapter = buf[0];
    
    di->update_time = jiffies;
    
 success:
    mutex_unlock(&di->lock);
    return 0;
	
 error:
    mutex_unlock(&di->lock);
    dev_warn(&di->i2c_client->dev, "call to read_battinfo_reg failed\n");
    return 1;
}

static int
portabook_battery_is_charged(struct portabook_battery *battery)
{
    /* charging or discharing with high rate */
    if (battery->state & ACPI_BATTERY_STATE_CHARGING)
	return 0;
    if (battery->state & ACPI_BATTERY_STATE_DISCHARGING &&
	battery->rate_now >= battery_ignore_discharge_rate)
	return 0;

    /* battery not reporting charge */
    if (battery->capacity_now == ACPI_BATTERY_VALUE_UNKNOWN ||
	battery->capacity_now == 0)
	return 0;
    
    /* good batteries update full_charge as the batteries degrade */
    if (battery->full_charge_capacity*battery_fullcharged_percentage
	<= battery->capacity_now*100)
	return 1;
    
    /* fallback to using design values for broken batteries */
    if (DESIGN_CAPACITY >= battery->capacity_now)
	return 1;
    
    /* we don't do any sort of metric based on percentages */
    return 0;
}

static int
portabook_battery_get_property(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
    int ret = 0;
    struct portabook_battery *battery = power_supply_get_drvdata(psy);
    
    portabook_battery_read_status(battery);
    
    switch (psp) {
    case POWER_SUPPLY_PROP_STATUS:
	if (battery->state & ACPI_BATTERY_STATE_DISCHARGING &&
	    battery->rate_now >= battery_ignore_discharge_rate)
	    val->intval = POWER_SUPPLY_STATUS_DISCHARGING;
	else if (battery->state & ACPI_BATTERY_STATE_CHARGING)
	    val->intval = POWER_SUPPLY_STATUS_CHARGING;
	else if (portabook_battery_is_charged(battery))
	    val->intval = POWER_SUPPLY_STATUS_FULL;
	else
	    val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
	break;
	
    case POWER_SUPPLY_PROP_PRESENT:
	val->intval = 1;
	break;
	
    case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
	val->intval = DESIGN_VOLTAGE * 1000;
	break;
	
    case POWER_SUPPLY_PROP_VOLTAGE_NOW:
	val->intval = battery->voltage_now * 1000;
	break;
	
    case POWER_SUPPLY_PROP_CURRENT_NOW:
    case POWER_SUPPLY_PROP_POWER_NOW:
	val->intval = battery->rate_now * 1000;
	break;
	
    case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
    case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
	val->intval = DESIGN_CAPACITY * 1000;
	break;
	
    case POWER_SUPPLY_PROP_CHARGE_FULL:
    case POWER_SUPPLY_PROP_ENERGY_FULL:
	val->intval = battery->full_charge_capacity * 1000;
	break;
	
    case POWER_SUPPLY_PROP_CHARGE_NOW:
    case POWER_SUPPLY_PROP_ENERGY_NOW:
	val->intval = battery->capacity_now * 1000;
	break;
	
    case POWER_SUPPLY_PROP_CAPACITY:
	if (battery->capacity_now && battery->full_charge_capacity)
	    val->intval = battery->capacity_now * 100 /
		battery->full_charge_capacity;
	else
	    val->intval = 0;
	break;
	
    case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
	if (battery->state & ACPI_BATTERY_STATE_CRITICAL)
	    val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
	else if (battery->capacity_now <= DESIGN_WARN_CAPACITY)
	    val->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
	else if (portabook_battery_is_charged(battery))
	    val->intval = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
	else
	    val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
	break;
	
    default:
	ret = -EINVAL;
    }
    return ret;
}

static enum power_supply_property portabook_battery_props[] = {
    POWER_SUPPLY_PROP_STATUS,
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
    POWER_SUPPLY_PROP_VOLTAGE_NOW,
    POWER_SUPPLY_PROP_CURRENT_NOW,
    POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
    POWER_SUPPLY_PROP_CHARGE_FULL,
    POWER_SUPPLY_PROP_CHARGE_NOW,
    POWER_SUPPLY_PROP_CAPACITY,
    POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

static struct portabook_battery *__portabook_battery_di;
static int
portabook_get_ac_property(struct power_supply *psy,
			  enum power_supply_property psp,
			  union power_supply_propval *val)
{
    struct portabook_battery *battery = __portabook_battery_di;
    if (!battery)
	return -ENODEV;

    portabook_battery_read_status(battery);
    
    switch (psp) {
    case POWER_SUPPLY_PROP_ONLINE:
	val->intval = battery->ac_adapter & 0x01;
	break;
    default:
	return -EINVAL;
    }
    return 0;
}

static enum power_supply_property portabook_ac_props[] = {
    POWER_SUPPLY_PROP_ONLINE,
};

static int
portabook_battery_probe(struct i2c_client *i2c_client,
			const struct i2c_device_id *id)
{
    struct power_supply_config psy_cfg = {};
    int retval = 0;
    struct portabook_battery *di;
    
    di = devm_kzalloc(&i2c_client->dev, sizeof(*di), GFP_KERNEL);
    if (!di) {
	retval = -ENOMEM;
	goto di_alloc_failed;
    }
    
    i2c_set_clientdata(i2c_client, di);
    
    mutex_init(&di->lock);
    di->i2c_client		= i2c_client;
    
    di->ac_desc.name		= "portabook_ac";
    di->ac_desc.type		= POWER_SUPPLY_TYPE_MAINS;
    di->ac_desc.properties	= portabook_ac_props;
    di->ac_desc.num_properties	= ARRAY_SIZE(portabook_ac_props);
    di->ac_desc.get_property	= portabook_get_ac_property;
    
    __portabook_battery_di	= di;
    
    di->ac = power_supply_register(&di->i2c_client->dev, &di->ac_desc, NULL);
    if (IS_ERR(di->ac)) {
	retval = PTR_ERR(di->ac);
	goto ac_failed;
    }
    
    di->bat_desc.name		= "portabook_batt";
    di->bat_desc.type		= POWER_SUPPLY_TYPE_BATTERY;
    di->bat_desc.properties	= portabook_battery_props;
    di->bat_desc.num_properties	= ARRAY_SIZE(portabook_battery_props);
    di->bat_desc.get_property	= portabook_battery_get_property;
    
    psy_cfg.drv_data		= di;
    
    di->bat = power_supply_register(&i2c_client->dev, &di->bat_desc, &psy_cfg);
    if (IS_ERR(di->bat)) {
	dev_err(&di->i2c_client->dev, "failed to register battery\n");
	retval = PTR_ERR(di->bat);
	goto batt_failed;
    }
    
    portabook_battery_read_status(di);
    return retval;

batt_failed:
    power_supply_unregister(di->ac);

 ac_failed:
di_alloc_failed:
    return retval;
}

static int
portabook_battery_remove(struct i2c_client *i2c_client)
{
    struct portabook_battery *di = i2c_get_clientdata(i2c_client);
    power_supply_unregister(di->ac);
    power_supply_unregister(di->bat);
    mutex_destroy(&di->lock);
    return 0;
}

static struct i2c_device_id portabook_battery_idtable[] = {
    { I2C_DEVICE_NAME, -1 },
    {},
};
MODULE_DEVICE_TABLE(i2c, portabook_battery_idtable);

static struct i2c_driver portabook_battery_driver = {
    .driver = {
	.name  = I2C_DEVICE_NAME,
	.owner = THIS_MODULE,
    },
    .id_table = portabook_battery_idtable,
    .probe    = portabook_battery_probe,
    .remove   = portabook_battery_remove,
};

static struct i2c_board_info portabook_ext_info = {
    .addr = 0x10,
    .type = I2C_DEVICE_NAME,
};

static struct i2c_client *battery_i2c_client;

int
portabook_battery_init(void)
{
    int s;
    int index;
    struct i2c_adapter *adapter;
    
    s = i2c_add_driver(&portabook_battery_driver);
    if (s < 0) return s;

    index = 0;
    while (1) {
	adapter = i2c_get_adapter(index);
	if (!adapter)
	    goto detect_failed;
	/* USE first founded adater */
	if (!strcmp(adapter->name, I2C_ADAPTER_NAME))
	    break;
	i2c_put_adapter(adapter);
	index++;
    }
    battery_i2c_client = i2c_new_device(adapter, &portabook_ext_info);
    if (!battery_i2c_client)
	goto detect_failed;
    i2c_put_adapter(adapter);
    return 0;

detect_failed:
    i2c_del_driver(&portabook_battery_driver);
    return -ENODEV;
}

void
portabook_battery_cleanup(void)
{
    if (battery_i2c_client)
	i2c_unregister_device(battery_i2c_client);
    battery_i2c_client = NULL;
    i2c_del_driver(&portabook_battery_driver);
}
