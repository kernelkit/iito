sbin_PROGRAMS = iitod iitoctl

iitod_CPPFLAGS = -include $(top_builddir)/config.h -DSYSCONFDIR=\"$(sysconfdir)\"
iitod_CFLAGS   = -Wall -Wextra -Wno-unused-parameter
iitod_CFLAGS  += $(libev_CFLAGS) $(libjansson_CFLAGS) $(libudev_CFLAGS)
iitod_LDADD    = $(libev_LIBS) $(libjansson_LIBS) $(libudev_LIBS)
iitod_SOURCES  = \
	in-path.c \
	in-udev.c \
	\
	out-led.c \
	\
	in.c out.c main.c uddev.c iito.h \
	ipc.c ipc.h

iitoctl_CPPFLAGS = -include $(top_builddir)/config.h
iitoctl_CFLAGS   = -Wall -Wextra $(libjansson_CFLAGS)
iitoctl_LDADD    = $(libjansson_LIBS)
iitoctl_SOURCES  = iitoctl.c ipc.h
