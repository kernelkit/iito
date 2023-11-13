#ifndef _IITO_H
#define _IITO_H

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <ev.h>
#include <jansson.h>
#include <libudev.h>


/* util */

#define offsetof(_type, _member)				\
    ((size_t)&(((_type *)0)->_member))

#define container_of(_ptr, _type, _member) ({			\
	const typeof(((_type *)0)->_member) * __mptr = (_ptr);	\
	(_type *)((char *)__mptr - offsetof(_type, _member)); })

#define log_cri(_fmt, ...) syslog(LOG_CRIT,    "C " _fmt, ##__VA_ARGS__)
#define log_err(_fmt, ...) syslog(LOG_ERR,     "E " _fmt, ##__VA_ARGS__)
#define log_wrn(_fmt, ...) syslog(LOG_WARNING, "W " _fmt, ##__VA_ARGS__)
#define log_not(_fmt, ...) syslog(LOG_NOTICE,  "N " _fmt, ##__VA_ARGS__)
#define log_inf(_fmt, ...) syslog(LOG_INFO,    "I " _fmt, ##__VA_ARGS__)
#define log_dbg(_fmt, ...) syslog(LOG_DEBUG,   "D " _fmt, ##__VA_ARGS__)

#define idev_err(_dev, _fmt, ...) log_err("(in) %s: " _fmt, (_dev)->name, ##__VA_ARGS__)
#define idev_wrn(_dev, _fmt, ...) log_wrn("(in) %s: " _fmt, (_dev)->name, ##__VA_ARGS__)
#define idev_inf(_dev, _fmt, ...) log_inf("(in) %s: " _fmt, (_dev)->name, ##__VA_ARGS__)
#define idev_dbg(_dev, _fmt, ...) log_dbg("(in) %s: " _fmt, (_dev)->name, ##__VA_ARGS__)

#define odev_err(_dev, _fmt, ...) log_err("(out) %s: " _fmt, (_dev)->name, ##__VA_ARGS__)
#define odev_wrn(_dev, _fmt, ...) log_wrn("(out) %s: " _fmt, (_dev)->name, ##__VA_ARGS__)
#define odev_inf(_dev, _fmt, ...) log_inf("(out) %s: " _fmt, (_dev)->name, ##__VA_ARGS__)
#define odev_dbg(_dev, _fmt, ...) log_dbg("(out) %s: " _fmt, (_dev)->name, ##__VA_ARGS__)


/* uddev */

struct uddev;

typedef void (*uddev_cb_t)(struct uddev *uddev, struct udev_device *dev);

struct uddev {
	const char *subsys;
	const char *sysname;
	uddev_cb_t cb;

	struct ev_io ev;
	struct udev *ud;
	struct udev_monitor *mon;
	struct udev_device *dev;
};

bool uddev_present(struct uddev *uddev);
int uddev_set_sysfs(struct uddev *uddev, const char *attr, const char *fmt, ...);

int uddev_start(struct uddev *uddev);
int uddev_init(struct uddev *uddev);


/* input */

struct in_dev {
	const char *name;

	int (*sample)(struct in_dev *dev, const char *prop, bool *state);
};

void in_dev_add(struct in_dev *idev);

struct in_drv {
	const char *name;
	int (*probe)(const char *name, json_t *data);
};

int in_probe(json_t *ins);

int in_dev_find(const char *nameprop, struct in_dev **idevp, const char **propp);


/* output */

struct out_rule {
	bool invert;
	struct in_dev *idev;
	const char *prop;
	json_t *state;
	void *priv;
};

struct out_dev {
	const char *name;
	struct out_rule *rules;
	size_t n_rules;

	struct out_rule *active_rule;
	int (*apply)(struct out_dev *odev, struct out_rule *rule);
};

void out_dump(void);

int out_update(const struct in_dev *filter);

void out_dev_add(struct out_dev *odev);

struct out_drv {
	const char *name;
	int (*probe)(const char *name, struct out_rule *rules, size_t n_rules,
		     json_t *data);
};

int out_probe(json_t *outs);

#endif	/* _IITO_H */
