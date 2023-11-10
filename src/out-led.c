#include "iito.h"

#define _PATH_SYSFS_LED "/sys/class/leds"

struct out_led {
	struct out_dev odev;
	struct uddev uddev;

	int max_brightness;
};

static int out_led_apply(struct out_dev *odev, struct out_rule *rule)
{
	struct out_led *ol = container_of(odev, struct out_led, odev);
	const char *key, *trigger = "none";
	int brightness = 0;
	bool set_max;
	json_t *val;

	if (!uddev_present(&ol->uddev)) {
		odev_dbg(&ol->odev, "Absent, not applying");
		return 0;
	}

	if (rule) {
		if (json_unpack(rule->state, "{s:s}", "trigger", &trigger))
			trigger = "none";

		/* Special handling of brightness: may be either bool
		 * or int. Interpret a bool as either 0 (false) or the
		 * LED's max_brightness (true) */
		if (!json_unpack(rule->state, "{s:b}", "brightness", &set_max))
			brightness = set_max ? ol->max_brightness : 0;
		else if (json_unpack(rule->state, "{s:i}", "brightness", &brightness))
			brightness = ol->max_brightness;
	}

	/* Always set trigger and brightness, which have default fallback values */

	if (uddev_set_sysfs(&ol->uddev, "trigger", trigger))
		return -EIO;
	if (uddev_set_sysfs(&ol->uddev, "brightness", "%d", brightness))
		return -EIO;

	odev_dbg(&ol->odev, "Set trigger:%s brightness:%d", trigger, brightness);

	if (!rule)
		return 0;

	/* Then apply any trigger specific attributes, if specified */

	json_object_foreach(rule->state, key, val) {
		if (!strcmp(key, "trigger") || !strcmp(key, "brightness"))
			continue;

		switch (json_typeof(val)) {
		case JSON_STRING:
			if (uddev_set_sysfs(&ol->uddev, key, json_string_value(val)))
				return -EIO;
			break;
		case JSON_INTEGER:
			if (uddev_set_sysfs(&ol->uddev, key, "%d", json_integer_value(val)))
				return -EIO;
			break;
		case JSON_TRUE:
			if (uddev_set_sysfs(&ol->uddev, key, "1"))
				return -EIO;
			break;
		case JSON_FALSE:
			if (uddev_set_sysfs(&ol->uddev, key, "0"))
				return -EIO;
			break;
		case JSON_NULL:
			if (uddev_set_sysfs(&ol->uddev, key, ""))
				return -EIO;
			break;
		default:
			odev_err(&ol->odev, "Unable to handle attribute \"%s\"", key);
			return -EINVAL;
		}
	}

	return 0;
}

static void out_led_set_max(struct out_led *ol)
{
	const char *maxstr;

	maxstr = udev_device_get_sysattr_value(ol->uddev.dev, "max_brightness");
	if (!maxstr)
		goto fallback;

	errno = 0;
	ol->max_brightness = strtol(maxstr, NULL, 0);
	if (errno)
		goto fallback;

	return;

fallback:
	odev_err(&ol->odev, "Unable to read \"max_brightness\", falling back to 1");
	ol->max_brightness = 1;
}

static void out_led_uddev_cb(struct uddev *uddev, struct udev_device *dev)
{
	struct out_led *ol = container_of(uddev, struct out_led, uddev);
	const char *action;

	action = udev_device_get_action(dev);
	if (!action || strcmp(action, "add"))
		return;

	udev_device_unref(uddev->dev);
	uddev->dev = udev_device_ref(dev);

	out_led_set_max(ol);

	odev_inf(&ol->odev, "Hotplugged, applying active rule");

	if (out_led_apply(&ol->odev, ol->odev.active_rule))
		odev_err(&ol->odev, "Unable to apply active rule after hotplug");
}

static int out_led_probe(const char *name, struct out_rule *rules,
			 size_t n_rules, json_t *data)
{
	struct out_led *ol;
	int err;

	ol = calloc(1, sizeof(*ol));
	if (!ol)
		return -ENOMEM;

	*ol = (struct out_led) {
		.odev = {
			.name = name,
			.apply = out_led_apply,
			.rules = rules,
			.n_rules = n_rules,
		},
		.uddev = {
			.subsys = "leds",
			.sysname = name,
			.cb = out_led_uddev_cb,
			.priv = ol,
		},
	};

	err = uddev_init(&ol->uddev);
	if (err)
		goto err;

	if (uddev_present(&ol->uddev))
		out_led_set_max(ol);

	out_dev_add(&ol->odev);
	uddev_start(&ol->uddev);
	return 0;

err:
	free(ol);
	return err;
}

const struct out_drv out_led = {
	.name = "led",
	.probe = out_led_probe,
};
