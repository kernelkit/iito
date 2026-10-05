#include "iito.h"
#include "ipc.h"

#include <grp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

struct ipc_method {
	const char *name;
	json_t *(*call)(json_t *params, const char **err);
};

static struct ev_loop *g_loop;
static struct ev_io g_ipc_ev;
static struct ev_timer g_locate_timer;

/*
 * Locate is driven through the configuration's "locate" path input,
 * the config's rules decide which LEDs blink, and how.
 */
static struct in_dev *ipc_locate_input(void)
{
	const char *prop = NULL;
	struct in_dev *idev;

	if (in_dev_find("locate", &idev, &prop) || strcmp(idev->type, "path"))
		return NULL;

	return idev;
}

static void ipc_locate_timeout(struct ev_loop *loop, struct ev_timer *w, int revents)
{
	struct in_dev *idev = ipc_locate_input();

	if (idev && in_path_set(idev, false))
		log_err("(ipc) Failed stopping locate");
}

/* "remaining" is only set while a locate timeout is running */
static json_t *ipc_locate_state(struct in_dev *idev)
{
	bool state = false;
	json_t *st;

	idev->sample(idev, NULL, &state);
	st = json_pack("{s:b}", "locate", state);
	if (ev_is_active(&g_locate_timer))
		json_object_set_new(st, "remaining", json_integer(
			(json_int_t)(ev_timer_remaining(g_loop, &g_locate_timer) + 0.999)));

	return st;
}

static json_t *ipc_status(json_t *params, const char **err)
{
	struct in_dev *idev = ipc_locate_input();
	json_t *st;

	st = idev ? ipc_locate_state(idev) : json_object();
	json_object_set_new(st, "inputs", in_status());
	json_object_set_new(st, "outputs", out_status());

	return st;
}

/*
 * Without params, or without "enable", only report the current state.
 * An optional "timeout", in seconds, stops locate by itself.
 */
static json_t *ipc_locate(json_t *params, const char **err)
{
	json_t *enable, *timeout;
	struct in_dev *idev;

	idev = ipc_locate_input();
	if (!idev) {
		*err = "no locate path input in the configuration";
		return NULL;
	}

	enable = json_object_get(params, "enable");
	timeout = json_object_get(params, "timeout");

	if (enable && !json_is_boolean(enable)) {
		*err = "\"enable\" must be true or false";
		return NULL;
	}
	if (timeout && (!json_is_integer(timeout) || json_integer_value(timeout) <= 0)) {
		*err = "\"timeout\" must be a positive number of seconds";
		return NULL;
	}

	if (enable) {
		ev_timer_stop(g_loop, &g_locate_timer);

		if (in_path_set(idev, json_is_true(enable))) {
			*err = "failed updating the locate input";
			return NULL;
		}

		if (json_is_true(enable) && timeout) {
			ev_timer_set(&g_locate_timer, (ev_tstamp)json_integer_value(timeout), 0.);
			ev_timer_start(g_loop, &g_locate_timer);
		}
	}

	return ipc_locate_state(idev);
}

static const struct ipc_method ipc_methods[] = {
	{ "status", ipc_status },
	{ "locate", ipc_locate },

	{ NULL }
};

static json_t *ipc_call(const char *req)
{
	const struct ipc_method *m;
	json_t *msg, *params = NULL, *result = NULL, *reply;
	const char *method, *err = "unknown method";
	json_error_t jerr;

	msg = json_loads(req, JSON_DISABLE_EOF_CHECK, &jerr);
	if (!msg)
		return json_pack("{s:s}", "error", "invalid request");

	if (json_unpack(msg, "{s:s, s?o}", "method", &method, "params", &params)) {
		json_decref(msg);
		return json_pack("{s:s}", "error", "request lacks a method");
	}

	for (m = ipc_methods; m->name; m++) {
		if (!strcmp(m->name, method)) {
			err = "failed";
			result = m->call(params, &err);
			break;
		}
	}

	if (result)
		reply = json_pack("{s:o}", "result", result);
	else
		reply = json_pack("{s:s}", "error", err);

	json_decref(msg);
	return reply;
}

static void ipc_send(int sd, json_t *reply)
{
	size_t len, off = 0;
	ssize_t n;
	char *buf;

	buf = json_dumps(reply, JSON_COMPACT);
	json_decref(reply);
	if (!buf)
		return;

	len = strlen(buf);
	buf[len++] = '\n';	/* json_dumps() leaves room for the NUL */

	while (off < len) {
		n = write(sd, &buf[off], len - off);
		if (n <= 0)
			break;
		off += n;
	}

	free(buf);
}

/*
 * Time left until the request deadline, as a receive timeout, or false
 * once the deadline has passed.
 */
static bool ipc_time_left(const struct timespec *deadline, struct timeval *tv)
{
	struct timespec now;
	long long us;

	clock_gettime(CLOCK_MONOTONIC, &now);
	us = (deadline->tv_sec - now.tv_sec) * 1000000LL +
		(deadline->tv_nsec - now.tv_nsec) / 1000;
	if (us <= 0)
		return false;

	tv->tv_sec  = us / 1000000;
	tv->tv_usec = us % 1000000;
	return true;
}

static void ipc_cb(struct ev_loop *loop, struct ev_io *w, int revents)
{
	/*
	 * Bound how long a client can hold up the event loop, in total,
	 * a per-read timeout alone lets a trickling client stall us.
	 */
	struct timeval tv = { .tv_sec = 1 };
	struct timespec deadline;
	char buf[IPC_MAX_MSG];
	size_t len = 0;
	ssize_t n;
	int sd;

	sd = accept4(w->fd, NULL, NULL, SOCK_CLOEXEC);
	if (sd < 0) {
		log_err("(ipc) Failed accepting connection: %s", strerror(errno));
		return;
	}

	setsockopt(sd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

	clock_gettime(CLOCK_MONOTONIC, &deadline);
	deadline.tv_sec += 1;

	while (len < sizeof(buf) - 1 && ipc_time_left(&deadline, &tv)) {
		setsockopt(sd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		n = read(sd, &buf[len], sizeof(buf) - 1 - len);
		if (n <= 0)
			break;

		len += n;
		if (memchr(buf, '\n', len))
			break;
	}
	buf[len] = 0;

	ipc_send(sd, ipc_call(buf));
	close(sd);
}

int ipc_init(struct ev_loop *loop, const char *path)
{
	struct sockaddr_un sun = { .sun_family = AF_UNIX };
	struct group *gr;
	int sd, err;

	if (strlen(path) >= sizeof(sun.sun_path)) {
		log_err("(ipc) Socket path too long: %s", path);
		return -ENAMETOOLONG;
	}
	strcpy(sun.sun_path, path);

	sd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (sd < 0)
		goto err;

	unlink(path);
	if (bind(sd, (struct sockaddr *)&sun, sizeof(sun)) || listen(sd, 8))
		goto err;

	/* Admin users, members of IPC_GROUP, may query and control us */
	gr = getgrnam(IPC_GROUP);
	if (!gr || chown(path, -1, gr->gr_gid)) {
		log_wrn("(ipc) Unable to grant group %s access to %s", IPC_GROUP, path);
		gr = NULL;
	}

	if (chmod(path, gr ? 0660 : 0600))
		goto err;

	g_loop = loop;
	ev_init(&g_locate_timer, ipc_locate_timeout);

	ev_io_init(&g_ipc_ev, ipc_cb, sd, EV_READ);
	ev_io_start(loop, &g_ipc_ev);
	return 0;

err:
	err = errno;
	log_err("(ipc) Failed setting up %s: %s", path, strerror(err));
	if (sd >= 0)
		close(sd);
	return -err;
}
