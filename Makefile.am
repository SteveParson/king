SUBDIRS = include src tests

small:
	$(MAKE) -C src \
		CFLAGS="$(CFLAGS) -Os -ffunction-sections -fdata-sections" \
		LDFLAGS="$(LDFLAGS) @DEAD_STRIP_LDFLAGS@"

tiny:
	$(MAKE) -C src \
		CFLAGS="$(CFLAGS) -Os -ffunction-sections -fdata-sections -flto \
		-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector" \
		CPPFLAGS="$(CPPFLAGS) -DLOG_LEVEL_COMPILE=0" \
		LDFLAGS="$(LDFLAGS) @DEAD_STRIP_LDFLAGS@"

sizes:
	@$(MAKE) -C src clean
	@$(MAKE) -C src
	@echo "normal size: $$(wc -c < src/discord2 | tr -d ' ') bytes"
	@$(MAKE) -C src clean
	@$(MAKE) -C src CFLAGS="$(CFLAGS) -Os -ffunction-sections -fdata-sections -flto \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector" \
	CPPFLAGS="$(CPPFLAGS) -DLOG_LEVEL_COMPILE=0" \
	LDFLAGS="$(LDFLAGS) @DEAD_STRIP_LDFLAGS@"
	@echo "tiny size: $$(wc -c < src/discord2 | tr -d ' ') bytes"

EXTRA_DIST = autogen.sh

lint:
	@command -v clang-tidy >/dev/null 2>&1 || { echo "clang-tidy not found"; exit 1; }
	@clang-tidy -p . $(top_srcdir)/src/*.c -- -I$(top_srcdir)/include @OPENSSL_CFLAGS@ $(CPPFLAGS) $(CFLAGS)

format:
	@command -v clang-format >/dev/null 2>&1 || { echo "clang-format not found"; exit 1; }
	@clang-format -i $(top_srcdir)/src/*.c $(top_srcdir)/include/*.h
