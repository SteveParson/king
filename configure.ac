AC_INIT([discord2],[0.1],[you@example.invalid])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC
AC_PROG_RANLIB

dnl --- Platform-specific linker flags for dead-code stripping --------------
AC_CANONICAL_HOST
case "$host_os" in
  darwin*)
    DEAD_STRIP_LDFLAGS="-Wl,-dead_strip"
    ;;
  *)
    DEAD_STRIP_LDFLAGS="-Wl,--gc-sections -s"
    ;;
esac
AC_SUBST([DEAD_STRIP_LDFLAGS])

AC_CONFIG_FILES([Makefile vendor/Makefile src/Makefile include/Makefile tests/Makefile])
AC_OUTPUT
