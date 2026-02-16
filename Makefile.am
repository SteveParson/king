SUBDIRS = vendor include src tests

BEARSSL_TINY_FLAGS = \
	CFLAGS="$(CFLAGS) -Os -ffunction-sections -fdata-sections -flto \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector" \
	LDFLAGS="$(LDFLAGS) @DEAD_STRIP_LDFLAGS@"

small:
	$(MAKE) -C vendor \
		CFLAGS="$(CFLAGS) -Os -ffunction-sections -fdata-sections" \
		LDFLAGS="$(LDFLAGS) @DEAD_STRIP_LDFLAGS@"
	$(MAKE) -C src \
		CFLAGS="$(CFLAGS) -Os -ffunction-sections -fdata-sections" \
		LDFLAGS="$(LDFLAGS) @DEAD_STRIP_LDFLAGS@"

tiny:
	$(MAKE) -C vendor $(BEARSSL_TINY_FLAGS)
	$(MAKE) -C src \
		CFLAGS="$(CFLAGS) -Os -ffunction-sections -fdata-sections -flto \
		-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector" \
		CPPFLAGS="$(CPPFLAGS) -DLOG_LEVEL_COMPILE=0" \
		LDFLAGS="$(LDFLAGS) @DEAD_STRIP_LDFLAGS@"

sizes:
	@$(MAKE) -C vendor clean
	@$(MAKE) -C src clean
	@$(MAKE) -C vendor
	@$(MAKE) -C src
	@echo "normal size: $$(wc -c < src/discord2 | tr -d ' ') bytes"
	@$(MAKE) -C vendor clean
	@$(MAKE) -C src clean
	@$(MAKE) -C vendor $(BEARSSL_TINY_FLAGS)
	@$(MAKE) -C src CFLAGS="$(CFLAGS) -Os -ffunction-sections -fdata-sections -flto \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector" \
	CPPFLAGS="$(CPPFLAGS) -DLOG_LEVEL_COMPILE=0" \
	LDFLAGS="$(LDFLAGS) @DEAD_STRIP_LDFLAGS@"
	@echo "tiny size: $$(wc -c < src/discord2 | tr -d ' ') bytes"

EXTRA_DIST = autogen.sh

lint:
	@command -v clang-tidy >/dev/null 2>&1 || { echo "clang-tidy not found"; exit 1; }
	@clang-tidy -p . $(top_srcdir)/src/*.c -- -I$(top_srcdir)/include -I$(top_srcdir)/vendor/bearssl/inc $(CPPFLAGS) $(CFLAGS)

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
	@clang-format -i $(top_srcdir)/src/*.c $(top_srcdir)/include/*.h
