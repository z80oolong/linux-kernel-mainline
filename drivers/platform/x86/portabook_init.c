/*
 * portabook_init.c - Portabook extra module
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

#include <linux/module.h>
#include <linux/kernel.h>

MODULE_DESCRIPTION("Portabook extra Module");
MODULE_AUTHOR("MURAMATSU Atsushi <amura@tomato.sakura.ne.jp>");
MODULE_LICENSE("GPL");

#ifdef CONFIG_PORTABOOK_EXT_BACKLIGHT
extern int portabook_backlight_init(void);
extern void portabook_backlight_cleanup(void);
#endif
#ifdef CONFIG_PORTABOOK_EXT_BATTERY
extern int portabook_battery_init(void);
extern void portabook_battery_cleanup(void);
#endif

static int
portabook_ext_init_module(void)
{
    int error = 0;

    printk("XMC10: portabook_ext is loaded!\n");
#ifdef CONFIG_PORTABOOK_EXT_BACKLIGHT
    error = portabook_backlight_init();
    if (error)
	return error;
#endif
#ifdef CONFIG_PORTABOOK_EXT_BATTERY
    error = portabook_battery_init();
    if (error)
	return error;
#endif
    return 0;
}

static void portabook_ext_cleanup_module(void)
{
#ifdef CONFIG_PORTABOOK_EXT_BATTERY
    portabook_battery_cleanup();
#endif
#ifdef CONFIG_PORTABOOK_EXT_BACKLIGHT
    portabook_backlight_cleanup();
#endif
    printk("XMC10: portabook_ext is unloaded!\n");
}

module_init(portabook_ext_init_module);
module_exit(portabook_ext_cleanup_module);
