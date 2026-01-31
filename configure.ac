AC_INIT([discord2],[0.1],[you@example.invalid])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AC_PROG_CC

PKG_PROG_PKG_CONFIG

AC_CHECK_HEADERS([openssl/ssl.h openssl/rand.h], [], [
  AC_MSG_ERROR([OpenSSL headers not found. Install openssl-devel/libssl-dev])
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

AC_CONFIG_FILES([Makefile src/Makefile include/Makefile])
AC_OUTPUT
