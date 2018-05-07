/*
 *  Copyright (C) 2018 Shrirang Bagul <shrirang.bagul at canonical.com>
 *
 *  Serial device bus slave driver for virtual HSUART ports on Intel Atom
 *  Baytrail SoC.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <linux/acpi.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/serdev.h>
#include <linux/serial.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>

#define DRV_NAME "hsuart-tty"
#define HSUART_NUM_MINORS	2

static DEFINE_IDR(hsuart_minors);
static DEFINE_MUTEX(table_lock);

struct hsuart_data {
	struct serdev_device *uart;	/* the uart connected to the chip */
	struct tty_driver *tty_drv;	/* this is the user space tty */
	struct device *dev;	/* returned by tty_port_register_device() */
	struct tty_port port;
	unsigned int minor;
	struct mutex mutex;
};

static struct hsuart_data *get_hsuart_by_minor(unsigned int minor)
{
	struct hsuart_data *data;

	mutex_lock(&table_lock);
	data = idr_find(&hsuart_minors, minor);
	if (data) {
		mutex_lock(&data->mutex);
		tty_port_get(&data->port);
		mutex_unlock(&data->mutex);
	}
	mutex_unlock(&table_lock);
	return data;
}

static int alloc_minor(struct hsuart_data *data)
{
	int minor;

	mutex_lock(&table_lock);
	minor = idr_alloc(&hsuart_minors, data, 0, HSUART_NUM_MINORS,
			GFP_KERNEL);
	mutex_unlock(&table_lock);
	if (minor >= 0)
		data->minor = minor;
	return minor;
}

static void release_minor(struct hsuart_data *data)
{
	int minor = data->minor;

	data->minor = 0;	/* Maybe should use an invalid value instead */
	mutex_lock(&table_lock);
	idr_remove(&hsuart_minors, minor);
	mutex_unlock(&table_lock);
}

static int huart_receive_buf(struct serdev_device *serdev, const unsigned char *rxdata,
				size_t count)
{
	struct hsuart_data *data =
		(struct hsuart_data *) serdev_device_get_drvdata(serdev);
	int ret;

	dev_dbg(&data->uart->dev, "push %d chars to tty port\n", count);
	ret = tty_insert_flip_string(&data->port, rxdata, count);	/* pass to user-space */
	if (ret != count)
		dev_dbg(&data->uart->dev, "lost %d characters\n", count - ret);
	tty_flip_buffer_push(&data->port);

	/* assume we have processed everything */
	return count;
}

static void hsuart_write_wakeup(struct serdev_device *serdev)
{
	struct hsuart_data *data =
		(struct hsuart_data *) serdev_device_get_drvdata(serdev);
	struct tty_struct *tty = tty_port_tty_get(&data->port);

	if (tty) {
		tty_wakeup(tty);
		tty_kref_put(tty);
	}
}

static const struct serdev_device_ops hsuart_serdev_client_ops = {
	.receive_buf = huart_receive_buf,
	.write_wakeup = hsuart_write_wakeup,
};

static int hsuart_tty_install(struct tty_driver *driver, struct tty_struct *tty)
{
	struct hsuart_data *data;
	int retval;

	data = get_hsuart_by_minor(tty->index);
	if (!data)
		return -ENODEV;

	retval = tty_standard_install(driver, tty);
	if (retval)
		goto error;

	tty->driver_data = data;
	return 0;
error:
	tty_port_put(&data->port);
	return retval;
}

static int hsuart_tty_open(struct tty_struct *tty, struct file *file)
{
	struct hsuart_data *data = tty->driver_data;

	return tty_port_open(&data->port, tty, file);
}

static void hsuart_tty_close(struct tty_struct *tty, struct file *file)
{
	struct hsuart_data *data = tty->driver_data;

	tty_port_close(&data->port, tty, file);
}

static int hsuart_tty_write(struct tty_struct *tty, const unsigned char *buf,
			int count)
{
	struct hsuart_data *data = tty->driver_data;

	return serdev_device_write_buf(data->uart, buf, count);
}

static int hsuart_tty_write_room(struct tty_struct *tty)
{
	struct hsuart_data *data = tty->driver_data;

	return serdev_device_write_room(data->uart);
}

static void hsuart_tty_flush_buffer(struct tty_struct *tty)
{
	struct hsuart_data *data = tty->driver_data;

	serdev_device_write_flush(data->uart);
}

static void hsuart_tty_wait_until_sent(struct tty_struct *tty, int timeout)
{
	struct hsuart_data *data = tty->driver_data;

	serdev_device_wait_until_sent(data->uart, timeout);
}

static void hsuart_tty_set_termios(struct tty_struct *tty,
			       struct ktermios *termios_old)
{
	struct hsuart_data *data = tty->driver_data;
	unsigned int speed = tty_get_baud_rate(tty);

	if (C_BAUD(tty) != B0)
		serdev_device_set_baudrate(data->uart, speed);

	serdev_device_set_flow_control(data->uart, C_CRTSCTS(tty));

	if (C_PARENB(tty)) {
		if (C_PARODD(tty))
			serdev_device_set_parity(data->uart, SERDEV_PARITY_ODD);
		else
			serdev_device_set_parity(data->uart, SERDEV_PARITY_EVEN);
	} else {
		serdev_device_set_parity(data->uart, SERDEV_PARITY_NONE);
	}
}

static int hsuart_tty_tiocmget(struct tty_struct *tty)
{
	struct hsuart_data *data = tty->driver_data;

	return serdev_device_get_tiocm(data->uart);
}

static int hsuart_tty_tiocmset(struct tty_struct *tty, unsigned int set,
			   unsigned int clear)
{
	struct hsuart_data *data = tty->driver_data;

	return serdev_device_set_tiocm(data->uart, set, clear);
}

static const struct tty_operations hsuart_tty_ops = {
	.install = hsuart_tty_install,
	.open = hsuart_tty_open,
	.close = hsuart_tty_close,
	.write = hsuart_tty_write,
	.write_room = hsuart_tty_write_room,
	.flush_buffer = hsuart_tty_flush_buffer,
	.wait_until_sent	= hsuart_tty_wait_until_sent,
	.set_termios = hsuart_tty_set_termios,
	.tiocmget = hsuart_tty_tiocmget,
	.tiocmset = hsuart_tty_tiocmset,
};

static const struct tty_port_operations hsuart_port_ops = {
};

static int hsuart_probe(struct serdev_device *serdev)
{
	struct hsuart_data *data;
	int minor;
	int err;

	data = devm_kzalloc(&serdev->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	serdev_device_set_drvdata(serdev, data);
	data->uart = serdev;

	serdev_device_set_client_ops(data->uart, &hsuart_serdev_client_ops);
	serdev_device_open(data->uart);

	serdev_device_set_baudrate(data->uart, 9600);
	serdev_device_set_flow_control(data->uart, false);

	/* allocate the tty driver */
	data->tty_drv = tty_alloc_driver(HSUART_NUM_MINORS, 0);
	if (!data->tty_drv) {
		dev_err(&serdev->dev,
				"failed to allcate tty driver\n");
		goto err_ttyfail;
	}

	/* initialize the tty driver */
	data->tty_drv->driver_name = DRV_NAME;
	data->tty_drv->name = "ttyHS";
	data->tty_drv->major = 0;
	data->tty_drv->minor_start = 0;
	data->tty_drv->type = TTY_DRIVER_TYPE_SERIAL;
	data->tty_drv->subtype = SERIAL_TYPE_NORMAL;
	data->tty_drv->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV;
	data->tty_drv->init_termios = tty_std_termios;
	data->tty_drv->init_termios.c_cflag = B9600 | CS8 |
		CREAD | HUPCL | CLOCAL;
	tty_set_operations(data->tty_drv, &hsuart_tty_ops);

	minor = alloc_minor(data);
	if (minor < 0) {
		if (minor == -ENOSPC) {
			dev_err(&serdev->dev,
				"no more free minor numbers\n");
			err = -ENODEV;
		}
		goto err_ttyfail;
	}
	mutex_init(&data->mutex);

	/* register the tty driver */
	err = tty_register_driver(data->tty_drv);
	if (err) {
		dev_err(&serdev->dev, "tty_register_driver failed(%d)\n", err);
		put_tty_driver(data->tty_drv);
		goto err_ttyfail;
	}

	tty_port_init(&data->port);
	data->port.ops = &hsuart_port_ops;
	data->dev = tty_port_register_device(&data->port, data->tty_drv,
			minor,	&serdev->dev);

	return 0;

err_ttyfail:
	serdev_device_close(data->uart);
	dev_err(&serdev->dev, "hsuart_probe failed error: %d\n", err);

	return err;
}

static void hsuart_remove(struct serdev_device *serdev)
{
	struct hsuart_data *data = serdev_device_get_drvdata(serdev);

	serdev_device_close(data->uart);
	tty_unregister_device(data->tty_drv, data->minor);
	release_minor(data);

	tty_unregister_driver(data->tty_drv);
}

static const struct acpi_device_id hsuart_acpi_match[] = {
	{"INT3511", 0},
	{"INT3512", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, hsuart_acpi_match);

static struct serdev_device_driver hsuart_tty_drv = {
	.driver		= {
		.name	= DRV_NAME,
		.acpi_match_table = ACPI_PTR(hsuart_acpi_match),
	},
	.probe	= hsuart_probe,
	.remove	= hsuart_remove,
};

module_serdev_device_driver(hsuart_tty_drv);

MODULE_AUTHOR("Shrirang Bagul <shrirang.bagul at canonical.com>");
MODULE_DESCRIPTION("Intel Atom (BayTrail) HS-UART serdev tty driver");
MODULE_LICENSE("GPL v2");
