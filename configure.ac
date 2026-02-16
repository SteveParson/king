AC_INIT([discord2],[0.1],[you@example.invalid])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC

PKG_PROG_PKG_CONFIG

dnl --- OpenSSL detection (pkg-config first, then manual fallback) ----------
openssl_found=no
AS_IF([test -n "$PKG_CONFIG"], [
  PKG_CHECK_MODULES([OPENSSL], [openssl], [openssl_found=yes], [openssl_found=no])
])
AS_IF([test "$openssl_found" = "no"], [
  AC_CHECK_HEADERS([openssl/ssl.h openssl/rand.h], [], [
    AC_MSG_ERROR([OpenSSL headers not found. Install openssl-devel (RHEL), libssl-dev (Debian), or: brew install openssl])
  ])
  AC_CHECK_LIB([ssl],[SSL_new],
    [OPENSSL_LIBS="-lssl"],
    [AC_MSG_ERROR([OpenSSL libssl not found])]
  )
  AC_CHECK_LIB([crypto],[RAND_bytes],
    [OPENSSL_LIBS="$OPENSSL_LIBS -lcrypto"],
    [AC_MSG_ERROR([OpenSSL libcrypto not found])]
  )
  AC_SUBST([OPENSSL_LIBS])
])

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

AC_CONFIG_FILES([Makefile src/Makefile include/Makefile tests/Makefile])
AC_OUTPUT
