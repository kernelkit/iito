#include "iito.h"


struct in_path {
	struct in_dev dev;
	struct ev_stat stat;
};

static void in_path_cb(struct ev_loop *loop, struct ev_stat *w, int revents)
{
	struct in_path *ip = container_of(w, struct in_path, stat);

	out_update(&ip->dev);
}

static int in_path_sample(struct in_dev *dev, const char *prop, bool *state)
{
	struct in_path *ip = container_of(dev, struct in_path, dev);

	if (!prop || !strcmp(prop, "present")) {
		*state = !!ip->stat.attr.st_nlink;
		return 0;
	}

	if (!strcmp(prop, "absent")) {
		*state = !ip->stat.attr.st_nlink;
		return 0;
	}

	dev_err(&ip->dev, "Unable to sample unknown property \"%s\"", prop);
	return -EINVAL;
}

static int in_path_probe(const char *name, json_t *data)
{
	struct in_path *ip;
	const char *path;
	int err;

	ip = calloc(1, sizeof(*ip));
	assert(ip);

	ip->dev.name = name;
	ip->dev.sample = in_path_sample;

	err = json_unpack(data, "{s:s}", "path", &path);
	if (err)
		path = name;

	in_dev_add(&ip->dev);

	ev_stat_init(&ip->stat, in_path_cb, path, 0.);
	ev_stat_start(ev_default_loop(0), &ip->stat);
	return 0;
}

const struct in_drv in_path = {
	.name = "path",
	.probe = in_path_probe,
};
