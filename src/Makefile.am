sbin_PROGRAMS = iitod

iitod_CPPFLAGS = -include $(top_builddir)/config.h
iitod_CFLAGS   = -Wall -Wextra -Wno-unused-parameter
iitod_CFLAGS  += $(libev_CFLAGS) $(libjansson_CFLAGS) $(libudev_CFLAGS)
iitod_LDADD    = $(libev_LIBS) $(libjansson_LIBS) $(libudev_LIBS)
iitod_SOURCES  = \
	in-path.c \
	in-udev.c \
	\
	out-led.c \
	\
	in.c out.c main.c uddev.c iito.h
