#include "iito.h"

static int in_true_sample(struct in_dev *dev, const char *prop, bool *state)
{
	*state = true;
	return 0;
}

static void in_true_probe(void)
{
	struct in_dev *tru;

	tru = calloc(1, sizeof(*tru));
	assert(tru);

	tru->name = "true";
	tru->sample = in_true_sample;
	in_dev_add(tru);
}

static struct in_dev **g_in_devs;
static size_t g_in_devs_n;

int in_dev_find(const char *nameprop, struct in_dev **idevp, const char **propp)
{
	struct in_dev **idev;
	const char *sep;
	size_t i;

	sep = index(nameprop, ':');
	if (sep)
		*propp = sep + 1;
	else
		sep = index(nameprop, '\0');

	for (i = 0, idev = g_in_devs; i < g_in_devs_n; i++, idev++) {
		if (!strncmp(nameprop, (*idev)->name, sep - nameprop)) {
			*idevp = *idev;
			return 0;
		}
	}

	log_dbg("Found no input device matching \"%s\"", nameprop);
	return -ENODEV;
}

void in_dev_add(struct in_dev *idev)
{
	struct in_dev **idevs;

	idevs = reallocarray(g_in_devs, g_in_devs_n + 1, sizeof(*g_in_devs));
	assert(idevs);

	idevs[g_in_devs_n++] = idev;
	g_in_devs = idevs;
}

extern const struct in_drv in_path;
extern const struct in_drv in_udev;

static const struct in_drv *in_drvs[] = {
	&in_path,
	&in_udev,

	NULL
};

static int in_probe_drv(const char *drvname, json_t *devs)
{
	const struct in_drv **drv;
	const char *name;
	json_t *data;
	int err;

	for (drv = in_drvs; *drv; drv++)
		if (!strcmp((*drv)->name, drvname))
			break;

	if (!(*drv)) {
		log_err("Unknown input type \"%s\"\n", drvname);
		return -ENODEV;
	}

	json_object_foreach(devs, name, data) {
		log_dbg("Probing %s input \"%s\"", drvname, name);

		err = (*drv)->probe(name, data);
		if (err) {
			log_err("Failed probing %s input \"%s\" (%d)",
				drvname, name, err);
			return err;
		}
	}

	return 0;
}

int in_probe(json_t *ins)
{
	const char *name;
	json_t *devs;
	int err;

	in_true_probe();

	json_object_foreach(ins, name, devs) {
		err = in_probe_drv(name, devs);
		if (err)
			return err;
	}

	log_not("Successfully probed %zu inputs", g_in_devs_n);
	return 0;
}
