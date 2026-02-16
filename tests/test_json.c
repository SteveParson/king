/*
 * Tests for the JSON parser (src/json.c).
 *
 * json.c is a targeted parser that extracts only the fields the Discord bot
 * needs.  These tests cover:
 *   - json_find_key      locate a top-level key
 *   - json_get_int       read an integer value
 *   - json_get_string    read a string value (with escape handling)
 *   - json_get_*_in_object  read from a nested object (e.g. "d" field)
 *
 * Tests also use real Gateway payload shapes so failures map to actual
 * Discord protocol breakage.
 */

#include "json.h"
#include "test.h"

int main(void) {
    char      out[256];
    long long n;

    /* ------------------------------------------------------------------
     * json_find_key — returns pointer to value, or NULL if absent
     * ------------------------------------------------------------------ */
    ASSERT(json_find_key("{\"op\":10}", "op") != NULL);
    ASSERT(json_find_key("{\"op\":10}", "missing") == NULL);
    ASSERT(json_find_key(NULL, "op") == NULL);

    /* "op" must not match a key named "ops" (checks exact key length) */
    ASSERT(json_find_key("{\"ops\":20}", "op") == NULL);

    /* ------------------------------------------------------------------
     * json_get_int — extract an integer (positive, zero, negative)
     * ------------------------------------------------------------------ */
    ASSERT(json_get_int("{\"op\":10}", "op", &n) && n == 10);
    ASSERT(json_get_int("{\"op\":0}", "op", &n) && n == 0);
    ASSERT(json_get_int("{\"seq\":-1}", "seq", &n) && n == -1);

    /* missing key must return 0 (not found) */
    ASSERT(!json_get_int("{\"op\":10}", "missing", &n));

    /* key present but value is not a number must also return 0 */
    ASSERT(!json_get_int("{\"op\":\"hello\"}", "op", &n));

    /* "op" must not steal the value from "ops" when both keys exist */
    ASSERT(json_get_int("{\"ops\":20,\"op\":10}", "op", &n) && n == 10);

    /* ------------------------------------------------------------------
     * json_get_string — extract a string value with escape handling
     * ------------------------------------------------------------------ */
    ASSERT(json_get_string("{\"t\":\"MESSAGE_CREATE\"}", "t", out, sizeof(out)));
    ASSERT_STR(out, "MESSAGE_CREATE");

    /* all standard JSON string escapes */
    ASSERT(json_get_string("{\"s\":\"a\\nb\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "a\nb"); /* \n → newline */

    ASSERT(json_get_string("{\"s\":\"a\\rb\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "a\rb"); /* \r → carriage return */

    ASSERT(json_get_string("{\"s\":\"a\\tb\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "a\tb"); /* \t → tab */

    ASSERT(json_get_string("{\"s\":\"a\\\\b\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "a\\b"); /* \\ → backslash */

    ASSERT(json_get_string("{\"s\":\"a\\\"b\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "a\"b"); /* \" → double-quote */

    /* \uXXXX is silently skipped by this minimal parser (no UTF-16 decode) */
    ASSERT(json_get_string("{\"s\":\"a\\u0041b\"}", "s", out, sizeof(out)));
    ASSERT_STR(out, "ab");

    /* missing key */
    ASSERT(!json_get_string("{\"t\":\"x\"}", "missing", out, sizeof(out)));

    /* truncation: string is clamped to the buffer without overflowing */
    char small[3]; /* room for 2 chars + NUL */
    ASSERT(json_get_string("{\"s\":\"hello\"}", "s", small, sizeof(small)));
    ASSERT_STR(small, "he");

    /* ------------------------------------------------------------------
     * Real Discord HELLO payload (op=10)
     * ------------------------------------------------------------------ */
    const char* hello = "{\"op\":10,\"d\":{\"heartbeat_interval\":41250}}";

    ASSERT(json_get_int(hello, "op", &n) && n == 10);

    json_object_key hb_key = {"d", "heartbeat_interval"};
    ASSERT(json_get_int_in_object(hello, &hb_key, &n) && n == 41250);

    /* ------------------------------------------------------------------
     * Real Discord MESSAGE_CREATE payload (op=0, t="MESSAGE_CREATE")
     * ------------------------------------------------------------------ */
    const char* msg =
        "{\"op\":0,\"t\":\"MESSAGE_CREATE\","
        "\"d\":{\"id\":\"111\",\"channel_id\":\"222\",\"content\":\"hello king\"}}";

    json_object_key content_key = {"d", "content"};
    ASSERT(json_get_string_in_object(msg, &content_key, out, sizeof(out)));
    ASSERT_STR(out, "hello king");

    json_object_key chan_key = {"d", "channel_id"};
    ASSERT(json_get_string_in_object(msg, &chan_key, out, sizeof(out)));
    ASSERT_STR(out, "222");

    /* missing nested key must return 0 */
    json_object_key bad_key = {"d", "nosuchfield"};
    ASSERT(!json_get_string_in_object(msg, &bad_key, out, sizeof(out)));

    TEST_SUMMARY();
}
