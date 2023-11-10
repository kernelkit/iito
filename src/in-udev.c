#include "iito.h"

struct in_udev {
	struct in_dev idev;
	struct uddev uddev;
};

static void in_udev_uddev_cb(struct uddev *uddev, struct udev_device *dev)
{
	struct in_udev *iu = container_of(uddev, struct in_udev, uddev);

	udev_device_unref(uddev->dev);
	uddev->dev = udev_device_ref(dev);

	out_update(&iu->idev);
}

static int in_udev_sample(struct in_dev *idev, const char *prop, bool *state)
{
	struct in_udev *iu = container_of(idev, struct in_udev, idev);
	const char *val;

	if (!uddev_present(&iu->uddev)) {
		*state = false;
		return 0;
	}

	if (!prop) {
		*state = true;
		return 0;
	}

	val = udev_device_get_sysattr_value(iu->uddev.dev, prop);
	if (!val) {
		idev_dbg(&iu->idev, "Interpreting absence of property \"%s\" as false",
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

	idev_wrn(&iu->idev, "Interpreting available, but non-boolean, value of \"%s\" (%s), as true",
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

	*iu = (struct in_udev) {
		.idev = {
			.name = name,
			.sample = in_udev_sample,
		},
		.uddev = {
			.cb = in_udev_uddev_cb,
		},
	};

	err = json_unpack(data, "{s:s}", "subsystem", &iu->uddev.subsys);
	if (err) {
		idev_err(&iu->idev, "Required property \"subsystem\" is missing");
		goto err;
	}

	err = json_unpack(data, "{s:s}", "sysname", &iu->uddev.sysname);
	if (err)
		iu->uddev.sysname = name;

	err = uddev_init(&iu->uddev);
	if (err) {
		idev_err(&iu->idev, "Unable to attach to udev");
		goto err;
	}

	in_dev_add(&iu->idev);
	uddev_start(&iu->uddev);
	return 0;

err:
	free(iu);
	return err;
}

const struct in_drv in_udev = {
	.name = "udev",
	.probe = in_udev_probe,
};
