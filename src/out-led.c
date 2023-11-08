#include "iito.h"

#define _PATH_SYSFS_LED "/sys/class/leds"

struct out_led {
	struct out_dev dev;

	FILE *trigger;
	FILE *brightness;
	int max_brightness;
};

struct out_led_rule {
	const char *trigger;
	int brightness;
};

static int out_led_apply(struct out_dev *odev, struct out_rule *rule)
{
	static const struct out_led_rule off = {
		.trigger = "none",
		.brightness = 0,
	};

	struct out_led *ol = container_of(odev, struct out_led, dev);
	const struct out_led_rule *olr;

	olr = rule ? rule->priv : &off;

	if ((fprintf(ol->trigger, "%s\n", olr->trigger) < 0) ||
	    fflush(ol->trigger)) {
		fprintf(stderr, "Unable to set trigger on \"%s\" to \"%s\"\n",
			odev->name, olr->trigger);
		return -EIO;
	}

	if ((fprintf(ol->brightness, "%d\n", olr->brightness) < 0) ||
	    fflush(ol->brightness)) {
		fprintf(stderr, "Unable to set brightness on \"%s\" to %d\n",
			odev->name, olr->brightness);
		return -EIO;
	}

	dev_dbg(odev, "Set trigger:%s brightness:%d", olr->trigger, olr->brightness);
	return 0;
}

static int out_led_parse_rules(struct out_led *ol,
			       struct out_rule *rules, size_t n_rules)
{
	struct out_led_rule *olr;
	struct out_rule *rule;
	bool brightness;
	size_t i;
	int err;

	for (i = 0, rule = rules; i < n_rules; i++, rule++) {
		olr = calloc(1, sizeof(*olr));
		assert(olr);

		err = json_unpack(rule->state, "{s:s}", "trigger", &olr->trigger);
		if (err)
			olr->trigger = "none";

		if (!json_unpack(rule->state, "{s:b}", "brightness", &brightness))
			olr->brightness = brightness ? ol->max_brightness : 0;
		else if (json_unpack(rule->state, "{s:i}", "brightness", &olr->brightness))
			olr->brightness = ol->max_brightness;

		rule->priv = olr;
	}

	return 0;
}

static int out_led_probe(const char *name, struct out_rule *rules,
			 size_t n_rules, json_t *data)
{
	char path[PATH_MAX];
	struct out_led *ol;
	FILE *max;
	int err;

	ol = calloc(1, sizeof(*ol));
	assert(ol);

	snprintf(path, sizeof(path), _PATH_SYSFS_LED "/%s/max_brightness", name);
	max = fopen(path, "r");
	if (!max || (fscanf(max, "%d", &ol->max_brightness) != 1)) {
		fprintf(stderr, "Unable to determine brightness range for LED \"%s\": %m\n", name);
		err = -ENODEV;
		goto err;
	}
	fclose(max);

	snprintf(path, sizeof(path), _PATH_SYSFS_LED "/%s/trigger", name);
	ol->trigger = fopen(path, "w");
	if (!ol->trigger) {
		fprintf(stderr, "Unable to open trigger for LED \"%s\": %m\n", name);
		err = -ENODEV;
		goto err;
	}

	snprintf(path, sizeof(path), _PATH_SYSFS_LED "/%s/brightness", name);
	ol->brightness = fopen(path, "w");
	if (!ol->brightness) {
		fprintf(stderr, "Unable to open brightness for LED \"%s\": %m\n", name);
		err = -ENODEV;
		goto err;
	}

	err = out_led_parse_rules(ol, rules, n_rules);
	if (err)
		goto err;

	ol->dev.name = name;
	ol->dev.rules = rules;
	ol->dev.n_rules = n_rules;
	ol->dev.apply = out_led_apply;
	out_dev_add(&ol->dev);
	return 0;

err:
	if (ol->brightness)
		fclose(ol->brightness);
	if (ol->trigger)
		fclose(ol->trigger);

	free(ol);
	return err;
}

const struct out_drv out_led = {
	.name = "led",
	.probe = out_led_probe,
};
