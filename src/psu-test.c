#define _GNU_SOURCE

#include <poll.h>
#include <stdio.h>

#include <libudev.h>

int main(void)
{
	struct udev_monitor *mon;
	struct udev_device *dev;
	struct pollfd pfd;
	struct udev *ud;

	ud = udev_new();
	mon = udev_monitor_new_from_netlink(ud, "kernel");
	udev_monitor_filter_add_match_subsystem_devtype(mon, "power_supply", NULL);
	udev_monitor_enable_receiving(mon);
	pfd.fd = udev_monitor_get_fd(mon);

	pfd.events = POLLIN;
	while (ppoll(&pfd, 1, NULL, NULL) > 0) {
		dev = udev_monitor_receive_device(mon);
		if (dev) {
			printf("I: ACTION=%s\n", udev_device_get_action(dev));
			printf("I: DEVNAME=%s\n", udev_device_get_sysname(dev));
			printf("I: DEVPATH=%s\n", udev_device_get_devpath(dev));
			printf("I: ONLINE=%s\n", udev_device_get_sysattr_value(dev, "online") ? : "NULL");
			printf("---\n");

			udev_device_unref(dev);
		}
	}
}
