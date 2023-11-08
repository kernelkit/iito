#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#include <linux/uleds.h>

#define N_LEDS 4

static const struct uleds_user_dev led_spec[N_LEDS] = {
	{ .name = "iito-test::0", .max_brightness =   1 },
	{ .name = "iito-test::1", .max_brightness =  15 },
	{ .name = "iito-test::2", .max_brightness = 127 },
	{ .name = "iito-test::3", .max_brightness = 255 },
};

int main(void)
{
	struct pollfd pfd[N_LEDS + 1] = {
		/* Detect, and exit, when test.sh closes its side of
		 * the pipe connected to our stdout */
		[N_LEDS] = { .fd = 1, .events = 0 }
	};
	int val[N_LEDS] = { 0 };
	int i;

	for (i = 0; i < N_LEDS; i++) {
		pfd[i].events = POLLIN;

		pfd[i].fd = open("/dev/uleds", O_RDWR);
		if (pfd[i].fd < 0) {
			fprintf(stderr, "Unable to open /dev/uleds: %m\n");
			return 1;
		}

		if (write(pfd[i].fd, &led_spec[i], sizeof(led_spec[0])) != sizeof(led_spec[0])) {
			fprintf(stderr, "Unable to setup %s: %m\n", led_spec[i].name);
			return 1;
		}
	}

	while (poll(pfd, N_LEDS + 1, -1) > 0) {
		if (pfd[N_LEDS].revents)
			break;

		for (i = 0; i < N_LEDS; i++) {
			if (!pfd[i].revents)
				continue;

			if (read(pfd[i].fd, &val[i], sizeof(val[0])) != sizeof(val[0])) {
				fprintf(stderr, "Failed reading of %s: %m\n", led_spec[i].name);
				return 1;
			}
		}

		for (i = 0; i < N_LEDS; i++) {
			printf("%u ", val[i]);
		}

		putchar('\n');
		fflush(stdout);
	}

	for (i = 0; i < N_LEDS; i++)
		close(pfd[i].fd);

	return 0;
}
