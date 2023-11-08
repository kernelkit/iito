#include "iito.h"

int main(int argc, char **argv)
{
	struct ev_loop *loop = ev_default_loop(0);
	json_t *cfg, *ins, *outs;
	json_error_t jerr;
	int err;

	if (argc < 2)
		return 1;

	openlog(NULL, LOG_PERROR, LOG_DAEMON);
	setlogmask(LOG_UPTO(LOG_DEBUG));

	if (!strcmp(argv[1], "-"))
		cfg = json_loadf(stdin, 0, &jerr);
	else
		cfg = json_load_file(argv[1], 0, &jerr);

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

	log_not("Entering event loop");
	return ev_run(loop, 0);
}
