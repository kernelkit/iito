#include <libudev.h>

#include "iito.h"

struct in_udev {
	struct in_dev dev;
	const char *subsys;
	const char *sysname;

	struct udev *ud;
	struct udev_monitor *udmon;
	struct udev_device *uddev;
	struct ev_io ev;
};

static void in_udev_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	struct in_udev *iu = container_of(w, struct in_udev, ev);
	struct udev_device *uddev;
	const char *sysname;

	uddev = udev_monitor_receive_device(iu->udmon);
	if (!uddev)
		return;

	sysname = udev_device_get_sysname(uddev);
	if (!sysname || strcmp(sysname, iu->sysname)) {
		idev_dbg(&iu->dev, "Ignoring unrelated event from \"%s\"",
			sysname);
		return;
	}

	udev_device_unref(iu->uddev);
	iu->uddev = uddev;

	out_update(&iu->dev);
}

static int in_udev_sample(struct in_dev *dev, const char *prop, bool *state)
{
	struct in_udev *iu = container_of(dev, struct in_udev, dev);
	const char *val;

	if (!iu->uddev) {
		*state = false;
		return 0;
	}

	if (!prop) {
		val = udev_device_get_action(iu->uddev);

		if (val && !strcmp(val, "remove"))
			*state = false;
		else
			*state = true;

		return 0;
	}

	val = udev_device_get_sysattr_value(iu->uddev, prop);
	if (!val) {
		idev_dbg(&iu->dev, "Interpreting absence of property \"%s\" as false",
			prop);

		*state = false;
		return 0;
	}

	if (!strcmp(val, "0")) {
		*state = false;
		return 0;
	} else if (!strcmp(val, "1")) {
		*state = true;
		return 0;
	}

	idev_wrn(&iu->dev, "Interpreting available, but non-boolean, value of \"%s\" (%s), as true",
		prop, val);
	*state = true;
	return 0;
}

static int in_udev_probe(const char *name, json_t *data)
{
	struct in_udev *iu;
	int err;

	iu = calloc(1, sizeof(*iu));
	assert(iu);

	iu->dev.name = name;
	iu->dev.sample = in_udev_sample;

	err = json_unpack(data, "{s:s}", "subsystem", &iu->subsys);
	if (err) {
		idev_err(&iu->dev, "Required property \"subsystem\" is missing");
		goto err;
	}

	err = json_unpack(data, "{s:s}", "sysname", &iu->sysname);
	if (err)
		iu->sysname = name;

	iu->ud = udev_new();
	if (!iu->ud) {
		idev_err(&iu->dev, "Unable to attach to udev");
		goto err;
	}

	iu->uddev = udev_device_new_from_subsystem_sysname(iu->ud,
							   iu->subsys,
							   iu->sysname);
	if (!iu->uddev) {
		idev_dbg(&iu->dev, "Found no %s named \"%s\"",
			iu->subsys, iu->sysname);
	}

	iu->udmon = udev_monitor_new_from_netlink(iu->ud, "kernel");
	if (!iu->udmon) {
		idev_err(&iu->dev, "Unable to setup udev monitor");
		return -ENOSYS;
	}

	udev_monitor_filter_add_match_subsystem_devtype(iu->udmon, iu->subsys, NULL);

	ev_io_init(&iu->ev, in_udev_cb, udev_monitor_get_fd(iu->udmon), EV_READ);
	ev_io_start(ev_default_loop(0), &iu->ev);

	in_dev_add(&iu->dev);

	udev_monitor_enable_receiving(iu->udmon);
	return 0;

err:
	if (iu->udmon)
		udev_monitor_unref(iu->udmon);

	if (iu->uddev)
		udev_device_unref(iu->uddev);

	if (iu->ud)
		udev_unref(iu->ud);

	free(iu);
	return err;
}

const struct in_drv in_udev = {
	.name = "udev",
	.probe = in_udev_probe,
};
