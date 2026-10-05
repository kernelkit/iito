#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <jansson.h>

#include "ipc.h"

static const char *g_sock = IPC_SOCKET;
static int g_json;

static void usage(void)
{
	fprintf(stderr,
		"iitoctl - Query and control iitod\n"
		"\n"
		"Usage:\n"
		"  iitoctl [options] [COMMAND]\n"
		"\n"
		"Commands:\n"
		"  status             Show inputs, and each output's rules (default)\n"
		"  locate [on [SEC] | off]\n"
		"                     Show, start, or stop locate, the LEDs blink as\n"
		"                     configured, with on SEC, locate stops by itself\n"
		"                     after SEC seconds\n"
		"\n"
		"Options:\n"
		"  -h, --help         Print usage message and exit\n"
		"  -j, --json         Print the reply from iitod as JSON\n"
		"  -s, --socket=PATH  Connect to iitod on PATH instead of %s\n"
		"  -v, --version      Print version information\n",
		IPC_SOCKET);
}

static json_t *call(const char *method, json_t *params)
{
	struct sockaddr_un sun = { .sun_family = AF_UNIX };
	json_t *req, *reply, *result;
	char buf[IPC_MAX_MSG], *msg;
	json_error_t jerr;
	size_t len = 0;
	ssize_t n;
	int sd;

	if (strlen(g_sock) >= sizeof(sun.sun_path)) {
		fprintf(stderr, "iitoctl: socket path too long: %s\n", g_sock);
		return NULL;
	}
	strcpy(sun.sun_path, g_sock);

	sd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (sd < 0 || connect(sd, (struct sockaddr *)&sun, sizeof(sun))) {
		fprintf(stderr, "iitoctl: cannot connect to %s: %s\n", g_sock, strerror(errno));
		goto fail;
	}

	req = json_pack("{s:s}", "method", method);
	if (params)
		json_object_set_new(req, "params", params);
	msg = json_dumps(req, JSON_COMPACT);
	json_decref(req);

	if (!msg || dprintf(sd, "%s\n", msg) < 0) {
		fprintf(stderr, "iitoctl: failed sending request: %s\n", strerror(errno));
		free(msg);
		goto fail;
	}
	free(msg);

	/* The reply ends when iitod closes the connection */
	while (len < sizeof(buf) - 1) {
		n = read(sd, &buf[len], sizeof(buf) - 1 - len);
		if (n <= 0)
			break;
		len += n;
	}
	buf[len] = 0;
	close(sd);

	reply = json_loads(buf, 0, &jerr);
	if (!reply) {
		fprintf(stderr, "iitoctl: invalid reply from iitod: %s\n", jerr.text);
		return NULL;
	}

	result = json_incref(json_object_get(reply, "result"));
	if (!result)
		fprintf(stderr, "iitoctl: %s\n",
			json_string_value(json_object_get(reply, "error")) ? : "unknown error");

	json_decref(reply);
	return result;

fail:
	if (sd >= 0)
		close(sd);
	return NULL;
}

static const char *value_str(json_t *val)
{
	if (json_is_true(val))
		return "true";
	if (json_is_false(val))
		return "false";

	return "error";
}

/* Print a rule's state as its alias, or as key=value pairs */
static void print_state(json_t *rule)
{
	const char *alias, *key;
	json_t *val;

	alias = json_string_value(json_object_get(rule, "alias"));
	if (alias) {
		printf("%s", alias);
		return;
	}

	json_object_foreach(json_object_get(rule, "then"), key, val) {
		if (json_is_string(val))
			printf("%s=%s ", key, json_string_value(val));
		else if (json_is_integer(val))
			printf("%s=%" JSON_INTEGER_FORMAT " ", key, json_integer_value(val));
		else
			printf("%s=%s ", key, json_is_true(val) ? "true" : "false");
	}
}

/* No "locate" when the config has no locate input */
static void print_locate(json_t *st)
{
	json_t *locate = json_object_get(st, "locate");
	json_t *left = json_object_get(st, "remaining");

	if (!locate)
		printf("Locate: not configured\n");
	else if (!json_is_true(locate))
		printf("Locate: off\n");
	else if (json_is_integer(left))
		printf("Locate: on, %" JSON_INTEGER_FORMAT " seconds left\n", json_integer_value(left));
	else
		printf("Locate: on\n");
}

static int width(json_t *arr, const char *key, int min)
{
	json_t *obj;
	size_t i;
	int len;

	json_array_foreach(arr, i, obj) {
		len = strlen(json_string_value(json_object_get(obj, key)) ? : "");
		if (len > min)
			min = len;
	}

	return min;
}

static int show_status(void)
{
	json_t *st, *ins, *outs, *in, *out, *rules, *rule, *active;
	int w;
	size_t i, j;

	st = call("status", NULL);
	if (!st)
		return 1;

	if (g_json)
		goto json;

	print_locate(st);

	ins = json_object_get(st, "inputs");
	w = width(ins, "name", 5);
	printf("\n%-*s  %-7s  %s\n", w, "INPUT", "TYPE", "VALUE");
	json_array_foreach(ins, i, in)
		printf("%-*s  %-7s  %s\n", w,
		       json_string_value(json_object_get(in, "name")),
		       json_string_value(json_object_get(in, "type")),
		       value_str(json_object_get(in, "value")));

	outs = json_object_get(st, "outputs");
	json_array_foreach(outs, i, out) {
		printf("\n%s (%s%s)\n",
		       json_string_value(json_object_get(out, "name")),
		       json_string_value(json_object_get(out, "type")),
		       json_is_true(json_object_get(out, "present")) ? "" : ", absent");

		rules = json_object_get(out, "rules");
		active = json_object_get(out, "active");
		w = width(rules, "if", 4);
		printf("    %-*s  %-5s  %s\n", w, "IF", "VALUE", "THEN");
		json_array_foreach(rules, j, rule) {
			printf("  %c %-*s  %-5s  ",
			       json_is_integer(active) && (size_t)json_integer_value(active) == j ? '*' : ' ',
			       w, json_string_value(json_object_get(rule, "if")),
			       value_str(json_object_get(rule, "value")));
			print_state(rule);
			putchar('\n');
		}

		if (!json_is_integer(active))
			printf("  * default, no rule matches\n");
	}

	json_decref(st);
	return 0;

json:
	json_dumpf(st, stdout, JSON_INDENT(2));
	putchar('\n');
	json_decref(st);
	return 0;
}

static int locate(const char *arg, const char *sec)
{
	json_t *params = NULL, *res;
	char *end;
	long tmo;

	if (arg) {
		if (!strcmp(arg, "on"))
			params = json_pack("{s:b}", "enable", 1);
		else if (!strcmp(arg, "off") && !sec)
			params = json_pack("{s:b}", "enable", 0);
		else {
			fprintf(stderr, "iitoctl: locate takes on [SEC] or off\n");
			return 1;
		}
	}

	if (sec) {
		tmo = strtol(sec, &end, 10);
		if (*end || tmo <= 0) {
			fprintf(stderr, "iitoctl: invalid number of seconds \"%s\"\n", sec);
			json_decref(params);
			return 1;
		}
		json_object_set_new(params, "timeout", json_integer(tmo));
	}

	res = call("locate", params);
	if (!res)
		return 1;

	if (g_json) {
		json_dumpf(res, stdout, JSON_INDENT(2));
		putchar('\n');
	} else {
		print_locate(res);
	}

	json_decref(res);
	return 0;
}

int main(int argc, char **argv)
{
	static const struct option lopts[] = {
		{ "help",    no_argument,       0, 'h' },
		{ "json",    no_argument,       0, 'j' },
		{ "socket",  required_argument, 0, 's' },
		{ "version", no_argument,       0, 'v' },
		{ NULL }
	};
	const char *cmd = "status";
	int opt;

	while ((opt = getopt_long(argc, argv, "hjs:v", lopts, NULL)) > 0) {
		switch (opt) {
		case 'h':
			usage();
			return 0;
		case 'j':
			g_json = 1;
			break;
		case 's':
			g_sock = optarg;
			break;
		case 'v':
			puts(PACKAGE_STRING);
			return 0;
		default:
			usage();
			return 1;
		}
	}

	if (optind < argc)
		cmd = argv[optind++];

	if (!strcmp(cmd, "status"))
		return show_status();
	if (!strcmp(cmd, "locate"))
		return locate(optind < argc ? argv[optind] : NULL,
			      optind + 1 < argc ? argv[optind + 1] : NULL);

	fprintf(stderr, "iitoctl: unknown command \"%s\"\n\n", cmd);
	usage();
	return 1;
}
