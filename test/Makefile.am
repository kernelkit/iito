EXTRA_DIST = test.sh

check_PROGRAMS = uled
uled_CFLAGS = -Wall -Wextra
uled_SOURCES = uled.c

TESTS_ENVIRONMENT = IITOD="$(abs_top_builddir)/src/iitod -"
TESTS = test.sh
