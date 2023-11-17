#include <stdarg.h>

#include "iito.h"

#define uddev_err(_udev, _fmt, ...)					\
	log_err("(udev) %s(%s): " _fmt,					\
		(_udev)->sysname, (_udev)->subsys, ##__VA_ARGS__)

#define uddev_dbg(_udev, _fmt, ...)					\
	log_dbg("(udev) %s(%s): " _fmt,					\
		(_udev)->sysname, (_udev)->subsys, ##__VA_ARGS__)

bool uddev_present(struct uddev *uddev)
{
	const char *action;

	if (!uddev->dev)
		return false;

	action = udev_device_get_action(uddev->dev);
	if (!action)
		return true;

	return !!strcmp(action, "remove");
}

int uddev_set_sysfs(struct uddev *uddev, const char *attr, const char *fmt, ...)
{
	char val[0x100];
	va_list ap;
	int err;

	if (!uddev_present(uddev))
		return -EINVAL;

	va_start(ap, fmt);
	vsnprintf(val, sizeof(val), fmt, ap);
	va_end(ap);

	err = udev_device_set_sysattr_value(uddev->dev, attr, val);
	if (err) {
		uddev_err(uddev, "Unable to set \"%s\" to \"%s\" (%d)", attr, val, err);
	}
	return err;
}

static void uddev_ev_cb(struct ev_loop *loop, struct ev_io *ev, int revents)
{
	struct uddev *uddev = container_of(ev, struct uddev, ev);
	struct udev_device *dev;
	const char *sysname;

	dev = udev_monitor_receive_device(uddev->mon);
	if (!dev)
		return;

	sysname = udev_device_get_sysname(dev);
	if (!sysname || strcmp(sysname, uddev->sysname)) {
		uddev_dbg(uddev, "Ignoring unrelated event from \"%s\"", sysname);
		udev_device_unref(dev);
		return;
	}

	uddev->cb(uddev, dev);

	if (uddev->dev)
		udev_device_unref(uddev->dev);

	uddev->dev = dev;
}

int uddev_start(struct uddev *uddev)
{
	ev_io_start(ev_default_loop(0), &uddev->ev);

	if (udev_monitor_enable_receiving(uddev->mon)) {
		uddev_err(uddev, "Unable to start monitor");
		return -EINVAL;
	}

	return 0;
}

int uddev_init(struct uddev *uddev)
{
	int err;

	uddev->ud = udev_new();
	if (!uddev->ud) {
		uddev_err(uddev, "Unable to create udev context");
		err = -ENOSYS;
		goto err;
	}

	uddev->dev = udev_device_new_from_subsystem_sysname(uddev->ud,
							    uddev->subsys,
							    uddev->sysname);
	if (!uddev->dev)
		uddev_dbg(uddev, "Not available");

	uddev->mon = udev_monitor_new_from_netlink(uddev->ud, "kernel");
	if (!uddev->mon) {
		uddev_err(uddev, "Unable to setup udev monitor");
		err = -ENOSYS;
		goto err;
	}

	if (udev_monitor_filter_add_match_subsystem_devtype(uddev->mon,
							    uddev->subsys, NULL)) {
		uddev_err(uddev, "Unable to apply subsystem filter");
		err = -ENOSYS;
		goto err;
	}

	ev_io_init(&uddev->ev, uddev_ev_cb, udev_monitor_get_fd(uddev->mon), EV_READ);
	return 0;

err:
	if (uddev->mon)
		udev_monitor_unref(uddev->mon);

	if (uddev->dev)
		udev_device_unref(uddev->dev);

	if (uddev->ud)
		udev_unref(uddev->ud);

	return err;
}
