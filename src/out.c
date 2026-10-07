#include "iito.h"

static struct out_dev **g_out_devs;
static size_t g_out_devs_n;

void out_dump(void)
{
	struct out_dev **odev;
	struct out_rule *rule;
	size_t i;

	log_not("Output status:");
	for (i = 0, odev = g_out_devs; i < g_out_devs_n; i++, odev++) {
		rule = (*odev)->active_rule;
		if (rule)
			log_not("  (out) %s: active rule: \"%s%s%s%s\"",
				(*odev)->name,
				rule->invert ? "!" : "",
				rule->idev->name, rule->prop ? ":" : "",
				rule->prop ? : "");
		else
			log_not("  (out) %s: active rule: none", (*odev)->name);
	}
}

static json_t *out_rule_status(struct out_rule *rule)
{
	char cond[128];
	json_t *val;
	bool state;

	snprintf(cond, sizeof(cond), "%s%s%s%s", rule->invert ? "!" : "",
		 rule->idev->name, rule->prop ? ":" : "", rule->prop ? : "");

	if (rule->idev->sample(rule->idev, rule->prop, &state))
		val = json_null();
	else
		val = json_boolean(state ^ rule->invert);

	val = json_pack("{s:s, s:o, s:O}", "if", cond, "value", val,
			"then", rule->state);
	if (rule->alias)
		json_object_set_new(val, "alias", json_string(rule->alias));

	return val;
}

json_t *out_status(void)
{
	struct out_dev **odev;
	json_t *outs, *rules;
	size_t i, j;

	outs = json_array();
	for (i = 0, odev = g_out_devs; i < g_out_devs_n; i++, odev++) {
		rules = json_array();
		for (j = 0; j < (*odev)->n_rules; j++)
			json_array_append_new(rules, out_rule_status(&(*odev)->rules[j]));

		json_array_append_new(outs, json_pack("{s:s, s:s, s:b, s:o, s:o}",
			"name", (*odev)->name,
			"type", (*odev)->type,
			"present", (*odev)->present(*odev),
			"active", (*odev)->active_rule ?
				json_integer((*odev)->active_rule - (*odev)->rules) :
				json_null(),
			"rules", rules));
	}

	return outs;
}

void out_dev_add(struct out_dev *odev)
{
	struct out_dev **odevs;

	odevs = reallocarray(g_out_devs, g_out_devs_n + 1, sizeof(*g_out_devs));
	assert(odevs);

	odevs[g_out_devs_n++] = odev;
	g_out_devs = odevs;
}

static bool out_dev_uses(struct out_dev *odev, const struct in_dev *idev)
{
	struct out_rule *rule;
	size_t i;

	for (i = 0, rule = odev->rules; i < odev->n_rules; i++, rule++) {
		if (rule->idev == idev)
			return true;
	}

	return false;
}

static int out_update_one(struct out_dev *odev)
{
	struct out_rule *rule;
	bool state;
	size_t i;
	int err;

	odev_dbg(odev, "Update");

	for (i = 0, rule = odev->rules; i < odev->n_rules; i++, rule++) {
		err = rule->idev->sample(rule->idev, rule->prop, &state);
		if (err) {
			odev_err(odev, "Failed to sample \"%s%s%s\" (%d)",
				rule->idev->name, rule->prop ? ":" : "",
				rule->prop ? : "", err);
			return err;
		}

		if (state ^ rule->invert) {
			odev_dbg(odev, "Apply rule \"%s%s%s%s\"",
				rule->invert ? "!" : "",
				rule->idev->name, rule->prop ? ":" : "",
				rule->prop ? : "");

			err = odev->apply(odev, rule);
			if (err)
				odev_err(odev, "Failed to apply rule \"%s%s%s%s\" (%d)",
					rule->invert ? "!" : "",
					rule->idev->name, rule->prop ? ":" : "",
					rule->prop ? : "", err);
			else
				odev->active_rule = rule;

			return err;
		}
	}

	odev_dbg(odev, "Apply default rule");

	err = odev->apply(odev, NULL);
	if (err)
		odev_err(odev, "Failed to apply default rule (%d)", err);
	else
		odev->active_rule = NULL;

	return err;
}

int out_update(const struct in_dev *filter)
{
	struct out_dev **odev;
	size_t i;
	int err;

	if (filter)
		log_dbg("Update outputs related to \"%s\"", filter->name);
	else
		log_dbg("Update all outputs");

	for (i = 0, odev = g_out_devs; i < g_out_devs_n; i++, odev++) {
		if (filter && !out_dev_uses(*odev, filter))
			continue;

		err = out_update_one(*odev);
		if (err)
			return err;
	}

	return 0;
}


extern const struct out_drv out_led;
extern const struct out_drv out_led_group;

static const struct out_drv *out_drvs[] = {
	&out_led,
	&out_led_group,

	NULL
};

static int out_probe_rule(json_t *data, struct out_rule *rule)
{
	const char *devprop;
	int err;

	err = json_unpack(data, "{s:s, s:o}",
			  "if", &devprop,
			  "then", &rule->state);
	if (err)
		return err;

	if (json_is_string(rule->state)) {
		rule->alias = strdup(json_string_value(rule->state));
		assert(rule->alias);
	}

	err = alias_resolve(&rule->state);
	if (err)
		return err;

	if (devprop[0] == '!') {
		rule->invert = true;
		devprop++;
	}

	return in_dev_find(devprop, &rule->idev, &rule->prop);
}

static int out_probe_rules(json_t *dev, struct out_rule **rulesp, size_t *n_rulesp)
{
	struct out_rule *rules;
	size_t i, rarr_size;
	json_t *rarr;
	int err;

	err = json_unpack(dev, "{s:o}", "rules", &rarr);
	if (err)
		return err;

	if (!json_is_array(rarr))
		return -EINVAL;

	rarr_size = json_array_size(rarr);
	rules = calloc(rarr_size, sizeof(*rules));
	assert(rules);

	for (i = 0; i < rarr_size; i++) {
		err = out_probe_rule(json_array_get(rarr, i), &rules[i]);
		if (err) {
			free(rules);
			return err;
		}
	}

	*rulesp = rules;
	*n_rulesp = rarr_size;
	return 0;
}

static int out_probe_drv(const char *drvname, json_t *devs)
{
	const struct out_drv **drv;
	struct out_rule *rules;
	const char *name;
	size_t n_rules;
	json_t *data;
	int err;

	for (drv = out_drvs; *drv; drv++)
		if (!strcmp((*drv)->name, drvname))
			break;

	if (!(*drv)) {
		log_err("Unknown output type \"%s\"\n", drvname);
		return -ENODEV;
	}

	json_object_foreach(devs, name, data) {
		log_dbg("Probing %s output \"%s\"", drvname, name);

		err = out_probe_rules(data, &rules, &n_rules);
		if (err) {
			log_err("Failed parsing rules of %s output \"%s\" (%d)",
				drvname, name, err);
			return err;
		}

		err = (*drv)->probe(name, rules, n_rules, data);
		if (err) {
			log_err("Failed probing %s output \"%s\" (%d)",
				drvname, name, err);
			return err;
		}
	}

	return 0;
}

int out_probe(json_t *outs)
{
	const char *name;
	json_t *devs;
	int err;

	json_object_foreach(outs, name, devs) {
		err = out_probe_drv(name, devs);
		if (err)
			return err;
	}

	log_not("Successfully probed %zu outputs", g_out_devs_n);
	return 0;
}
