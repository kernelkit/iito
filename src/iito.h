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

#define dev_err(_dev, _fmt, ...) log_err("%s: " _fmt, (_dev)->name, ##__VA_ARGS__)
#define dev_wrn(_dev, _fmt, ...) log_wrn("%s: " _fmt, (_dev)->name, ##__VA_ARGS__)
#define dev_dbg(_dev, _fmt, ...) log_dbg("%s: " _fmt, (_dev)->name, ##__VA_ARGS__)
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

	int (*apply)(struct out_dev *odev, struct out_rule *rule);
};

int out_update(const struct in_dev *filter);

void out_dev_add(struct out_dev *odev);

struct out_drv {
	const char *name;
	int (*probe)(const char *name, struct out_rule *rules, size_t n_rules,
		     json_t *data);
};

int out_probe(json_t *outs);

#endif	/* _IITO_H */
