/* String utilities used by the Discord bot. */
#ifndef STR_H
#define STR_H

#include <stddef.h>

/* Case-insensitive whole-word search. Returns 1 if needle appears as a
   complete word in hay (bounded by non-alphanumeric chars or string edges). */
int contains_case(const char *hay, const char *needle);

/* Escape a C string for embedding between JSON double-quotes.
   Writes at most cap bytes (including NUL) to out. */
void json_escape(const char *in, char *out, size_t cap);

/* RFC 3986 percent-encoding. Unreserved chars pass through; everything
   else becomes %XX.  Writes at most cap bytes (including NUL) to out. */
void url_encode(const char *in, char *out, size_t cap);

/* Parse the three-digit status code from an HTTP response line.
   Returns -1 on malformed input. */
int http_status_code(const char *resp);

#endif
