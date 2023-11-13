#include <getopt.h>

#define SYSLOG_NAMES
#include "iito.h"

static int logmask = LOG_UPTO(LOG_NOTICE);

static void sigusr1_cb(struct ev_loop *loop, struct ev_signal *sig, int revents)
{
	int current = setlogmask(0);

	if (current == logmask)
		setlogmask(LOG_UPTO(LOG_DEBUG));
	else
		setlogmask(logmask);
}

static void sigusr2_cb(struct ev_loop *loop, struct ev_signal *sig, int revents)
{
	out_dump();
}

#define DEFAULT_CONFIG SYSCONFDIR "/iitod.json"

static void usage()
{
	fprintf(stderr,
		"iitod - If Input, Then Output (Daemon)\n"
		"\n"
		"Usage:\n"
		"  iitod [options]\n"
		"\n"
		" Monitor input sources, and reflect their state on output sinks.\n"
		"\n"
		"Options:\n"
		"  -d, --debug         In addition to syslog, also log to stderr\n"
		"  -f, --config=FILE   Use configuration from FILE instead of %s\n"
		"  -h, --help          Print usage message and exit\n"
		"  -l, --loglevel=LVL  Log level: none, err, warn, notice*, info, debug\n"
		"  -v, --version       Print version information\n",
		DEFAULT_CONFIG);
}

static const char *sopts = "df:hl:v";
static struct option lopts[] = {
	{ "debug",    no_argument,       0, 'd' },
	{ "config",   required_argument, 0, 'f' },
	{ "help",     no_argument,       0, 'h' },
	{ "loglevel", required_argument, 0, 'l' },
	{ "version",  no_argument,       0, 'v' },

	{ NULL }
};

int logmask_from_str(const char *str)
{
	const CODE *code;

	for (code = prioritynames; code->c_name; code++)
		if (!strcmp(str, code->c_name))
			return LOG_UPTO(code->c_val);

	return -1;
}

int main(int argc, char **argv)
{
	struct ev_loop *loop = ev_default_loop(0);
	const char *file = DEFAULT_CONFIG;
	struct ev_signal sigusr[2];
	int err, opt, logopt = 0;
	json_t *cfg, *ins, *outs;
	json_error_t jerr;

	while ((opt = getopt_long(argc, argv, sopts, lopts, NULL)) > 0) {
		switch (opt) {
		case 'd':
			logopt |= LOG_PERROR;
			break;
		case 'f':
			file = optarg;
			break;
		case 'h':
			usage();
			return 0;
		case 'l':
			logmask = logmask_from_str(optarg);
			if (logmask < 0) {
				fprintf(stderr, "Invalid loglevel '%s'\n\n", optarg);
				usage();
				exit(1);
			}
			break;
		case 'v':
			puts(PACKAGE_STRING);
			return 0;

		default:
			fprintf(stderr, "Unknown option '%c'\n\n", opt);
			usage();
			return 1;
		}
	}

	openlog(NULL, logopt, LOG_DAEMON);
	setlogmask(logmask);

	if (!strcmp(file, "-"))
		cfg = json_loadf(stdin, 0, &jerr);
	else
		cfg = json_load_file(file, 0, &jerr);

	if (!cfg) {
		log_cri("Unable to parse config (%s:%d): %s",
		   jerr.source, jerr.line, jerr.text);
		return 1;
	}

	err = json_unpack(cfg, "{s:o}", "input", &ins);
	if (err) {
		log_wrn("Configuration does not define any inputs");
		ins = json_object();
	}

	err = json_unpack(cfg, "{s:o}", "output", &outs);
	if (err) {
		log_cri("Configuration does not define any outputs");
		return 1;
	}

	err = in_probe(ins);
	if (err) {
		log_cri("Unable to probe inputs (%d)", err);
		return 1;
	}

	err = out_probe(outs);
	if (err) {
		log_cri("Unable to probe outputs (%d)", err);
		return 1;
	}

	err = out_update(NULL);
	if (err) {
		log_cri("Unable to set initial output states (%d)\n", err);
		return 1;
	}

	ev_signal_init(&sigusr[0], sigusr1_cb, SIGUSR1);
	ev_signal_init(&sigusr[1], sigusr2_cb, SIGUSR2);
	ev_signal_start(loop, &sigusr[0]);
	ev_signal_start(loop, &sigusr[1]);

	log_not("Entering event loop");
	return ev_run(loop, 0);
}
