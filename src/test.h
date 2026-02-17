/*
 * Minimal test framework.
 *
 * Usage:
 *   ASSERT(expr)          — pass if expr is non-zero
 *   ASSERT_STR(got, want) — pass if strings are equal; prints both on failure
 *   TEST_SUMMARY()        — print "N passed, M failed" and return exit code
 */
#ifndef TEST_H
#define TEST_H

#include <stdio.h>
#include <string.h>

static int _pass = 0, _fail = 0;

#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        if (cond) {                                                                                \
            _pass++;                                                                               \
        } else {                                                                                   \
            fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                        \
            _fail++;                                                                               \
        }                                                                                          \
    } while (0)

#define ASSERT_STR(got, want)                                                                      \
    do {                                                                                           \
        const char *_g = (got);                                                                    \
        const char *_w = (want);                                                                   \
        if (strcmp(_g, _w) == 0) {                                                                 \
            _pass++;                                                                               \
        } else {                                                                                   \
            fprintf(stderr, "FAIL %s:%d  got=\"%s\" want=\"%s\"\n", __FILE__, __LINE__, _g, _w);   \
            _fail++;                                                                               \
        }                                                                                          \
    } while (0)

/* Print summary and return 0 (all passed) or 1 (some failed). */
#define TEST_SUMMARY()                                                                             \
    do {                                                                                           \
        printf("%d passed, %d failed\n", _pass, _fail);                                            \
        return _fail ? 1 : 0;                                                                      \
    } while (0)

#endif
